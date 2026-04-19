#include <windows.h>
#include <stdio.h>
#include "Hooks.h"          // Local file
#include "detours.h"        // Local file (based on your tree)
#include "../SDK/IL2CPP.h"  // UP to root, DOWN to SDK
#include "../Features/Features.h" // UP to root, DOWN to Features

// Link the library physically located in the Lib folder
#pragma comment(lib, "../Lib/detours.lib") 

namespace Hooks {
     void Init() {
          void* domain = SDK::domain_get();
          void* assembly = SDK::assembly_open(domain, "Assembly-CSharp");
          void* image = SDK::assembly_get_image(assembly);

          // Helper lambda to find method addresses
          auto getMethod = [&](const char* className) {
               void* klass = SDK::class_from_name(image, "", className);
               return klass ? SDK::get_method(klass, "Update", 0) : nullptr;
               };

          // Capture addresses
          void* mStamina = getMethod("PlayerStamina");
          if (mStamina) oStaminaUpdate = *(Update_t*)mStamina;

          void* mFPC = getMethod("FirstPersonController");
          if (mFPC) oFPCUpdate = *(Update_t*)mFPC;

          void* mGhost = getMethod("GhostInfo");
          if (mGhost) oGhostUpdate = *(Update_t*)mGhost;

          // Perform the detours
          DetourTransactionBegin();
          DetourUpdateThread(GetCurrentThread());
          if (oStaminaUpdate) DetourAttach(&(PVOID&)oStaminaUpdate, hkStaminaUpdate);
          if (oFPCUpdate) DetourAttach(&(PVOID&)oFPCUpdate, hkFPCUpdate);
          if (oGhostUpdate) DetourAttach(&(PVOID&)oGhostUpdate, hkGhostUpdate);
          DetourTransactionCommit();

          printf("[SYSTEM] Hooks successfully detoured.\n");
     }
}