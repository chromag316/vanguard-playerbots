#include "playerbot/playerbot.h"
#include "GambitMgr.h"

#include "Chat/Chat.h"
#include "Database/DatabaseEnv.h"
#include "playerbot/ServerFacade.h"

#include <set>

using namespace ai;

static bool IsPlayableClass(uint32 classId)
{
    switch (classId)
    {
    case CLASS_WARRIOR: case CLASS_PALADIN: case CLASS_HUNTER: case CLASS_ROGUE: case CLASS_PRIEST:
    case CLASS_SHAMAN: case CLASS_MAGE: case CLASS_WARLOCK: case CLASS_DRUID:
        return true;
    default:
        return false;
    }
}

static GambitSlot ReadSlot(Field* fields)
{
    GambitSlot gambit;
    gambit.slot = fields[0].GetUInt8();
    gambit.enabled = fields[1].GetUInt8() != 0;
    gambit.conditionId = fields[2].GetUInt32();
    gambit.conditionParam = fields[3].GetUInt32();
    gambit.actionId = fields[4].GetUInt32();
    gambit.actionParam = fields[5].GetUInt32();
    return gambit;
}

void GambitMgr::LoadCatalog()
{
    conditions.clear();
    actions.clear();

    if (auto result = WorldDatabase.Query("SELECT `id`, `name`, `param_min`, `param_max` FROM `vanguard_gambit_condition`"))
    {
        do
        {
            Field* fields = result->Fetch();
            GambitConditionInfo info;
            info.name = fields[1].GetCppString();
            info.paramMin = fields[2].GetUInt32();
            info.paramMax = fields[3].GetUInt32();
            conditions[fields[0].GetUInt32()] = info;
        } while (result->NextRow());
    }

    if (auto result = WorldDatabase.Query("SELECT `id`, `name`, `needs_spell`, `self_only` FROM `vanguard_gambit_action`"))
    {
        do
        {
            Field* fields = result->Fetch();
            GambitActionInfo info;
            info.name = fields[1].GetCppString();
            info.needsSpell = fields[2].GetUInt8() != 0;
            info.selfOnly = fields[3].GetUInt8() != 0;
            actions[fields[0].GetUInt32()] = info;
        } while (result->NextRow());
    }

    passiveCatalog.clear();
    if (auto result = WorldDatabase.Query("SELECT `class`, `spell_id`, `exclusive_group` FROM `vanguard_gambit_passive` ORDER BY `class`, `sort`"))
    {
        do
        {
            Field* fields = result->Fetch();
            GambitPassiveInfo info;
            info.spellId = fields[1].GetUInt32();
            info.group = fields[2].GetCppString();
            passiveCatalog[fields[0].GetUInt8()].push_back(info);
        } while (result->NextRow());
    }

    std::lock_guard<std::mutex> guard(cacheLock);
    classDefaults.clear();
    classPassives.clear();
    cache.clear();
    uint32 defaults = 0;
    if (auto result = CharacterDatabase.Query("SELECT `class`, `spell_id` FROM `vanguard_passive_class_default` ORDER BY `class`, `slot`"))
    {
        do
        {
            Field* fields = result->Fetch();
            classPassives[fields[0].GetUInt8()].push_back(fields[1].GetUInt32());
        } while (result->NextRow());
    }
    if (auto result = CharacterDatabase.Query("SELECT `slot`, `enabled`, `condition_id`, `condition_param`, `action_id`, `action_param`, `class` "
        "FROM `vanguard_gambit_class_default` ORDER BY `class`, `slot`"))
    {
        do
        {
            Field* fields = result->Fetch();
            GambitSlot gambit = ReadSlot(fields);
            if (gambit.slot >= 1 && gambit.slot <= GAMBIT_MAX_SLOTS)
            {
                classDefaults[fields[6].GetUInt8()].push_back(gambit);
                ++defaults;
            }
        } while (result->NextRow());
    }

    uint32 passives = 0;
    for (auto& entry : passiveCatalog)
        passives += uint32(entry.second.size());
    sLog.outString("Loaded %u gambit conditions, %u gambit actions, %u class default gambits, %u passive spells",
        uint32(conditions.size()), uint32(actions.size()), defaults, passives);
    if (passiveCatalog.empty())
        sLog.outError("Gambit passive catalog is empty: apply vanguard-db vanguard/world/0012_gambit_passives.sql.");
    if (conditions.empty() || actions.empty())
        sLog.outError("Gambit catalog is empty: apply vanguard-db vanguard/world/0010_gambit_catalog.sql. Companions will do nothing.");
}

GambitMgr::Companion& GambitMgr::Cached(uint32 guidLow)
{
    auto it = cache.find(guidLow);
    if (it != cache.end())
        return it->second;

    Companion companion;
    if (auto result = CharacterDatabase.PQuery("SELECT `class` FROM `characters` WHERE `guid` = '%u'", guidLow))
        companion.classId = result->Fetch()[0].GetUInt8();
    companion.custom = bool(CharacterDatabase.PQuery("SELECT 1 FROM `vanguard_gambit_custom` WHERE `guid` = '%u'", guidLow));
    if (companion.custom)
    {
        if (auto result = CharacterDatabase.PQuery("SELECT `slot`, `enabled`, `condition_id`, `condition_param`, `action_id`, `action_param` "
            "FROM `vanguard_gambit` WHERE `guid` = '%u' ORDER BY `slot`", guidLow))
        {
            do
            {
                GambitSlot gambit = ReadSlot(result->Fetch());
                if (gambit.slot >= 1 && gambit.slot <= GAMBIT_MAX_SLOTS)
                    companion.slots.push_back(gambit);
            } while (result->NextRow());
        }
        if (auto result = CharacterDatabase.PQuery("SELECT `spell_id` FROM `vanguard_passive` WHERE `guid` = '%u' AND `slot` BETWEEN 1 AND %u ORDER BY `slot`",
            guidLow, uint32(GAMBIT_MAX_PASSIVES)))
        {
            do
            {
                companion.passives.push_back(result->Fetch()[0].GetUInt32());
            } while (result->NextRow());
        }
    }
    return cache.emplace(guidLow, companion).first->second;
}

const std::vector<GambitSlot>& GambitMgr::Effective(Companion& companion)
{
    return companion.custom ? companion.slots : classDefaults[companion.classId];
}

const std::vector<uint32>& GambitMgr::EffectivePassives(Companion& companion)
{
    return companion.custom ? companion.passives : classPassives[companion.classId];
}

std::vector<uint32> GambitMgr::GetPassives(uint32 guidLow)
{
    std::lock_guard<std::mutex> guard(cacheLock);
    return EffectivePassives(Cached(guidLow));
}

std::vector<uint32> GambitMgr::GetClassPassives(uint8 classId)
{
    std::lock_guard<std::mutex> guard(cacheLock);
    return classPassives[classId];
}

void GambitMgr::WriteOwn(uint32 guidLow, const std::vector<GambitSlot>& slots, const std::vector<uint32>& passives)
{
    // All values are validated numbers
    CharacterDatabase.PExecute("DELETE FROM `vanguard_gambit` WHERE `guid` = '%u'", guidLow);
    for (const GambitSlot& gambit : slots)
    {
        CharacterDatabase.PExecute("INSERT INTO `vanguard_gambit` (`guid`, `slot`, `enabled`, `condition_id`, `condition_param`, `action_id`, `action_param`) "
            "VALUES ('%u', '%u', '%u', '%u', '%u', '%u', '%u')",
            guidLow, uint32(gambit.slot), uint32(gambit.enabled ? 1 : 0), gambit.conditionId, gambit.conditionParam, gambit.actionId, gambit.actionParam);
    }
    CharacterDatabase.PExecute("DELETE FROM `vanguard_passive` WHERE `guid` = '%u'", guidLow);
    for (size_t i = 0; i < passives.size(); ++i)
        CharacterDatabase.PExecute("INSERT INTO `vanguard_passive` (`guid`, `slot`, `spell_id`) VALUES ('%u', '%u', '%u')", guidLow, uint32(i + 1), passives[i]);
    CharacterDatabase.PExecute("INSERT IGNORE INTO `vanguard_gambit_custom` (`guid`) VALUES ('%u')", guidLow);
}

std::vector<GambitSlot> GambitMgr::GetSlots(uint32 guidLow)
{
    std::lock_guard<std::mutex> guard(cacheLock);
    return Effective(Cached(guidLow));
}

bool GambitMgr::GetSlot(uint32 guidLow, uint8 slot, GambitSlot& out)
{
    if (conditions.empty())
        return false;

    std::lock_guard<std::mutex> guard(cacheLock);
    for (const GambitSlot& gambit : Effective(Cached(guidLow)))
    {
        if (gambit.slot == slot)
        {
            out = gambit;
            return true;
        }
    }
    return false;
}

bool GambitMgr::IsCustom(uint32 guidLow)
{
    std::lock_guard<std::mutex> guard(cacheLock);
    return Cached(guidLow).custom;
}

std::vector<GambitSlot> GambitMgr::GetClassDefault(uint8 classId)
{
    std::lock_guard<std::mutex> guard(cacheLock);
    return classDefaults[classId];
}

bool GambitMgr::Validate(uint32 guidLow, const std::vector<GambitSlot>& slots, std::string& error) const
{
    if (conditions.empty())
    {
        error = "the gambit catalog is not loaded";
        return false;
    }

    if (slots.size() > GAMBIT_MAX_SLOTS)
    {
        error = "at most 12 gambits";
        return false;
    }

    Player* player = nullptr;
    if (guidLow)
    {
        if (!CharacterDatabase.PQuery("SELECT 1 FROM `characters` WHERE `guid` = '%u'", guidLow))
        {
            error = "no character with that guid";
            return false;
        }
        player = sObjectMgr.GetPlayer(ObjectGuid(HIGHGUID_PLAYER, guidLow));
    }

    for (size_t i = 0; i < slots.size(); ++i)
    {
        const GambitSlot& gambit = slots[i];
        std::string where = "gambit " + std::to_string(i + 1) + ": ";

        auto condition = conditions.find(gambit.conditionId);
        if (condition == conditions.end())
        {
            error = where + "unknown condition " + std::to_string(gambit.conditionId);
            return false;
        }
        const GambitConditionInfo& cond = condition->second;
        bool paramOk = cond.paramMax == 0 ? gambit.conditionParam == 0 : (gambit.conditionParam >= cond.paramMin && gambit.conditionParam <= cond.paramMax);
        if (!paramOk)
        {
            error = where + "condition parameter out of range";
            return false;
        }

        auto action = actions.find(gambit.actionId);
        if (action == actions.end())
        {
            error = where + "unknown action " + std::to_string(gambit.actionId);
            return false;
        }

        if (action->second.selfOnly && !IsSelfGambitCondition(gambit.conditionId))
        {
            error = where + action->second.name + " needs a Self condition";
            return false;
        }

        if (!action->second.needsSpell)
        {
            if (gambit.actionParam != 0)
            {
                error = where + "this action takes no parameter";
                return false;
            }
            continue;
        }

        if (!sServerFacade.LookupSpellInfo(gambit.actionParam))
        {
            error = where + "unknown spell " + std::to_string(gambit.actionParam);
            return false;
        }

        if (!guidLow)
            continue;

        bool known = player ? player->HasSpell(gambit.actionParam)
            : bool(CharacterDatabase.PQuery("SELECT 1 FROM `character_spell` WHERE `guid` = '%u' AND `spell` = '%u'", guidLow, gambit.actionParam));
        if (!known)
        {
            error = where + "the companion does not know spell " + std::to_string(gambit.actionParam);
            return false;
        }
    }

    return true;
}

static std::vector<GambitSlot> Numbered(const std::vector<GambitSlot>& slots)
{
    std::vector<GambitSlot> numbered = slots;
    for (size_t i = 0; i < numbered.size(); ++i)
        numbered[i].slot = uint8(i + 1);
    return numbered;
}

bool GambitMgr::Save(uint32 guidLow, const std::vector<GambitSlot>& slots, std::string& error)
{
    if (!Validate(guidLow, slots, error))
        return false;

    std::vector<GambitSlot> numbered = Numbered(slots);

    // The passives stay as they are: its own, or (becoming custom now) its class default's
    std::lock_guard<std::mutex> guard(cacheLock);
    Companion& companion = Cached(guidLow);
    std::vector<uint32> passives = EffectivePassives(companion);

    CharacterDatabase.BeginTransaction();
    WriteOwn(guidLow, numbered, passives);
    CharacterDatabase.CommitTransaction();

    companion.custom = true;
    companion.slots = numbered;
    companion.passives = passives;
    return true;
}

bool GambitMgr::ValidatePassives(uint8 classId, const std::vector<uint32>& spells, std::string& error) const
{
    if (spells.size() > GAMBIT_MAX_PASSIVES)
    {
        error = "at most 3 passives";
        return false;
    }

    auto catalog = passiveCatalog.find(classId);
    std::set<std::string> groups;
    for (size_t i = 0; i < spells.size(); ++i)
    {
        std::string where = "passive " + std::to_string(i + 1) + ": ";
        const GambitPassiveInfo* info = nullptr;
        if (catalog != passiveCatalog.end())
        {
            for (const GambitPassiveInfo& entry : catalog->second)
            {
                if (entry.spellId == spells[i])
                    info = &entry;
            }
        }
        if (!info)
        {
            error = where + "spell " + std::to_string(spells[i]) + " is not a passive of this class";
            return false;
        }
        if (!groups.insert(info->group).second)
        {
            error = where + "only one passive of the kind '" + info->group + "' (they replace each other)";
            return false;
        }
    }
    return true;
}

bool GambitMgr::SavePassives(uint32 guidLow, const std::vector<uint32>& spells, std::string& error)
{
    if (!CharacterDatabase.PQuery("SELECT 1 FROM `characters` WHERE `guid` = '%u'", guidLow))
    {
        error = "no character with that guid";
        return false;
    }

    std::lock_guard<std::mutex> guard(cacheLock);
    Companion& companion = Cached(guidLow);
    if (!ValidatePassives(companion.classId, spells, error))
        return false;

    // The gambits stay as they are: its own, or (becoming custom now) its class default's
    std::vector<GambitSlot> slots = Effective(companion);

    CharacterDatabase.BeginTransaction();
    WriteOwn(guidLow, slots, spells);
    CharacterDatabase.CommitTransaction();

    companion.custom = true;
    companion.slots = slots;
    companion.passives = spells;
    return true;
}

bool GambitMgr::SaveClassPassives(uint8 classId, const std::vector<uint32>& spells, std::string& error)
{
    if (!IsPlayableClass(classId))
    {
        error = "unknown class " + std::to_string(classId);
        return false;
    }

    if (!ValidatePassives(classId, spells, error))
        return false;

    CharacterDatabase.BeginTransaction();
    CharacterDatabase.PExecute("DELETE FROM `vanguard_passive_class_default` WHERE `class` = '%u'", uint32(classId));
    for (size_t i = 0; i < spells.size(); ++i)
        CharacterDatabase.PExecute("INSERT INTO `vanguard_passive_class_default` (`class`, `slot`, `spell_id`) VALUES ('%u', '%u', '%u')", uint32(classId), uint32(i + 1), spells[i]);
    // A GM's list, even an empty one, is never replaced by the starting defaults again
    CharacterDatabase.PExecute("INSERT IGNORE INTO `vanguard_passive_class_seeded` (`class`) VALUES ('%u')", uint32(classId));
    CharacterDatabase.CommitTransaction();

    std::lock_guard<std::mutex> guard(cacheLock);
    classPassives[classId] = spells;
    return true;
}

bool GambitMgr::Reset(uint32 guidLow, std::string& error)
{
    if (!CharacterDatabase.PQuery("SELECT 1 FROM `characters` WHERE `guid` = '%u'", guidLow))
    {
        error = "no character with that guid";
        return false;
    }

    CharacterDatabase.BeginTransaction();
    CharacterDatabase.PExecute("DELETE FROM `vanguard_gambit` WHERE `guid` = '%u'", guidLow);
    CharacterDatabase.PExecute("DELETE FROM `vanguard_passive` WHERE `guid` = '%u'", guidLow);
    CharacterDatabase.PExecute("DELETE FROM `vanguard_gambit_custom` WHERE `guid` = '%u'", guidLow);
    CharacterDatabase.CommitTransaction();

    std::lock_guard<std::mutex> guard(cacheLock);
    Companion& companion = Cached(guidLow);
    companion.custom = false;
    companion.slots.clear();
    companion.passives.clear();
    return true;
}

bool GambitMgr::SaveClassDefault(uint8 classId, const std::vector<GambitSlot>& slots, std::string& error)
{
    if (!IsPlayableClass(classId))
    {
        error = "unknown class " + std::to_string(classId);
        return false;
    }

    if (!Validate(0, slots, error))
        return false;

    std::vector<GambitSlot> numbered = Numbered(slots);

    CharacterDatabase.BeginTransaction();
    CharacterDatabase.PExecute("DELETE FROM `vanguard_gambit_class_default` WHERE `class` = '%u'", uint32(classId));
    for (const GambitSlot& gambit : numbered)
    {
        CharacterDatabase.PExecute("INSERT INTO `vanguard_gambit_class_default` (`class`, `slot`, `enabled`, `condition_id`, `condition_param`, `action_id`, `action_param`) "
            "VALUES ('%u', '%u', '%u', '%u', '%u', '%u', '%u')",
            uint32(classId), uint32(gambit.slot), uint32(gambit.enabled ? 1 : 0), gambit.conditionId, gambit.conditionParam, gambit.actionId, gambit.actionParam);
    }
    // A GM's list, even an empty one, is never replaced by the starting defaults again
    CharacterDatabase.PExecute("INSERT IGNORE INTO `vanguard_gambit_class_seeded` (`class`) VALUES ('%u')", uint32(classId));
    CharacterDatabase.CommitTransaction();

    std::lock_guard<std::mutex> guard(cacheLock);
    classDefaults[classId] = numbered;
    return true;
}

std::string GambitMgr::Describe(const GambitSlot& gambit) const
{
    std::ostringstream out;
    out << uint32(gambit.slot) << ". " << (gambit.enabled ? "[on] " : "[off] ");

    auto condition = conditions.find(gambit.conditionId);
    std::string condName = condition != conditions.end() ? condition->second.name : "condition " + std::to_string(gambit.conditionId);
    size_t x = condName.find("X%");
    if (x != std::string::npos)
        condName.replace(x, 1, std::to_string(gambit.conditionParam));
    out << condName << " -> ";

    auto action = actions.find(gambit.actionId);
    out << (action != actions.end() ? action->second.name : "action " + std::to_string(gambit.actionId));
    if (action != actions.end() && action->second.needsSpell)
    {
        const SpellEntry* spell = sServerFacade.LookupSpellInfo(gambit.actionParam);
        out << " " << (spell ? spell->SpellName[0] : std::to_string(gambit.actionParam).c_str());
    }
    return out.str();
}

// Every argument must be a plain unsigned number: the command is built by the web app from
// validated integers and nothing else may get through.
static bool ParseNumbers(const std::string& args, std::vector<uint32>& numbers)
{
    std::istringstream in(args);
    std::string token;
    while (in >> token)
    {
        if (token.size() > 10 || token.find_first_not_of("0123456789") != std::string::npos)
            return false;
        unsigned long long value = std::stoull(token);
        if (value > 0xFFFFFFFFull)
            return false;
        numbers.push_back(uint32(value));
    }
    return true;
}

// numbers: <count> [<enabled> <condition> <param> <action> <param>] x count
static bool ParseSlots(ChatHandler* handler, const std::vector<uint32>& numbers, std::vector<GambitSlot>& slots)
{
    if (numbers.empty() || numbers.size() != 1 + size_t(numbers[0]) * 5)
    {
        handler->SendSysMessage("Gambits not saved: the count does not match the number of values");
        return false;
    }

    for (uint32 i = 0; i < numbers[0]; ++i)
    {
        const uint32* v = &numbers[1 + i * 5];
        if (v[0] > 1)
        {
            handler->SendSysMessage("Gambits not saved: enabled must be 0 or 1");
            return false;
        }
        GambitSlot gambit;
        gambit.enabled = v[0] == 1;
        gambit.conditionId = v[1];
        gambit.conditionParam = v[2];
        gambit.actionId = v[3];
        gambit.actionParam = v[4];
        slots.push_back(gambit);
    }
    return true;
}

static void ListPassives(ChatHandler* handler, const std::vector<uint32>& spells)
{
    for (size_t i = 0; i < spells.size(); ++i)
    {
        const SpellEntry* spell = sServerFacade.LookupSpellInfo(spells[i]);
        handler->PSendSysMessage("P%u. %s", uint32(i + 1), spell ? spell->SpellName[0] : std::to_string(spells[i]).c_str());
    }
}

bool GambitMgr::HandleConsoleCommand(ChatHandler* handler, char const* args)
{
    std::istringstream in(args ? args : "");
    std::string sub;
    in >> sub;
    bool passive = sub == "passive";
    if (passive)
        in >> sub;
    bool classDefault = sub == "default";
    if (classDefault)
        in >> sub;
    std::string rest;
    std::getline(in, rest);

    std::vector<uint32> numbers;
    bool known = passive ? sub == "save" : (sub == "list" || sub == "save" || (!classDefault && sub == "reset"));
    if (!known || !ParseNumbers(rest, numbers) || numbers.empty())
    {
        handler->SendSysMessage("Usage: .gambit list|reset <guid>");
        handler->SendSysMessage("       .gambit save <guid> <count> [<enabled> <condition> <param> <action> <param>] x count");
        handler->SendSysMessage("       .gambit default list <class>");
        handler->SendSysMessage("       .gambit default save <class> <count> [<enabled> <condition> <param> <action> <param>] x count");
        handler->SendSysMessage("       .gambit passive save <guid> <count> [<spell>] x count");
        handler->SendSysMessage("       .gambit passive default save <class> <count> [<spell>] x count");
        return false;
    }

    uint32 id = numbers[0];
    if (classDefault && id > 0xFF)
        id = 0; // not a class; uint8 would wrap it onto one
    std::vector<uint32> slotNumbers(numbers.begin() + 1, numbers.end());
    std::string error;

    if (passive)
    {
        if (slotNumbers.empty() || slotNumbers.size() != 1 + size_t(slotNumbers[0]))
        {
            handler->SendSysMessage("Passives not saved: the count does not match the number of spells");
            return false;
        }
        std::vector<uint32> spells(slotNumbers.begin() + 1, slotNumbers.end());
        bool saved = classDefault ? sGambitMgr.SaveClassPassives(uint8(id), spells, error) : sGambitMgr.SavePassives(id, spells, error);
        if (!saved)
        {
            handler->PSendSysMessage("Passives not saved: %s", error.c_str());
            return false;
        }
        handler->PSendSysMessage(classDefault ? "Saved %u default passives for class %u" : "Saved %u passives for %u", uint32(spells.size()), id);
        return true;
    }

    if (sub == "list")
    {
        std::vector<GambitSlot> slots;
        std::vector<uint32> passives;
        if (classDefault)
        {
            slots = sGambitMgr.GetClassDefault(uint8(id));
            passives = sGambitMgr.GetClassPassives(uint8(id));
            handler->PSendSysMessage("Default gambits of class %u: %u, passives: %u", id, uint32(slots.size()), uint32(passives.size()));
        }
        else
        {
            slots = sGambitMgr.GetSlots(id);
            passives = sGambitMgr.GetPassives(id);
            handler->PSendSysMessage("Gambits of %u (%s): %u, passives: %u", id, sGambitMgr.IsCustom(id) ? "own list" : "class default",
                uint32(slots.size()), uint32(passives.size()));
        }
        ListPassives(handler, passives);
        for (const GambitSlot& gambit : slots)
            handler->PSendSysMessage("%s", sGambitMgr.Describe(gambit).c_str());
        return true;
    }

    if (sub == "reset")
    {
        if (!sGambitMgr.Reset(id, error))
        {
            handler->PSendSysMessage("Gambits not reset: %s", error.c_str());
            return false;
        }
        handler->PSendSysMessage("%u uses its class default gambits again", id);
        return true;
    }

    std::vector<GambitSlot> slots;
    if (!ParseSlots(handler, slotNumbers, slots))
        return false;

    bool saved = classDefault ? sGambitMgr.SaveClassDefault(uint8(id), slots, error) : sGambitMgr.Save(id, slots, error);
    if (!saved)
    {
        handler->PSendSysMessage("Gambits not saved: %s", error.c_str());
        return false;
    }

    if (classDefault)
        handler->PSendSysMessage("Saved %u default gambits for class %u", uint32(slots.size()), id);
    else
        handler->PSendSysMessage("Saved %u gambits for %u", uint32(slots.size()), id);
    return true;
}
