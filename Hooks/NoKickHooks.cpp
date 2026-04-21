#include "Hooks.h"
#include <stdio.h>
#include "../Features/Features.h"

void Hooks::hkKickPlayerNetworked(void* instance, bool isBanned, void* photonMessageInfo, void* methodInfo)
{
     printf("[BLOCK] Prevented a networked kick attempt.\n");
     return;
}