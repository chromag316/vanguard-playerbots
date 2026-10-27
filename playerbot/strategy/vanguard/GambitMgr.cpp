#include "playerbot/playerbot.h"
#include "GambitMgr.h"

#include "Chat/Chat.h"
#include "Database/DatabaseEnv.h"
#include "playerbot/ServerFacade.h"

using namespace ai;

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

    if (auto result = WorldDatabase.Query("SELECT `id`, `name`, `needs_spell` FROM `vanguard_gambit_action`"))
    {
        do
        {
            Field* fields = result->Fetch();
            GambitActionInfo info;
            info.name = fields[1].GetCppString();
            info.needsSpell = fields[2].GetUInt8() != 0;
            actions[fields[0].GetUInt32()] = info;
        } while (result->NextRow());
    }

    sLog.outString("Loaded %u gambit conditions, %u gambit actions", uint32(conditions.size()), uint32(actions.size()));
    if (conditions.empty() || actions.empty())
        sLog.outError("Gambit catalog is empty: apply vanguard-db vanguard/world/0010_gambit_catalog.sql. Gambits are off.");
}

std::vector<GambitSlot> GambitMgr::LoadSlots(uint32 guidLow) const
{
    std::vector<GambitSlot> slots;
    auto result = CharacterDatabase.PQuery("SELECT `slot`, `enabled`, `condition_id`, `condition_param`, `action_id`, `action_param` "
        "FROM `vanguard_gambit` WHERE `guid` = '%u' ORDER BY `slot`", guidLow);
    if (result)
    {
        do
        {
            Field* fields = result->Fetch();
            GambitSlot gambit;
            gambit.slot = fields[0].GetUInt8();
            gambit.enabled = fields[1].GetUInt8() != 0;
            gambit.conditionId = fields[2].GetUInt32();
            gambit.conditionParam = fields[3].GetUInt32();
            gambit.actionId = fields[4].GetUInt32();
            gambit.actionParam = fields[5].GetUInt32();
            if (gambit.slot >= 1 && gambit.slot <= GAMBIT_MAX_SLOTS)
                slots.push_back(gambit);
        } while (result->NextRow());
    }
    return slots;
}

std::vector<GambitSlot> GambitMgr::GetSlots(uint32 guidLow)
{
    std::lock_guard<std::mutex> guard(cacheLock);
    auto it = cache.find(guidLow);
    if (it == cache.end())
        it = cache.emplace(guidLow, LoadSlots(guidLow)).first;
    return it->second;
}

bool GambitMgr::GetSlot(uint32 guidLow, uint8 slot, GambitSlot& out)
{
    if (conditions.empty())
        return false;

    std::lock_guard<std::mutex> guard(cacheLock);
    auto it = cache.find(guidLow);
    if (it == cache.end())
        it = cache.emplace(guidLow, LoadSlots(guidLow)).first;

    for (const GambitSlot& gambit : it->second)
    {
        if (gambit.slot == slot)
        {
            out = gambit;
            return true;
        }
    }
    return false;
}

bool GambitMgr::Validate(uint32 guidLow, const std::vector<GambitSlot>& slots, std::string& error) const
{
    if (slots.size() > GAMBIT_MAX_SLOTS)
    {
        error = "at most 12 gambits";
        return false;
    }

    auto character = CharacterDatabase.PQuery("SELECT 1 FROM `characters` WHERE `guid` = '%u'", guidLow);
    if (!character)
    {
        error = "no character with that guid";
        return false;
    }

    Player* player = sObjectMgr.GetPlayer(ObjectGuid(HIGHGUID_PLAYER, guidLow));

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

bool GambitMgr::Save(uint32 guidLow, const std::vector<GambitSlot>& slots, std::string& error)
{
    if (conditions.empty())
    {
        error = "the gambit catalog is not loaded";
        return false;
    }

    if (!Validate(guidLow, slots, error))
        return false;

    std::vector<GambitSlot> numbered = slots;
    for (size_t i = 0; i < numbered.size(); ++i)
        numbered[i].slot = uint8(i + 1);

    // All values are validated numbers
    CharacterDatabase.BeginTransaction();
    CharacterDatabase.PExecute("DELETE FROM `vanguard_gambit` WHERE `guid` = '%u'", guidLow);
    for (const GambitSlot& gambit : numbered)
    {
        CharacterDatabase.PExecute("INSERT INTO `vanguard_gambit` (`guid`, `slot`, `enabled`, `condition_id`, `condition_param`, `action_id`, `action_param`) "
            "VALUES ('%u', '%u', '%u', '%u', '%u', '%u', '%u')",
            guidLow, uint32(gambit.slot), uint32(gambit.enabled ? 1 : 0), gambit.conditionId, gambit.conditionParam, gambit.actionId, gambit.actionParam);
    }
    CharacterDatabase.CommitTransaction();

    std::lock_guard<std::mutex> guard(cacheLock);
    cache[guidLow] = numbered;
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
static bool ParseNumbers(char const* args, std::vector<uint32>& numbers)
{
    std::istringstream in(args ? args : "");
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

bool GambitMgr::HandleConsoleCommand(ChatHandler* handler, char const* args)
{
    std::string text = args ? args : "";
    size_t space = text.find(' ');
    std::string sub = text.substr(0, space);
    std::string rest = space == std::string::npos ? "" : text.substr(space + 1);

    std::vector<uint32> numbers;
    if ((sub != "save" && sub != "list") || !ParseNumbers(rest.c_str(), numbers) || numbers.empty())
    {
        handler->SendSysMessage("Usage: .gambit list <guid>");
        handler->SendSysMessage("       .gambit save <guid> <count> [<enabled> <condition> <param> <action> <param>] x count");
        handler->SetSentErrorMessage(true);
        return false;
    }

    uint32 guidLow = numbers[0];

    if (sub == "list")
    {
        std::vector<GambitSlot> slots = sGambitMgr.GetSlots(guidLow);
        handler->PSendSysMessage("Gambits of %u: %u", guidLow, uint32(slots.size()));
        for (const GambitSlot& gambit : slots)
            handler->PSendSysMessage("%s", sGambitMgr.Describe(gambit).c_str());
        return true;
    }

    if (numbers.size() < 2 || numbers.size() != 2 + size_t(numbers[1]) * 5)
    {
        handler->SendSysMessage("Gambits not saved: the count does not match the number of values");
        handler->SetSentErrorMessage(true);
        return false;
    }

    std::vector<GambitSlot> slots;
    for (uint32 i = 0; i < numbers[1]; ++i)
    {
        const uint32* v = &numbers[2 + i * 5];
        if (v[0] > 1)
        {
            handler->SendSysMessage("Gambits not saved: enabled must be 0 or 1");
            handler->SetSentErrorMessage(true);
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

    std::string error;
    if (!sGambitMgr.Save(guidLow, slots, error))
    {
        handler->PSendSysMessage("Gambits not saved: %s", error.c_str());
        handler->SetSentErrorMessage(true);
        return false;
    }

    handler->PSendSysMessage("Saved %u gambits for %u", uint32(slots.size()), guidLow);
    return true;
}
