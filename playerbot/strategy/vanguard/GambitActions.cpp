#include "playerbot/playerbot.h"
#include "GambitActions.h"
#include "GambitTriggers.h"

#include "playerbot/ServerFacade.h"
#include "Spells/SpellMgr.h"

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

// Outside this distance (or out of sight) a Cast gambit first moves the companion closer
static float CastRange(const SpellEntry* spell)
{
    return GetSpellMaxRange(sSpellRangeStore.LookupEntry(spell->rangeIndex));
}

static bool OutOfReach(Player* bot, Unit* target, const SpellEntry* spell)
{
    if (target == bot)
        return false;
    float range = CastRange(spell);
    return (range > 0.0f && sServerFacade.GetDistance(bot, target) > range) || !bot->IsWithinLOSInMap(target);
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
    case GAMBIT_ACTION_CAST_RANK:
    {
        uint32 spellId = GambitSpellId(ai, gambit);
        const SpellEntry* spell = spellId ? sServerFacade.LookupSpellInfo(spellId) : nullptr;
        if (!spell)
            return false;
        // Auto Shot and wands keep firing on their own once started
        if (IsAutoRepeatRangedSpell(spell))
        {
            Spell* current = bot->GetCurrentSpell(CURRENT_AUTOREPEAT_SPELL);
            if (current && current->m_spellInfo->Id == spellId && bot->GetSelectionGuid() == target->GetObjectGuid())
                return false;
        }
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
    case GAMBIT_ACTION_CAST_RANK:
    {
        // Range is handled in Execute: out of reach, the companion moves closer first. GambitSpellId
        // already checked the bot may cast it (a lower rank of a known spell is not in its list).
        uint32 spellId = GambitSpellId(ai, gambit);
        return spellId && ai->CanCastSpell(spellId, target, 0, false, nullptr, true);
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
    case GAMBIT_ACTION_CAST_RANK:
    {
        uint32 spellId = GambitSpellId(ai, gambit);
        const SpellEntry* spell = spellId ? sServerFacade.LookupSpellInfo(spellId) : nullptr;
        if (!spell)
            return false;
        if (OutOfReach(bot, target, spell))
            return MoveNear(target, std::max(CastRange(spell) - 2.0f, sPlayerbotAIConfig.contactDistance));
        return ai->CastSpell(spellId, target);
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
    ai->TellPlayer(requester, sGambitMgr.IsCustom(bot->GetGUIDLow()) ? "=== My gambits ===" : "=== My gambits (class default) ===");
    if (slots.empty())
        ai->TellPlayer(requester, "None: I will only follow you.");
    for (const GambitSlot& gambit : slots)
        ai->TellPlayer(requester, sGambitMgr.Describe(gambit));
    return true;
}
