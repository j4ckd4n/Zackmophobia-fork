#include "Hooks.h"
#include <stdio.h>
#include "../Features/Features.h"

int Hooks::hkGetBonus(void* instance) {
     // You can return any value here, but keep it reasonable to avoid server-side flags
     // Returning 1000 or 5000 is usually plenty
     if (Features::CheckToggle())
     {
          return 5000;
     }
}

bool Hooks::hkIsPerfect(void* instance) {
     // Forces the game to think you completed every objective perfectly
     if (Features::CheckToggle())
     {
          return true;
     }
}