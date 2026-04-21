#include "Hooks.h"
#include <stdio.h>
#include "../Features/Features.h"

int Hooks::hkGetBonus(void* instance) 
{
     if (Features::CheckToggle())
     {
          return 20000;
     }
}

bool Hooks::hkIsPerfect(void* instance) 
{
     if (Features::CheckToggle())
     {
          return true;
     }
}