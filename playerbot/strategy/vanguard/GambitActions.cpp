#include "playerbot/playerbot.h"
#include "GambitActions.h"
#include "GambitTriggers.h"

#include "playerbot/ServerFacade.h"

using namespace ai;

static bool AppliesAura(const SpellEntry* spell)
{
    for (int i = 0; i < MAX_EFFECT_INDEX; ++i)
    {
        if (spell->Effect[i] == SPELL_EFFECT_APPLY_AURA)
            return true;
    }
    return false;
}

bool GambitAction::Current(GambitSlot& gambit, Unit*& target)
{
    uint8 slot = uint8(atoi(getQualifier().c_str()));
    if (!sGambitMgr.GetSlot(bot->GetGUIDLow(), slot, gambit) || !gambit.enabled)
        return false;

    target = ai->GetUnit(AI_VALUE2(ObjectGuid, "gambit target", getQualifier()));
    return target != nullptr;
}

bool GambitAction::isUseful()
{
    GambitSlot gambit;
    Unit* target = nullptr;
    if (!Current(gambit, target))
        return false;

    switch (gambit.actionId)
    {
    case GAMBIT_ACTION_CAST:
    {
        uint32 spellId = GambitSpellId(ai, gambit);
        const SpellEntry* spell = spellId ? sServerFacade.LookupSpellInfo(spellId) : nullptr;
        if (!spell)
            return false;
        // Don't refresh an aura that is still on: a buff from anyone, a debuff from this bot
        if (AppliesAura(spell))
        {
            bool hostile = bot->CanAttack(target);
            if (hostile ? ai->HasAura(spellId, target, true) : ai->HasAura(spell->SpellName[0], target))
                return false;
        }
        return true;
    }
    case GAMBIT_ACTION_ATTACK:
        return bot->GetVictim() != target;
    case GAMBIT_ACTION_MOVE_TO_LEADER:
    {
        Player* leader = ai->GetGroupMaster();
        return leader && leader != bot && leader->GetMapId() == bot->GetMapId() &&
            sServerFacade.GetDistance(bot, leader) > sPlayerbotAIConfig.followDistance + 5.0f;
    }
    default:
        return false;
    }
}

bool GambitAction::isPossible()
{
    GambitSlot gambit;
    Unit* target = nullptr;
    if (!Current(gambit, target))
        return false;

    switch (gambit.actionId)
    {
    case GAMBIT_ACTION_CAST:
    {
        uint32 spellId = GambitSpellId(ai, gambit);
        return spellId && ai->CanCastSpell(spellId, target, 0, true);
    }
    case GAMBIT_ACTION_ATTACK:
        return target->IsAlive() && AttackAction::isPossible();
    case GAMBIT_ACTION_MOVE_TO_LEADER:
        return MovementAction::isPossible();
    default:
        return false;
    }
}

bool GambitAction::Execute(Event& event)
{
    GambitSlot gambit;
    Unit* target = nullptr;
    if (!Current(gambit, target))
        return false;

    switch (gambit.actionId)
    {
    case GAMBIT_ACTION_CAST:
    {
        uint32 spellId = GambitSpellId(ai, gambit);
        return spellId && ai->CastSpell(spellId, target);
    }
    case GAMBIT_ACTION_ATTACK:
        return Attack(GetMaster(), target);
    case GAMBIT_ACTION_MOVE_TO_LEADER:
    {
        Player* leader = ai->GetGroupMaster();
        return leader && MoveNear(leader, sPlayerbotAIConfig.followDistance);
    }
    default:
        return false;
    }
}

bool GambitListAction::Execute(Event& event)
{
    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();
    std::vector<GambitSlot> slots = sGambitMgr.GetSlots(bot->GetGUIDLow());
    if (slots.empty())
    {
        ai->TellPlayer(requester, "I have no gambits.");
        return true;
    }

    ai->TellPlayer(requester, "=== Gambits ===");
    for (const GambitSlot& gambit : slots)
        ai->TellPlayer(requester, sGambitMgr.Describe(gambit));
    return true;
}
