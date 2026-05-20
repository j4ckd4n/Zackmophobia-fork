#include <windows.h>
#include <stdio.h>
#include <string>
#include "Hooks.h"
#include "../SDK/IL2CPP.h"
#include "../Features/Features.h"
#include "../Utils/Logger.h"

namespace {
    constexpr int32_t GHOST_STATE_HUNTING = 2;
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

    if (state == GHOST_STATE_HUNTING) {
        Logger::Log("[GHOST] ChangeState -> HUNTING (bParam2=%s, photonInteract=%p)", bParam2 ? "true" : "false", photonInteract);
    } else {
        Logger::Log("[GHOST] ChangeState -> %d (bParam2=%s, photonInteract=%p)", state, bParam2 ? "true" : "false", photonInteract);
    }

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

        // Display all fields
        // printf("\n[GHOST DATA] Ghost Type 1: %d\n", fields->ghostTypeId1);
        // printf("[GHOST DATA] Ghost Type 2: %d\n", fields->ghostTypeId2);
        // printf("[GHOST DATA] Unknown Int 1: %d\n", fields->unkInt1);
        // printf("[GHOST DATA] Unknown String 1: %s\n", SDK::IL2CPP_To_String(fields->string).c_str());
        // printf("[GHOST DATA] Unknown Int 2: %d\n", fields->unkInt2);
        // printf("[GHOST DATA] Unknown Int 3: %d\n", fields->unkInt3);
        // printf("[GHOST DATA] Unknown Int 4: %d\n", fields->unkInt4);
        // printf("[GHOST DATA] Unknown Int 5: %d\n", fields->unkInt5);
        // printf("[GHOST DATA] Unknown Bool 1: %s\n", fields->unkBool1 ? "true" : "false");
        // printf("[GHOST DATA] Unknown Bool 2: %s\n", fields->unkBool2 ? "true" : "false");
        // printf("[GHOST DATA] Unknown Bool 3: %s\n", fields->unkBool3 ? "true" : "false");
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