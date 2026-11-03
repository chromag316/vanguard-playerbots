#include "playerbot/playerbot.h"
#include "TalentPlanMgr.h"

#include "Chat/Chat.h"
#include "Database/DatabaseEnv.h"
#include "playerbot/ServerFacade.h"
#include "Server/DBCStructure.h"

using namespace ai;

static std::string TalentName(const TalentEntry* talent)
{
    const SpellEntry* spell = sServerFacade.LookupSpellInfo(talent->RankID[0]);
    return spell ? spell->SpellName[0] : "talent " + std::to_string(talent->TalentID);
}

static uint32 KnownRank(Player* bot, const TalentEntry* talent)
{
    for (int rank = MAX_TALENT_RANK - 1; rank >= 0; --rank)
    {
        if (talent->RankID[rank] && bot->HasSpell(talent->RankID[rank]))
            return uint32(rank + 1);
    }
    return 0;
}

std::vector<uint32>& TalentPlanMgr::Cached(uint32 guidLow)
{
    auto it = cache.find(guidLow);
    if (it != cache.end())
        return it->second;

    std::vector<uint32> steps;
    if (auto result = CharacterDatabase.PQuery("SELECT `step`, `talent_id` FROM `vanguard_talent_plan` WHERE `guid` = '%u' ORDER BY `step`", guidLow))
    {
        do
        {
            Field* fields = result->Fetch();
            uint32 step = fields[0].GetUInt8();
            if (step >= 1 && step <= TALENT_PLAN_MAX_STEPS)
                steps.push_back(fields[1].GetUInt32());
        } while (result->NextRow());
    }
    return cache.emplace(guidLow, steps).first->second;
}

std::vector<uint32> TalentPlanMgr::GetPlan(uint32 guidLow)
{
    std::lock_guard<std::mutex> guard(cacheLock);
    return Cached(guidLow);
}

// Walks the plan from no talents, as the talent frame would: the class's trees only, no rank past
// the talent's last, 5 points in a tree per row above, and the required talent first.
bool TalentPlanMgr::Validate(uint8 classId, const std::vector<uint32>& steps, std::string& error) const
{
    if (steps.size() > TALENT_PLAN_MAX_STEPS)
    {
        error = "at most 51 talent points";
        return false;
    }

    if (!classId)
    {
        error = "no character with that guid";
        return false;
    }

    uint32 classMask = 1 << (classId - 1);
    std::map<uint32, uint32> ranks;
    std::map<uint32, uint32> treePoints;

    for (size_t i = 0; i < steps.size(); ++i)
    {
        std::string where = "step " + std::to_string(i + 1) + ": ";

        const TalentEntry* talent = sTalentStore.LookupEntry(steps[i]);
        const TalentTabEntry* tree = talent ? sTalentTabStore.LookupEntry(talent->TalentTab) : nullptr;
        if (!talent || !tree)
        {
            error = where + "unknown talent " + std::to_string(steps[i]);
            return false;
        }
        if (!(tree->ClassMask & classMask))
        {
            error = where + TalentName(talent) + " is not a talent of this class";
            return false;
        }

        uint32 rank = ranks[talent->TalentID];
        if (rank >= MAX_TALENT_RANK || !talent->RankID[rank])
        {
            error = where + TalentName(talent) + " is already at its highest rank";
            return false;
        }

        if (treePoints[talent->TalentTab] < talent->Row * 5)
        {
            error = where + TalentName(talent) + " needs " + std::to_string(talent->Row * 5) + " points in its tree first";
            return false;
        }

        if (talent->DependsOn)
        {
            const TalentEntry* required = sTalentStore.LookupEntry(talent->DependsOn);
            if (required && ranks[required->TalentID] < talent->DependsOnRank + 1)
            {
                error = where + TalentName(talent) + " needs " + TalentName(required) + " rank " + std::to_string(talent->DependsOnRank + 1) + " first";
                return false;
            }
        }

        ++ranks[talent->TalentID];
        ++treePoints[talent->TalentTab];
    }

    return true;
}

bool TalentPlanMgr::Save(uint32 guidLow, const std::vector<uint32>& steps, std::string& error)
{
    uint8 classId = 0;
    if (auto result = CharacterDatabase.PQuery("SELECT `class` FROM `characters` WHERE `guid` = '%u'", guidLow))
        classId = result->Fetch()[0].GetUInt8();

    if (!Validate(classId, steps, error))
        return false;

    // All values are validated numbers
    CharacterDatabase.BeginTransaction();
    CharacterDatabase.PExecute("DELETE FROM `vanguard_talent_plan` WHERE `guid` = '%u'", guidLow);
    for (size_t i = 0; i < steps.size(); ++i)
    {
        CharacterDatabase.PExecute("INSERT INTO `vanguard_talent_plan` (`guid`, `step`, `talent_id`) VALUES ('%u', '%u', '%u')",
            guidLow, uint32(i + 1), steps[i]);
    }
    CharacterDatabase.CommitTransaction();

    {
        std::lock_guard<std::mutex> guard(cacheLock);
        Cached(guidLow) = steps;
    }
    ApplyIfCompanion(guidLow);
    return true;
}

bool TalentPlanMgr::Reset(uint32 guidLow, std::string& error)
{
    if (!CharacterDatabase.PQuery("SELECT 1 FROM `characters` WHERE `guid` = '%u'", guidLow))
    {
        error = "no character with that guid";
        return false;
    }

    CharacterDatabase.PExecute("DELETE FROM `vanguard_talent_plan` WHERE `guid` = '%u'", guidLow);

    std::lock_guard<std::mutex> guard(cacheLock);
    Cached(guidLow).clear();
    return true;
}

bool TalentPlanMgr::Apply(Player* bot)
{
    std::vector<uint32> steps = GetPlan(bot->GetGUIDLow());
    if (steps.empty())
        return false;

    size_t count = std::min(steps.size(), size_t(bot->CalculateTalentsPoints()));
    std::map<uint32, uint32> target;
    for (size_t i = 0; i < count; ++i)
        ++target[steps[i]];

    // A talent above its planned rank, or not in the plan: start over from no talents
    bool reset = false;
    uint32 classMask = bot->getClassMask();
    for (uint32 i = 0; i < sTalentStore.GetNumRows() && !reset; ++i)
    {
        const TalentEntry* talent = sTalentStore.LookupEntry(i);
        const TalentTabEntry* tree = talent ? sTalentTabStore.LookupEntry(talent->TalentTab) : nullptr;
        if (tree && (tree->ClassMask & classMask) && KnownRank(bot, talent) > target[talent->TalentID])
            reset = true;
    }
    if (reset)
        bot->resetTalents(true);

    // In plan order; LearnTalent skips ranks already known
    std::map<uint32, uint32> ranks;
    for (size_t i = 0; i < count; ++i)
        bot->LearnTalent(steps[i], ranks[steps[i]]++);

    if (PlayerbotAI* ai = bot->GetPlayerbotAI())
        ai->UpdateTalentSpec();
    return true;
}

void TalentPlanMgr::ApplyIfCompanion(uint32 guidLow)
{
    Player* bot = sObjectMgr.GetPlayer(ObjectGuid(HIGHGUID_PLAYER, guidLow));
    if (bot && bot->GetPlayerbotAI() && bot->GetPlayerbotAI()->IsCompanion())
        Apply(bot);
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

// A refused save or reset still succeeds as a command: mangosd's SOAP answers a failed command
// with an empty reply, and the web app needs the "not saved" reason to show it.
bool TalentPlanMgr::HandleConsoleCommand(ChatHandler* handler, char const* args)
{
    std::istringstream in(args ? args : "");
    std::string sub;
    in >> sub;
    std::string rest;
    std::getline(in, rest);

    std::vector<uint32> numbers;
    bool known = sub == "list" || sub == "save" || sub == "reset";
    bool argsOk = ParseNumbers(rest, numbers) && (sub == "save" ? numbers.size() >= 2 : numbers.size() == 1);
    if (!known || !argsOk)
    {
        handler->SendSysMessage("Usage: .talentplan list|reset <guid>");
        handler->SendSysMessage("       .talentplan save <guid> <talent id> [<talent id> ...] (one id per talent point, in order)");
        return false;
    }

    uint32 guidLow = numbers[0];
    std::string error;

    if (sub == "list")
    {
        std::vector<uint32> steps = sTalentPlanMgr.GetPlan(guidLow);
        handler->PSendSysMessage("Talent plan of %u: %u points", guidLow, uint32(steps.size()));
        std::map<uint32, uint32> ranks;
        for (size_t i = 0; i < steps.size(); ++i)
        {
            const TalentEntry* talent = sTalentStore.LookupEntry(steps[i]);
            std::string name = talent ? TalentName(talent) : "talent " + std::to_string(steps[i]);
            handler->PSendSysMessage("%u. %s %u", uint32(i + 1), name.c_str(), ++ranks[steps[i]]);
        }
        return true;
    }

    if (sub == "reset")
    {
        if (!sTalentPlanMgr.Reset(guidLow, error))
        {
            handler->PSendSysMessage("Talent plan not reset: %s", error.c_str());
            return true;
        }
        handler->PSendSysMessage("%u picks its talents automatically again", guidLow);
        return true;
    }

    std::vector<uint32> steps(numbers.begin() + 1, numbers.end());
    if (!sTalentPlanMgr.Save(guidLow, steps, error))
    {
        handler->PSendSysMessage("Talent plan not saved: %s", error.c_str());
        return true;
    }
    handler->PSendSysMessage("Saved a talent plan of %u points for %u", uint32(steps.size()), guidLow);
    return true;
}
