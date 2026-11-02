#pragma once
#include "playerbot/strategy/actions/AttackAction.h"
#include "playerbot/strategy/actions/GenericActions.h"
#include "GambitMgr.h"

namespace ai
{
    // "gambit action::<slot>": does what gambit slot <slot> says, on the target its trigger found.
    // Not useful or not possible means the engine tries the next gambit, as in FF XII.
    class GambitAction : public AttackAction, public Qualified
    {
    public:
        GambitAction(PlayerbotAI* ai) : AttackAction(ai, "gambit action"), Qualified() {}
        std::string getName() override { return "gambit action::" + qualifier; }
        bool Execute(Event& event) override;
        bool isPossible() override;
        bool isUseful() override;

    private:
        bool Current(GambitSlot& gambit, Unit*& target);
    };

    // "gambit end retreat": the fight is over, so a Retreat order becomes a plain follow again
    class GambitEndRetreatAction : public Action
    {
    public:
        GambitEndRetreatAction(PlayerbotAI* ai) : Action(ai, "gambit end retreat") {}
        bool Execute(Event& event) override;
    };

    // "gambits" (whisper): the companion lists its gambits
    class GambitListAction : public ChatCommandAction
    {
    public:
        GambitListAction(PlayerbotAI* ai) : ChatCommandAction(ai, "gambits") {}
        bool Execute(Event& event) override;
        bool isUsefulWhenStunned() override { return true; }
    };
}
