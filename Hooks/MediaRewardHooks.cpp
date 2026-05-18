#include "Hooks.h"
#include "../Features/Features.h"
#include <stdio.h>
#include "../Utils/Logger.h"

int Hooks::hkGetRewardAmount(void* instance, void* methodInfo)
{
     static bool logged = false;
     if (!logged) {
          Logger::Log("[HOOK] Media.GetRewardAmount is executing.");
          logged = true;
     }

     if (Features::bBonusReward)
     {
          Logger::Log("[MOD] Media reward overridden to 10000.");
          return 10000;
     }

     return oGetRewardAmount ? oGetRewardAmount(instance, methodInfo) : 0;
}