#include <windows.h>
#include "Hooks.h"
#include "../SDK/IL2CPP.h"
#include "../Features/Features.h"
#include "../Features/GhostControl.h"
#include "../Features/Stamina.h"
#include "../Utils/Logger.h"

namespace Hooks {
     void hkPCStaminaUpdate(void* instance, void* methodInfo) {
          Stamina::Update(oPCStaminaUpdate, instance, methodInfo);
     }

     void hkVRStaminaUpdate(void* instance, void* methodInfo) {
          Stamina::Update(oVRStaminaUpdate, instance, methodInfo);
     }

     void hkFPCUpdate(void* instance, void* methodInfo) {
          UpdateRadar();
          static bool loggedSpeed = false;
          if (!loggedSpeed) {
               Logger::Log("[HOOK] FirstPersonController.Update is executing.");
               loggedSpeed = true;
          }

          if (!GhostControl::ControlsPlayer(instance) && Features::bSpeedEnabled && instance && !IsBadReadPtr(instance, 0x200))
          {
               // Named indices for clarity and safer writes
               constexpr int WALK_SPEED_IDX = 37;
               constexpr int RUN_SPEED_IDX = 38;
               constexpr int SPRINT_SPEED_IDX = 40;

               float* movement = (float*)instance;
               auto setMovement = [&](int idx, float val){
                    if (!movement) return;
                    if (!SDK::IsReadable(&movement[idx], sizeof(float))) {
                         Logger::Log("[FPC] movement[%d] not readable", idx);
                         return;
                    }
                    SDK::WriteSafe(&movement[idx], val);
               };

               setMovement(WALK_SPEED_IDX, Features::fSprintValue); // Walk Speed
               setMovement(RUN_SPEED_IDX, Features::fSprintValue); // Run Speed
               setMovement(SPRINT_SPEED_IDX, Features::fSprintValue); // Sprint Speed
          }

          GhostControl::PlayerUpdate(oFPCUpdate, instance, methodInfo);
          UpdateRadar();
     }

     void hkChangeSanity(void* instance, float amount, void* methodInfo) 
     {
          static bool loggedSanity = false;
          if (!loggedSanity) {
               Logger::Log("[HOOK] PlayerSanity.ChangeSanity is executing.");
               loggedSanity = true;
          }
          if (Features::bNoSanityLoss && instance && !IsBadReadPtr(instance, 0x100)) 
          {
               // Negate sanity loss (keep sanity the same or increase it)
               if (amount < 0) amount = 0.0f; 
          }
          oChangeSanity(instance, amount, methodInfo); // Call original game code
     }
}