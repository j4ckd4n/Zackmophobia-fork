#include <windows.h>
#include "Hooks.h"
#include "../SDK/IL2CPP.h"
#include "../Features/Features.h"

namespace Hooks {
     void hkStaminaUpdate(void* instance, void* methodInfo) {
          if (Features::CheckToggle() && instance && !IsBadReadPtr(instance, 0x100)) {
               // PlayerStamina + 0x50 = current stamina float
               float* stamina = (float*)((char*)instance + 0x50);
               if (!IsBadReadPtr(stamina, 4)) *stamina = 3.0f;
          }
          oStaminaUpdate(instance, methodInfo); // Call original game code
     }

     void hkFPCUpdate(void* instance, void* methodInfo) {
          if (Features::CheckToggle() && instance && !IsBadReadPtr(instance, 0x200)) {
               float* movement = (float*)instance;
               movement[37] = 6.0f; // Walk Speed
               movement[38] = 6.0f; // Run Speed
               movement[40] = 6.0f; // Sprint Speed
          }
          oFPCUpdate(instance, methodInfo);
     }
}