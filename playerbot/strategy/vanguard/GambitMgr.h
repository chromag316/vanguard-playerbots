#pragma once

// Vanguard gambits: player-programmed tactics for a player's own companion bots, in the style of
// Final Fantasy XII. A gambit is one slot, "condition -> action", where the condition also names
// the target (Self, Ally, Foe). Lower slots win. The catalog of conditions and actions lives in
// the world DB (vanguard_gambit_condition / vanguard_gambit_action); the slots of each companion
// in the characters DB (vanguard_gambit). The ids below are fixed: the DB rows only name them.

#include <map>
#include <mutex>
#include <string>
#include <vector>

class ChatHandler;

namespace ai
{
    enum GambitConditionId : uint32
    {
        GAMBIT_COND_SELF_ANY            = 1,
        GAMBIT_COND_SELF_HP_BELOW       = 2,
        GAMBIT_COND_SELF_MANA_BELOW     = 3,
        GAMBIT_COND_ALLY_ANY            = 10,
        GAMBIT_COND_ALLY_HP_BELOW       = 11,
        GAMBIT_COND_ALLY_DEAD           = 12,
        GAMBIT_COND_ALLY_DISPELLABLE    = 13,
        GAMBIT_COND_FOE_LEADER_TARGET   = 20,
        GAMBIT_COND_FOE_TARGETING_SELF  = 21,
        GAMBIT_COND_FOE_TARGETING_ALLY  = 22,
        GAMBIT_COND_FOE_CASTING         = 23,
        GAMBIT_COND_FOE_NEAREST         = 24,
        GAMBIT_COND_FOE_HP_BELOW        = 25,
        GAMBIT_COND_FOE_HP_ABOVE        = 26,
        GAMBIT_COND_FOE_WITHOUT_AURA    = 27,
    };

    enum GambitActionId : uint32
    {
        GAMBIT_ACTION_CAST              = 1,
        GAMBIT_ACTION_ATTACK            = 2,
        GAMBIT_ACTION_MOVE_TO_LEADER    = 3,
    };

    constexpr uint8 GAMBIT_MAX_SLOTS = 12;

    struct GambitSlot
    {
        uint8 slot = 0;
        bool enabled = false;
        uint32 conditionId = 0;
        uint32 conditionParam = 0;
        uint32 actionId = 0;
        uint32 actionParam = 0;
    };

    struct GambitConditionInfo
    {
        std::string name;
        uint32 paramMin = 0;
        uint32 paramMax = 0;
    };

    struct GambitActionInfo
    {
        std::string name;
        bool needsSpell = false;
    };

    class GambitMgr
    {
    public:
        static GambitMgr& instance()
        {
            static GambitMgr instance;
            return instance;
        }

        // World DB catalog, once at startup
        void LoadCatalog();

        // The slot of a companion, from the cache (loaded from the DB on first use).
        // False when the slot is empty.
        bool GetSlot(uint32 guidLow, uint8 slot, GambitSlot& out);
        std::vector<GambitSlot> GetSlots(uint32 guidLow);

        // Replaces all slots of a companion. Slots are numbered by their order (1..n).
        // On a validation failure nothing is written and error says why.
        bool Save(uint32 guidLow, const std::vector<GambitSlot>& slots, std::string& error);

        std::string Describe(const GambitSlot& gambit) const;

        // ".gambit save|list" (mangosd console, SOAP)
        static bool HandleConsoleCommand(ChatHandler* handler, char const* args);

    private:
        bool Validate(uint32 guidLow, const std::vector<GambitSlot>& slots, std::string& error) const;
        std::vector<GambitSlot> LoadSlots(uint32 guidLow) const;

        std::map<uint32, GambitConditionInfo> conditions;
        std::map<uint32, GambitActionInfo> actions;

        std::mutex cacheLock;
        std::map<uint32, std::vector<GambitSlot>> cache;
    };
}

#define sGambitMgr ai::GambitMgr::instance()
