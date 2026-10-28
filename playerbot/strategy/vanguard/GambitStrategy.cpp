#include "playerbot/playerbot.h"
#include "GambitStrategy.h"
#include "GambitMgr.h"

using namespace ai;

static void AddGambitTriggers(std::list<TriggerNode*>& triggers)
{
    for (uint8 slot = 1; slot <= GAMBIT_MAX_SLOTS; ++slot)
    {
        std::string qualifier = std::to_string(slot);
        float relevance = 96.5f - 0.5f * slot;
        triggers.push_back(new TriggerNode("gambit::" + qualifier,
            NextAction::array(0, new NextAction("gambit action::" + qualifier, relevance), NULL)));
    }

    // Passives rank just below the last gambit (90.0, 89.5, 89.0): a gambit that applies, such
    // as a heal, goes first; the fallback AI casts no spells for a companion at all
    for (uint8 slot = 1; slot <= GAMBIT_MAX_PASSIVES; ++slot)
    {
        std::string qualifier = std::to_string(slot);
        float relevance = 90.5f - 0.5f * slot;
        triggers.push_back(new TriggerNode("gambit passive::" + qualifier,
            NextAction::array(0, new NextAction("gambit passive action::" + qualifier, relevance), NULL)));
    }
}

void GambitStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    AddGambitTriggers(triggers);
}

void GambitStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    AddGambitTriggers(triggers);
}
