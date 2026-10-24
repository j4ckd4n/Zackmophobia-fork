#include <windows.h>
#include <stdio.h>
#include <cmath>
#include <string>
#include "Hooks.h"
#include "../SDK/IL2CPP.h"
#include "../Features/Features.h"
#include "../Features/GhostControl.h"
#include "../Utils/Logger.h"

namespace {
    void ChangeGhostState(void* instance, int32_t state, void* interact, bool parameter, void* info) {
        const bool suspended = Hooks::BeginGhostTransition(instance);
        __try {
            Hooks::oGhostAI_ChangeState(instance, state, interact, parameter, info);
        }
        __finally {
            if (suspended) Hooks::EndGhostTransition();
        }
    }

    constexpr int32_t GHOST_STATE_IDLE = 0;
    constexpr int32_t GHOST_STATE_HUNTING = 2;
    constexpr int32_t GHOST_STATE_ROAM = 3;
    constexpr int32_t GHOST_STATE_LIGHT_SWITCH = 4; // photonInteract is used.
    constexpr int32_t GHOST_STATE_DOOR_INTERACT = 5; // photonInteract is used.
    constexpr int32_t GHOST_STATE_INTERACT_OBJECT = 6; // photonInteract is used.
    constexpr int32_t GHOST_STATE_LAUGHING = 14;

    typedef struct {
        int32_t state;
        void* photonInteract;
        const char* name;
    } Ghost_State;

    Ghost_State stateMap[] = {
        {0, nullptr, "Idle"},
        {1, nullptr, "Wander"},
        {2, nullptr, "Hunting"},
        {3, nullptr, "Favorite Room"},
        {4, nullptr, "Light Switch"},
        {5, nullptr, "Door Interact"},
        {6, nullptr, "Thrown Object"},
        {7, nullptr, "Fusebox Interaction"},
        {8, nullptr, "Appeared"},
        {9, nullptr, "Door Knock"},
        {10, nullptr, "Window Knock"},
        {11, nullptr, "Car Alarm"},
        {12, nullptr, "Flicker"},
        {13, nullptr, "CCTV"},
        {14, nullptr, "Random Event"},
        {15, nullptr, "Used Ghost Ability"},
        {16, nullptr, "Mannequin Interaction"},
        {17, nullptr, "Teleported Object"},
        {18, nullptr, "Interaction"},
        {19, nullptr, "At Summoning Circle"},
        {20, nullptr, "Following Music Box"},
        {21, nullptr, "Triggered DOTS"},
        {22, nullptr, "Interacted with salt"},
        {23, nullptr, "Ignite"},
    };

    inline bool IsFinite3(const Hooks::Vector3& v) {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    }

    // Reject obviously bogus vectors to avoid logging random memory as world position.
    inline bool LooksLikeWorldPos(const Hooks::Vector3& v) {
        if (!IsFinite3(v)) return false;
        const float MAX_REASONABLE = 100000.0f;
        return (fabsf(v.x) < MAX_REASONABLE && fabsf(v.y) < MAX_REASONABLE && fabsf(v.z) < MAX_REASONABLE);
    }

    const char* GhostTypeName(int32_t id) {
        static constexpr const char* types[] = {
            "Spirit", "Wraith", "Phantom", "Poltergeist", "Banshee", "Jinn",
            "Mare", "Revenant", "Shade", "Demon", "Yurei", "Oni", "Yokai",
            "Hantu", "Goryo", "Myling", "Onryo", "The Twins", "Raiju", "Obake",
            "Mimic", "Moroi", "Deogen", "Thaye", "None", "Gallu", "Dayan",
            "Obambo", "Kormos", "Aswang", "Deildegast"
        };
        return id >= 0 && id < static_cast<int32_t>(sizeof(types) / sizeof(types[0]))
            ? types[id] : nullptr;
    }

    const char* GhostStateName(int32_t state) {
        for (const auto& entry : stateMap) {
            if (entry.state == state) return entry.name;
        }
        return "Unknown";
    }

    void ResetGhostDisplay() {
        std::lock_guard<std::mutex> lock(Features::ghostDisplayMutex);
        Features::ghostDisplay = {};
    }

    void CacheGhost(Hooks::GhostAI_o* instance) {
        if (Hooks::gCurrentGhostAI != instance) {
            ResetGhostDisplay();
            Hooks::gCurrentGhostAI = instance;
        }
    }

    bool TryGetPosition(void* ghostAIInstance, Hooks::Vector3& out, const char*& source) {
        source = "none";
        if (!SDK::IsReadable(ghostAIInstance, 0x20)) return false;

        // try transform path first
        if (Hooks::oGetTransform && Hooks::oGetPositionInjected) {
            __try {
                void* transform = Hooks::oGetTransform(ghostAIInstance, Hooks::getTransformMethodInfo);
                if (SDK::IsReadable(transform, 0x20)) {
                    Hooks::Vector3 pos{};
                    Hooks::oGetPositionInjected(transform, &pos, Hooks::getPositionInjectedMethodInfo);
                    if (LooksLikeWorldPos(pos)) {
                        out = pos;
                        source = "GhostAI.transform.position";
                        return true;
                    }
                }
            }
            __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION
                ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
                return false;
            }
        }

        return false;
    }
}

void Hooks::ForceHunting()
{
    if (!GhostControl::AllowTransition(gCurrentGhostAI, GHOST_STATE_HUNTING)) {
        GhostControl::huntRequested.store(true);
        Logger::Log("[MOD] Force Hunt queued for the controlled ghost.");
        return;
    }
	if (!gCurrentGhostAI || IsBadReadPtr(gCurrentGhostAI, sizeof(GhostAI_o)))
	{
		Logger::Log("[ERROR] ForceHunting: No valid ghost instance cached.");
		return;
	}

    // Preferred path: ChangeState(Hunting) avoids crafting PhotonMessageInfo manually.
	if (oGhostAI_ChangeState)
	{
        Logger::Log("[MOD] ForceHunting: GhostAI.ChangeState -> HUNTING.");
        __try {
            oGhostAI_ChangeState(gCurrentGhostAI, GHOST_STATE_HUNTING, nullptr, true, nullptr);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            Logger::Log("[ERROR] ForceHunting: ChangeState raised an exception.");
        }
		return;
	}

    // Fallback path for older builds where ChangeState isn't available.
	if (!fnGhostAI_Hunting)
	{
		Logger::Log("[ERROR] ForceHunting: neither GhostAI.ChangeState nor GhostAI.Hunting resolved.");
		return;
	}

    Logger::Log("[MOD] ForceHunting: Triggering hunt on cached ghost.");
    __try {
        fnGhostAI_Hunting(gCurrentGhostAI, true, 0, nullptr, nullptr);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        Logger::Log("[ERROR] ForceHunting: Hunting raised an exception.");
    }
}

void Hooks::hkGhostAI_ChangeState(void* instance, int32_t state, void* photonInteract, bool bParam2, void* methodInfo)
{
    if (!GhostControl::AllowTransition(instance, state)) return;
    static bool logged = false;
    if (!logged) {
        Logger::Log("[HOOK] GhostAI.ChangeState is executing.");
        logged = true;
    }

    if (gGhostInfoLayoutValid && SDK::IsReadable(instance, sizeof(GhostAI_o))) {
        CacheGhost(static_cast<GhostAI_o*>(instance));
        const char* stateName = GhostStateName(state);
        std::lock_guard<std::mutex> lock(Features::ghostDisplayMutex);
        Features::ghostDisplay.state = stateName;
        for (int i = 9; i > 0; --i) {
            Features::ghostDisplay.historicalStates[i] = Features::ghostDisplay.historicalStates[i - 1];
        }
        Features::ghostDisplay.historicalStates[0] = stateName;
    }

    if (oGhostAI_ChangeState) {
        ChangeGhostState(instance, state, photonInteract, bParam2, methodInfo);
    }
}

// This is called once per level start/ghost spawn.
// 
void Hooks::hkGhostAI_Init(void* instance, GhostData_o* data, void* methodInfo)
{
    static bool logged = false;
    if (!logged) {
        Logger::Log("[HOOK] GhostAI.Init is executing.");
        logged = true;
    }

    if (gGhostInfoLayoutValid && SDK::IsReadable(instance, sizeof(GhostAI_o))) {
        ResetGhostDisplay();
        ResetRadar();
        gCurrentGhostAI = static_cast<GhostAI_o*>(instance);
    }

    oGhostAI_Init(instance, data, methodInfo);
}

void Hooks::hkGhostAI_Destroy(void* instance, void* methodInfo) {
    if (instance == gCurrentGhostAI) {
        gCurrentGhostAI = nullptr;
        ResetGhostDisplay();
        ResetRadar();
        Logger::Log("[GHOST] Ghost destroyed; display cleared.");
    }
    oGhostAI_Destroy(instance, methodInfo);
}

void Hooks::hkGhostUpdate(void* instance, void* methodInfo) {
    static bool logged = false;
    if (!logged) {
        Logger::Log("[HOOK] GhostInfo.Update is executing.");
        logged = true;
    }

    oGhostUpdate(instance, methodInfo);
    UpdateRadar();
    if (!gGhostInfoLayoutValid) return;

    GhostInfo_o info{};
    GhostAI_o ghost{};
    if (!SDK::ReadSafe(instance, info) ||
        !SDK::ReadSafe(info.fields.ghost, ghost) ||
        ghost.fields.ghostInfo != instance) {
        CacheGhost(nullptr);
        return;
    }
    CacheGhost(info.fields.ghost);

    const auto& data = info.fields.data.fields;
    const std::string name = SDK::IL2CPP_To_String(data.string);
    const char* type = GhostTypeName(data.ghostTypeId2);
    Vector3 position{};
    const char* source = nullptr;
    const bool positionValid = TryGetPosition(info.fields.ghost, position, source);
    static bool positionFailed = false;
    if (!positionValid && !positionFailed) {
        Logger::Log("[ERROR] GhostAI position unavailable; ghost coordinates cleared.");
    }
    positionFailed = !positionValid;

    {
        std::lock_guard<std::mutex> lock(Features::ghostDisplayMutex);
        auto& display = Features::ghostDisplay;
        if (display.name != name || display.typeId != data.ghostTypeId2) {
            Logger::Log("[GHOST] %s is %s (ID %d)", name.empty() ? "<unavailable>" : name.c_str(),
                type ? type : "<unknown>", data.ghostTypeId2);
        }
        display.name = name;
        display.type = type;
        display.typeId = data.ghostTypeId2;
        display.state = GhostStateName(ghost.fields.state);
        display.positionValid = positionValid;
        display.positionSource = positionValid ? source : nullptr;
        display.position[0] = positionValid ? position.x : 0.0f;
        display.position[1] = positionValid ? position.y : 0.0f;
        display.position[2] = positionValid ? position.z : 0.0f;
    }

    // Execute force-hunt requests on a game thread to avoid cross-thread Unity/IL2CPP calls.
    if (Features::bForceHunting) {
        Features::bForceHunting = false;
        ForceHunting();
    }

}