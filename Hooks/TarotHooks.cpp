#include "Hooks.h"
#include "../Features/Features.h"
#include <stdio.h>
#include "../Utils/Logger.h"

namespace Hooks {
     void hkSetCard(void* instance, int cardType, void* methodInfo) 
     {
          static bool logged = false;
          if (!logged) {
               Logger::Log("[HOOK] TarotCards.SetCard is executing.");
               logged = true;
          }

          if (Features::bForceTarot) 
          {
               // Sun is index 7 in the dnSpy enum
               cardType = 7;
               printf("[MOD] Tarot Card intercepted! Forcing SUN.\n");
               Logger::Log("[MOD] Tarot card overridden to SUN (7).");
          }

          // Call original with our modified cardType
          oSetCard(instance, cardType, methodInfo);
     }
}