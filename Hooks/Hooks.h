#pragma once
#include <windows.h>
#include "../SDK/IL2CPP.h" // JUMP UP to root, then DOWN to SDK folder

namespace Hooks {
     // Type definition for a generic Update function
     typedef void (*Update_t)(void* instance, void* methodInfo);

     // Original function pointers (to call the real game code after our logic)
     inline Update_t oStaminaUpdate = nullptr;
     inline Update_t oFPCUpdate = nullptr;
     inline Update_t oGhostUpdate = nullptr;

     // Declarations for our custom functions
     void hkStaminaUpdate(void* instance, void* methodInfo);
     void hkFPCUpdate(void* instance, void* methodInfo);
     void hkGhostUpdate(void* instance, void* methodInfo);

     // Main setup function
     void Init();
}