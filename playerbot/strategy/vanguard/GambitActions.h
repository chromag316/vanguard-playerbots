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

    // "gambit passive action::<slot>": casts passive <slot> (an aspect, stance, aura...) on the bot
    class GambitPassiveAction : public Action, public Qualified
    {
    public:
        GambitPassiveAction(PlayerbotAI* ai) : Action(ai, "gambit passive action"), Qualified() {}
        std::string getName() override { return "gambit passive action::" + qualifier; }
        bool Execute(Event& event) override;
        bool isPossible() override;
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
