#include "Hooks.h"
#include <stdio.h>
#include "../Features/Features.h"
#include "../Utils/Logger.h"

int Hooks::hkGetBonus(void* instance) 
{
     static bool loggedBonus = false;
     if (!loggedBonus) {
          Logger::Log("[HOOK] LevelValues.GetInvestigationBonusReward is executing.");
          loggedBonus = true;
     }

     if (Features::bBonusReward)
     {
          Logger::Log("[MOD] Investigation bonus overridden to 20000.");
          return 20000;
     }

     return oGetBonus ? oGetBonus(instance) : 0;
}

bool Hooks::hkIsPerfect(void* instance) 
{
     static bool loggedPerfect = false;
     if (!loggedPerfect) {
          Logger::Log("[HOOK] LevelValues.IsPerfectGame is executing.");
          loggedPerfect = true;
     }

     if (Features::bPerfectGame)
     {
          Logger::Log("[MOD] Perfect game forced TRUE.");
          return true;
     }

     return oIsPerfect ? oIsPerfect(instance) : false;
}