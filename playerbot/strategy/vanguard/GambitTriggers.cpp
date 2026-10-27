#include "playerbot/playerbot.h"
#include "GambitTriggers.h"

#include "playerbot/ServerFacade.h"

using namespace ai;

uint32 ai::GambitSpellId(PlayerbotAI* ai, const GambitSlot& gambit)
{
    if (gambit.actionId != GAMBIT_ACTION_CAST)
        return 0;

    const SpellEntry* spell = sServerFacade.LookupSpellInfo(gambit.actionParam);
    if (!spell)
        return 0;

    return ai->GetAiObjectContext()->GetValue<uint32>("spell id", spell->SpellName[0])->Get();
}

static float HealthPct(Unit* unit)
{
    return unit->GetMaxHealth() ? 100.0f * unit->GetHealth() / unit->GetMaxHealth() : 100.0f;
}

static bool CanDispel(PlayerbotAI* ai, uint32 spellId, Unit* target)
{
    const SpellEntry* spell = sServerFacade.LookupSpellInfo(spellId);
    if (!spell)
        return false;

    for (int i = 0; i < MAX_EFFECT_INDEX; ++i)
    {
        if (spell->Effect[i] == SPELL_EFFECT_DISPEL && ai->HasAuraToDispel(target, spell->EffectMiscValue[i]))
            return true;
    }
    return false;
}

// Group members on the bot's map and within sight, the player included; the bot alone when it
// has no group.
static std::vector<Unit*> Allies(PlayerbotAI* ai)
{
    Player* bot = ai->GetBot();
    std::vector<Unit*> allies;
    for (ObjectGuid guid : ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("group members")->Get())
    {
        Unit* unit = ai->GetUnit(guid);
        if (unit && unit->IsInWorld() && unit->GetMapId() == bot->GetMapId() &&
            sServerFacade.GetDistance(bot, unit) <= sPlayerbotAIConfig.sightDistance)
            allies.push_back(unit);
    }
    return allies;
}

// Living enemies in combat with the group, nearest first
static std::vector<Unit*> Foes(PlayerbotAI* ai)
{
    Player* bot = ai->GetBot();
    std::vector<Unit*> foes;
    for (ObjectGuid guid : ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("attackers")->Get())
    {
        Unit* unit = ai->GetUnit(guid);
        if (unit && unit->IsAlive() && bot->CanAttack(unit))
            foes.push_back(unit);
    }
    std::sort(foes.begin(), foes.end(), [bot](Unit* a, Unit* b) { return bot->GetDistance(a) < bot->GetDistance(b); });
    return foes;
}

static bool IsAlly(const std::vector<Unit*>& allies, Unit* unit)
{
    return std::find(allies.begin(), allies.end(), unit) != allies.end();
}

Unit* ai::FindGambitTarget(PlayerbotAI* ai, const GambitSlot& gambit)
{
    Player* bot = ai->GetBot();
    uint32 x = gambit.conditionParam;

    switch (gambit.conditionId)
    {
    case GAMBIT_COND_SELF_ANY:
        return bot;
    case GAMBIT_COND_SELF_HP_BELOW:
        return HealthPct(bot) < x ? bot : nullptr;
    case GAMBIT_COND_SELF_MANA_BELOW:
        if (bot->GetPowerType() != POWER_MANA || !bot->GetMaxPower(POWER_MANA))
            return nullptr;
        return 100.0f * bot->GetPower(POWER_MANA) / bot->GetMaxPower(POWER_MANA) < x ? bot : nullptr;

    case GAMBIT_COND_ALLY_ANY:
    case GAMBIT_COND_ALLY_HP_BELOW:
    {
        Unit* lowest = nullptr;
        for (Unit* ally : Allies(ai))
        {
            if (ally->IsAlive() && (!lowest || HealthPct(ally) < HealthPct(lowest)))
                lowest = ally;
        }
        if (lowest && gambit.conditionId == GAMBIT_COND_ALLY_HP_BELOW && HealthPct(lowest) >= x)
            return nullptr;
        return lowest;
    }
    case GAMBIT_COND_ALLY_DEAD:
        for (Unit* ally : Allies(ai))
        {
            if (!ally->IsAlive())
                return ally;
        }
        return nullptr;
    case GAMBIT_COND_ALLY_DISPELLABLE:
    {
        uint32 spellId = GambitSpellId(ai, gambit);
        if (!spellId)
            return nullptr;
        for (Unit* ally : Allies(ai))
        {
            if (ally->IsAlive() && CanDispel(ai, spellId, ally))
                return ally;
        }
        return nullptr;
    }

    case GAMBIT_COND_FOE_LEADER_TARGET:
    {
        Player* leader = ai->GetGroupMaster();
        if (!leader || leader == bot)
            return nullptr;
        Unit* target = ai->GetUnit(leader->GetSelectionGuid());
        return target && target->IsAlive() && bot->CanAttack(target) ? target : nullptr;
    }
    case GAMBIT_COND_FOE_TARGETING_SELF:
        for (Unit* foe : Foes(ai))
        {
            if (foe->GetVictim() == bot)
                return foe;
        }
        return nullptr;
    case GAMBIT_COND_FOE_TARGETING_ALLY:
    {
        std::vector<Unit*> allies = Allies(ai);
        for (Unit* foe : Foes(ai))
        {
            Unit* victim = foe->GetVictim();
            if (victim && victim != bot && IsAlly(allies, victim))
                return foe;
        }
        return nullptr;
    }
    case GAMBIT_COND_FOE_CASTING:
        for (Unit* foe : Foes(ai))
        {
            if (foe->IsNonMeleeSpellCasted(false))
                return foe;
        }
        return nullptr;
    case GAMBIT_COND_FOE_NEAREST:
    {
        std::vector<Unit*> foes = Foes(ai);
        return foes.empty() ? nullptr : foes.front();
    }
    case GAMBIT_COND_FOE_HP_BELOW:
    case GAMBIT_COND_FOE_HP_ABOVE:
        for (Unit* foe : Foes(ai))
        {
            float pct = HealthPct(foe);
            if (gambit.conditionId == GAMBIT_COND_FOE_HP_BELOW ? pct < x : pct > x)
                return foe;
        }
        return nullptr;
    case GAMBIT_COND_FOE_WITHOUT_AURA:
    {
        uint32 spellId = GambitSpellId(ai, gambit);
        if (!spellId)
            return nullptr;
        for (Unit* foe : Foes(ai))
        {
            if (!ai->HasAura(spellId, foe, true))
                return foe;
        }
        return nullptr;
    }
    default:
        return nullptr;
    }
}

bool GambitTrigger::IsActive()
{
    uint8 slot = uint8(atoi(getQualifier().c_str()));
    GambitSlot gambit;
    Unit* target = nullptr;
    if (sGambitMgr.GetSlot(bot->GetGUIDLow(), slot, gambit) && gambit.enabled)
        target = FindGambitTarget(ai, gambit);

    SET_AI_VALUE2(ObjectGuid, "gambit target", getQualifier(), target ? target->GetObjectGuid() : ObjectGuid());
    return target != nullptr;
}
