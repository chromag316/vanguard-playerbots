#pragma once
#include "playerbot/strategy/Trigger.h"
#include "playerbot/strategy/Value.h"
#include "GambitMgr.h"

namespace ai
{
    // The unit that gambit slot <qualifier> matched this tick, for its action
    class GambitTargetValue : public UnitManualSetValue, public Qualified
    {
    public:
        GambitTargetValue(PlayerbotAI* ai) : UnitManualSetValue(ai, ObjectGuid(), "gambit target"), Qualified() {}
    };

    // "gambit::<slot>": active when the slot is on and its condition finds a target
    class GambitTrigger : public Trigger, public Qualified
    {
    public:
        GambitTrigger(PlayerbotAI* ai) : Trigger(ai, "gambit"), Qualified() {}
        std::string getName() override { return "gambit::" + qualifier; }
        bool IsActive() override;
    };

    // "gambit passive::<slot>": active when passive <slot> is a spell the bot knows and its aura
    // is not on the bot
    class GambitPassiveTrigger : public Trigger, public Qualified
    {
    public:
        GambitPassiveTrigger(PlayerbotAI* ai) : Trigger(ai, "gambit passive"), Qualified() {}
        std::string getName() override { return "gambit passive::" + qualifier; }
        bool IsActive() override;
    };

    // The bot's highest known rank of a stored (rank 1) spell; 0 when it does not know it
    uint32 KnownSpellRank(PlayerbotAI* ai, uint32 spellId);

    // The spell a "Cast spell" gambit casts: the bot's highest known rank of the stored spell
    uint32 GambitSpellId(PlayerbotAI* ai, const GambitSlot& gambit);

    // The spell passive <slot> keeps up (highest known rank), or 0
    uint32 GambitPassiveSpellId(PlayerbotAI* ai, const std::string& slot);

    Unit* FindGambitTarget(PlayerbotAI* ai, const GambitSlot& gambit);
}
