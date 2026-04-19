#include "Hooks.h"
#include <stdio.h>

void Hooks::hkGiveChallenge(void* instance) {
     if (!instance) return;

     printf("[!] GiveChallengeOnEnable::OnEnable triggered! Looping all types...\n");

     // The Enum had ~48 entries (0 to 47)
     for (int i = 0; i <= 47; i++) {
          // Offset 0x18 is the typical field location for the first variable in a MonoBehaviour
          int* challengeTypeField = (int*)((uintptr_t)instance + 0x18);

          *challengeTypeField = i; // Inject the enum ID

          // Call the original function to process the current ID
          oGiveChallenge(instance);
     }

     printf("[SUCCESS] Forced %d challenge completions. Check your trophies/rewards!\n", 48);
}