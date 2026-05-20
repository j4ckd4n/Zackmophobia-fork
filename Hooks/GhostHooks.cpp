#include <windows.h>
#include <stdio.h>
#include <cmath>
#include <string>
#include "Hooks.h"
#include "../SDK/IL2CPP.h"
#include "../Features/Features.h"
#include "../Utils/Logger.h"

namespace {
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

    bool TryReadVec3At(void* base, size_t offset, Hooks::Vector3& out) {
        if (!base) return false;
        return SDK::ReadSafe((char*)base + offset, out) && LooksLikeWorldPos(out);
    }

    bool TryGetPosition(void* ghostAIInstance, Hooks::Vector3& out, const char*& source) {
        source = "none";
        if (!SDK::IsReadable(ghostAIInstance, 0x20)) return false;

        // try transform path first
        if (Hooks::oGetTransform && Hooks::oGetPosition) {
            __try {
                void* transform = Hooks::oGetTransform(ghostAIInstance, nullptr);
                if (SDK::IsReadable(transform, 0x20)) {
                    // On x64 IL2CPP, Vector3 can be returned through an explicit out pointer in many builds.
                    Hooks::Vector3 pos{};
                    Hooks::oGetPosition(&pos, transform, nullptr);
                    if (LooksLikeWorldPos(pos)) {
                        out = pos;
                        source = "unity:get_position";
                        return true;
                    }
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                // Signature/layout mismatches are common across Unity versions.
            }
        }

        return false;
    }
}

void Hooks::ForceHunting()
{
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
    static bool logged = false;
    if (!logged) {
        Logger::Log("[HOOK] GhostAI.ChangeState is executing.");
        logged = true;
    }

    if (instance && !IsBadReadPtr(instance, sizeof(GhostAI_o))) {
        gCurrentGhostAI = (GhostAI_o*)instance;
    }

    const char* stateName = "Unknown";
    for (const auto& entry : stateMap) {
        if (entry.state == state) {
            stateName = entry.name;
            break;
        }
    }

    if (stateName != "Unknown") {
        Features::cGhostState = const_cast<char*>(stateName);
    } else {
        Logger::Log("[GHOST] Unknown ChangeState -> %d (bParam2=%s, photonInteract=%p)", state, bParam2 ? "true" : "false", photonInteract);
    }

    // add to historical states buffer
    for (int i = 9; i > 0; i--) {
        Features::cHistoricalStates[i] = Features::cHistoricalStates[i - 1];
    }
    Features::cHistoricalStates[0] = stateName;

    if (oGhostAI_ChangeState) {
        oGhostAI_ChangeState(instance, state, photonInteract, bParam2, methodInfo);
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

    // Cache the ghost instance so ForceHunting() can use it
    if (instance && !IsBadReadPtr(instance, sizeof(GhostAI_o)))
        gCurrentGhostAI = (GhostAI_o*)instance;

    if (Features::bGhostTypeEnabled && instance && !IsBadReadPtr(instance, 0x200)) {
        GhostData_o* ghostData = (GhostData_o*)data;
        GhostData_Fields* fields = &ghostData->fields;
        
        const char* types[] = { "Spirit", "Wraith", "Phantom", "Poltergeist", "Banshee", "Jinn", "Mare", "Revenant", "Shade", "Demon", "Yurei", "Oni", "Yokai", "Hantu", "Goryo", "Myling", "Onryo", "The Twins", "Raiju", "Obake", "Mimic", "Moroi", "Deogen", "Thaye", "None", "Gallu", "Dayan", "Obambo", "Aswang", "Kormos"};

        Logger::Log("[GHOST INIT] Ghost Type: %d, %d", fields->ghostTypeId1, fields->ghostTypeId2);
        Logger::Log("[GHOST INIT] Ghost Type: %s", types[fields->ghostTypeId2]);
    }

    oGhostAI_Init(instance, data, methodInfo);
}

void Hooks::hkGhostUpdate(void* instance, void* methodInfo) {
    static bool logged = false;
    if (!logged) {
        Logger::Log("[HOOK] GhostInfo.Update is executing.");
        logged = true;
    }

    // Execute force-hunt requests on a game thread to avoid cross-thread Unity/IL2CPP calls.
    if (Features::bForceHunting) {
        Features::bForceHunting = false;
        ForceHunting();
    }

    if (Features::bGhostTypeEnabled && instance && !IsBadReadPtr(instance, 0x200)) {

        static int sampleTick = 0;
        if ((++sampleTick % 60) == 0) { // Don't spam every frame, check periodically as ghost spawns and types are stable.
            Hooks::Vector3 pos{};
            const char* source = nullptr;
            if (TryGetPosition(instance, pos, source)) {
                Features::cGhostPosSource = const_cast<char*>(source);
                Features::cGhostPos[0] = pos.x;
                Features::cGhostPos[1] = pos.y;
                Features::cGhostPos[2] = pos.z;
                // Logger::Log("[GHOST] Position (%s): X=%.2f Y=%.2f Z=%.2f", source, pos.x, pos.y, pos.z);
            }
        }

        static std::string lastGhost = "";
            void** fields = (void**)instance;

            // Scan memory for the ghost's name string
        for (int i = 0; i < 100; i++) {
            std::string currentName = SDK::IL2CPP_To_String(fields[i]);
            if (!currentName.empty()) {
                // Find ghost type ID (usually an integer between 0-27)
                int typeIdx = 0;
                int* intFields = (int*)instance;
                for (int j = 0; j < 64; j++) {
                    if (intFields[j] > 0 && intFields[j] <= 29) {
                        typeIdx = intFields[j];
                        break;
                    }
                }

                const char* types[] = { "Spirit", "Wraith", "Phantom", "Poltergeist", "Banshee", "Jinn", "Mare", "Revenant", "Shade", "Demon", "Yurei", "Oni", "Yokai", "Hantu", "Goryo", "Myling", "Onryo", "The Twins", "Raiju", "Obake", "Mimic", "Moroi", "Deogen", "Thaye", "None", "Gallu", "Dayan", "Obambo", "Aswang", "Kormos"};

                if (currentName != lastGhost) {
                    printf("\n[GHOST][ID: %d] %s is a %s\n", typeIdx, currentName.c_str(), types[typeIdx]);
                    Logger::Log("[GHOST] %s is a %s", currentName.c_str(), types[typeIdx]);

                    Features::cGhostName = currentName.c_str();
                    Features::cGhostType = types[typeIdx];
                    Features::cGhostTypeId = typeIdx;
                    lastGhost = currentName;
                }
                break;
            }
        }
    }
    oGhostUpdate(instance, methodInfo);
}