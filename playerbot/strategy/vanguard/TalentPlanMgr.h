#pragma once

// Vanguard talent plans: the player decides which talents a companion takes and in what order
// (the web app's talent calculator). A plan is a list of up to 51 steps; each step is one talent
// point in a Talent.dbc talent, and a talent named n times has rank n. The saved plan is always
// the truth: a companion with a plan learns its first CalculateTalentsPoints() steps, and talents
// that are not in them are reset (for free). A companion without a plan picks its talents the
// normal playerbots way. Stored in the characters DB (vanguard_talent_plan).

#include <map>
#include <mutex>
#include <string>
#include <vector>

class ChatHandler;
class Player;

namespace ai
{
    constexpr uint8 TALENT_PLAN_MAX_STEPS = 51;

    class TalentPlanMgr
    {
    public:
        static TalentPlanMgr& instance()
        {
            static TalentPlanMgr instance;
            return instance;
        }

        // The companion's plan: talent ids in order, empty when it has none
        std::vector<uint32> GetPlan(uint32 guidLow);
        bool HasPlan(uint32 guidLow) { return !GetPlan(guidLow).empty(); }

        // Replace the plan or drop it. On a validation failure nothing is written and error says
        // why, starting with "step N:" when one step is at fault.
        bool Save(uint32 guidLow, const std::vector<uint32>& steps, std::string& error);
        bool Reset(uint32 guidLow, std::string& error);

        // Make the bot's talents match its plan for its level. False when it has no plan.
        bool Apply(Player* bot);

        // ".talentplan list|save|reset" (mangosd console, SOAP)
        static bool HandleConsoleCommand(ChatHandler* handler, char const* args);

    private:
        bool Validate(uint8 classId, const std::vector<uint32>& steps, std::string& error) const;
        std::vector<uint32>& Cached(uint32 guidLow); // cacheLock held
        void ApplyIfCompanion(uint32 guidLow);

        std::mutex cacheLock;
        std::map<uint32, std::vector<uint32>> cache;
    };
}

#define sTalentPlanMgr ai::TalentPlanMgr::instance()
