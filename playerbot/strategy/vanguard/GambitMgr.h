#pragma once

// Vanguard gambits: player-programmed tactics for a player's own companion bots, in the style of
// Final Fantasy XII. A gambit is one slot, "condition -> action", where the condition also names
// the target (Self, Ally, Foe). Lower slots win. A companion runs its gambits first and falls back to the normal AI (see
// PlayerbotAI::IsCompanion and ResetStrategies).
// The catalog of conditions and actions lives in the world DB (vanguard_gambit_condition /
// vanguard_gambit_action). In the characters DB: each class's default list
// (vanguard_gambit_class_default), and a companion's own list (vanguard_gambit), used only when
// the companion is in vanguard_gambit_custom. The ids below are fixed: the DB rows only name them.

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
        GAMBIT_COND_ALLY_WITHOUT_AURA   = 14,
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
        GAMBIT_ACTION_EAT               = 4,
        GAMBIT_ACTION_DRINK             = 5,
    };

    constexpr uint8 GAMBIT_MAX_SLOTS = 12;

    inline bool IsSelfGambitCondition(uint32 conditionId) { return conditionId >= GAMBIT_COND_SELF_ANY && conditionId <= GAMBIT_COND_SELF_MANA_BELOW; }

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
        bool selfOnly = false;
    };

    class GambitMgr
    {
    public:
        static GambitMgr& instance()
        {
            static GambitMgr instance;
            return instance;
        }

        // World DB catalog and the class defaults, once at startup
        void LoadCatalog();

        // The companion's gambits in effect: its own list when it has one, else its class default
        bool GetSlot(uint32 guidLow, uint8 slot, GambitSlot& out);
        std::vector<GambitSlot> GetSlots(uint32 guidLow);
        bool IsCustom(uint32 guidLow);
        std::vector<GambitSlot> GetClassDefault(uint8 classId);

        // Replace a companion's own list (it becomes custom), drop it again, or replace a class
        // default. Slots are numbered by their order (1..n). On a validation failure nothing is
        // written and error says why.
        bool Save(uint32 guidLow, const std::vector<GambitSlot>& slots, std::string& error);
        bool Reset(uint32 guidLow, std::string& error);
        bool SaveClassDefault(uint8 classId, const std::vector<GambitSlot>& slots, std::string& error);

        std::string Describe(const GambitSlot& gambit) const;

        // ".gambit list|save|reset|default list|default save" (mangosd console, SOAP)
        static bool HandleConsoleCommand(ChatHandler* handler, char const* args);

    private:
        struct Companion
        {
            uint8 classId = 0;
            bool custom = false;
            std::vector<GambitSlot> slots;
        };

        // guidLow 0: a class default (spells are only checked to exist)
        bool Validate(uint32 guidLow, const std::vector<GambitSlot>& slots, std::string& error) const;
        Companion& Cached(uint32 guidLow); // cacheLock held
        const std::vector<GambitSlot>& Effective(Companion& companion);

        std::map<uint32, GambitConditionInfo> conditions;
        std::map<uint32, GambitActionInfo> actions;

        std::mutex cacheLock;
        std::map<uint32, Companion> cache;
        std::map<uint8, std::vector<GambitSlot>> classDefaults;
    };
}

#define sGambitMgr ai::GambitMgr::instance()
