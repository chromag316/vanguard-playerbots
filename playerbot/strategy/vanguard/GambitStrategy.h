#pragma once
#include "playerbot/strategy/Strategy.h"

namespace ai
{
    // "gambits": the companion's gambit slots, ranked above every class rotation (90.5 for slot
    // 12 up to 96 for slot 1, below ACTION_PASSTROUGH). A slot's trigger reads the slot from
    // GambitMgr on every check, so saved changes apply at once. When no gambit applies, the
    // bot's normal strategies decide.
    class GambitStrategy : public Strategy
    {
    public:
        GambitStrategy(PlayerbotAI* ai) : Strategy(ai) {}
        std::string getName() override { return "gambits"; }

#ifdef GenerateBotHelp
        virtual std::string GetHelpName() { return "gambits"; } //Must equal iternal name
        virtual std::string GetHelpDescription() { return "This strategy runs the gambits the player set for this companion on the Vanguard website."; }
#endif

    private:
        void InitCombatTriggers(std::list<TriggerNode*>& triggers) override;
        void InitNonCombatTriggers(std::list<TriggerNode*>& triggers) override;
    };
}
