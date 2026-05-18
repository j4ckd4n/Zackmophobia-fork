#include "Hooks.h"
#include <stdio.h>
#include "../Features/Features.h"
#include "../Utils/Logger.h"

void Hooks::hkKickPlayerNetworked(void* instance, bool isBanned, void* photonMessageInfo, void* methodInfo)
{
     static bool logged = false;
     if (!logged) {
          Logger::Log("[HOOK] ServerManager.KickPlayerNetworked is executing.");
          logged = true;
     }

     if (Features::bNoKick)
     {
          printf("[BLOCK] Prevented a networked kick attempt.\n");
          Logger::Log("[BLOCK] Prevented a networked kick attempt.");
          return;
     }

     if (oServerManagerKickPlayerNetworked)
     {
          oServerManagerKickPlayerNetworked(instance, isBanned, photonMessageInfo, methodInfo);
     }
}