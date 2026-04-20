#include <windows.h>
#include <stdio.h>
#include "Hooks.h"
#include "detours.h"
#include "../SDK/IL2CPP.h"

#pragma comment(lib, "../Lib/detours.lib") 

namespace Hooks {
     void Init() {

          printf("[SYSTEM] Welcome to Zackmophobia\nF4: Toggle Stamina Mod\nF5: Toggle Speed Mod\nF6: Toggle all other mods(perfect game, tarot card force, ghost info)\n");
          void* domain = SDK::domain_get();
          void* assembly = SDK::assembly_open(domain, "Assembly-CSharp");
          void* image = SDK::assembly_get_image(assembly);

          auto getMethod = [&](const char* className, const char* methodName, int args) {
               void* klass = SDK::class_from_name(image, "", className);
               return klass ? SDK::get_method(klass, methodName, args) : nullptr;
               };

          // Existing Hooks
          void* mStamina = getMethod("PlayerStamina", "Update", 0);
          if (mStamina) oStaminaUpdate = *(Update_t*)mStamina;

          void* mFPC = getMethod("FirstPersonController", "Update", 0);
          if (mFPC) oFPCUpdate = *(Update_t*)mFPC;

          void* mGhost = getMethod("GhostInfo", "Update", 0);
          if (mGhost) oGhostUpdate = *(Update_t*)mGhost;

          void* mTarot = getMethod("TarotCards", "SetCard", 1);
          if (mTarot) oSetCard = *(SetCard_t*)mTarot;

          // NEW: Reward & Level Hooks
          void* mBonus = getMethod("LevelValues", "GetInvestigationBonusReward", 0);
          if (mBonus) oGetBonus = *(GetBonus_t*)mBonus;

          void* mPerfect = getMethod("LevelValues", "IsPerfectGame", 0);
          if (mPerfect) oIsPerfect = *(IsPerfect_t*)mPerfect;

          DetourTransactionBegin();
          DetourUpdateThread(GetCurrentThread());

          if (oStaminaUpdate) DetourAttach(&(PVOID&)oStaminaUpdate, hkStaminaUpdate);
          if (oFPCUpdate) DetourAttach(&(PVOID&)oFPCUpdate, hkFPCUpdate);
          if (oGhostUpdate) DetourAttach(&(PVOID&)oGhostUpdate, hkGhostUpdate);
          if (oSetCard) DetourAttach(&(PVOID&)oSetCard, hkSetCard);

          // Apply new reward hooks
          if (oGetBonus) DetourAttach(&(PVOID&)oGetBonus, hkGetBonus);
          if (oIsPerfect) DetourAttach(&(PVOID&)oIsPerfect, hkIsPerfect);

          DetourTransactionCommit();

          printf("[SYSTEM] LevelValues Reward Hooks Applied.\n");
     }
}