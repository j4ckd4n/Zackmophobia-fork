#include <windows.h>
#include "Hooks.h"
#include "../SDK/IL2CPP.h"
#include "../Features/Features.h"
#include "../Utils/Logger.h"

namespace Hooks {
     void hkStaminaUpdate(void* instance, void* methodInfo) 
     {
          static bool loggedStamina = false;
          if (!loggedStamina) {
               Logger::Log("[HOOK] PlayerStamina.Update is executing.");
               loggedStamina = true;
          }

          if (Features::bStaminaEnabled && instance && !IsBadReadPtr(instance, 0x100)) 
          {
               float* stamina = (float*)((char*)instance + 0x50);
               if (!IsBadReadPtr(stamina, 4)) *stamina = 3.0f;
          }
          oStaminaUpdate(instance, methodInfo); // Call original game code
     }

     void hkFPCUpdate(void* instance, void* methodInfo) {
          static bool loggedSpeed = false;
          if (!loggedSpeed) {
               Logger::Log("[HOOK] FirstPersonController.Update is executing.");
               loggedSpeed = true;
          }

          if (Features::bSpeedEnabled && instance && !IsBadReadPtr(instance, 0x200)) 
          {
               float* movement = (float*)instance;
               movement[37] = Features::fSprintValue; // Walk Speed
               movement[38] = Features::fSprintValue; // Run Speed
               movement[40] = Features::fSprintValue; // Sprint Speed
          }
          oFPCUpdate(instance, methodInfo); // Call original game code
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