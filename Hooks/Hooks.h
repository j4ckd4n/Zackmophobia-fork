#pragma once
#include <windows.h>
#include "../SDK/IL2CPP.h"

namespace Hooks {
     // Typedefs
     typedef void (*Update_t)(void* instance, void* methodInfo);
     typedef void (*SetCard_t)(void* instance, int cardType, void* methodInfo);
     typedef int (*GetBonus_t)(void* instance);
     typedef bool (*IsPerfect_t)(void* instance);

     // Original function pointers
     inline Update_t oStaminaUpdate = nullptr;
     inline Update_t oFPCUpdate = nullptr;
     inline Update_t oGhostUpdate = nullptr;
     inline SetCard_t oSetCard = nullptr;
     inline GetBonus_t oGetBonus = nullptr;
     inline IsPerfect_t oIsPerfect = nullptr;

     // Hook function declarations
     void hkStaminaUpdate(void* instance, void* methodInfo);
     void hkFPCUpdate(void* instance, void* methodInfo);
     void hkGhostUpdate(void* instance, void* methodInfo);
     void hkSetCard(void* instance, int cardType, void* methodInfo);
     int hkGetBonus(void* instance);
     bool hkIsPerfect(void* instance);

     void Init();
}