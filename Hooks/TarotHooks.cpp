#include "Hooks.h"
#include "../Features/Features.h"
#include <stdio.h>

namespace Hooks {
     void hkSetCard(void* instance, int cardType, void* methodInfo) 
     {
          if (Features::CheckToggle()) 
          {
               // Sun is index 7 in the dnSpy enum
               cardType = 7;
               printf("[MOD] Tarot Card intercepted! Forcing SUN.\n");
          }

          // Call original with our modified cardType
          oSetCard(instance, cardType, methodInfo);
     }
}