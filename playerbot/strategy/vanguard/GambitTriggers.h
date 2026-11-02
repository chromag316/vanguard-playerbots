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

    // "gambit retreat over": a Retreat order ("flee" adds "passive") is in force, and nobody in
    // the party is in combat any more
    class GambitRetreatOverTrigger : public Trigger
    {
    public:
        GambitRetreatOverTrigger(PlayerbotAI* ai) : Trigger(ai, "gambit retreat over") {}
        bool IsActive() override;
    };

    // The spell a "Cast spell" gambit casts: the bot's highest known rank of the stored spell;
    // for "Cast spell (exact rank)", the stored rank itself. 0 when the bot can't cast it.
    uint32 GambitSpellId(PlayerbotAI* ai, const GambitSlot& gambit);

    Unit* FindGambitTarget(PlayerbotAI* ai, const GambitSlot& gambit);
}
