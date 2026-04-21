#pragma once
#include <windows.h>
#include "../SDK/IL2CPP.h"

namespace Hooks {
     typedef void (*Update_t)(void* instance, void* methodInfo);
     typedef void (*SetCard_t)(void* instance, int cardType, void* methodInfo);
     typedef int (*GetBonus_t)(void* instance);
     typedef bool (*IsPerfect_t)(void* instance);
     typedef void (*KickPlayerNetworked_t)(void* instance, bool isBanned, void* photonMessageInfo, void* methodInfo);
     typedef void (*GetRewardAmount_t)(void* instance, void* methodInfo);
     
     // Original function pointers
     inline Update_t oStaminaUpdate = nullptr;
     inline Update_t oFPCUpdate = nullptr;
     inline Update_t oGhostUpdate = nullptr;
     inline SetCard_t oSetCard = nullptr;
     inline GetBonus_t oGetBonus = nullptr;
     inline IsPerfect_t oIsPerfect = nullptr;
     inline KickPlayerNetworked_t oServerManagerKickPlayerNetworked = nullptr;
     inline GetRewardAmount_t oGetRewardAmount = nullptr;

     // Hook function declarations
     void hkStaminaUpdate(void* instance, void* methodInfo);
     void hkFPCUpdate(void* instance, void* methodInfo);
     void hkGhostUpdate(void* instance, void* methodInfo);
     void hkSetCard(void* instance, int cardType, void* methodInfo);
     int hkGetBonus(void* instance);
     bool hkIsPerfect(void* instance);
     void hkKickPlayerNetworked(void* instance, bool isBanned, void* photonMessageInfo, void* methodInfo);
     int hkGetRewardAmount(void* instance, void* methodInfo);

     void Init();
}