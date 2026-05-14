#include <windows.h>
#include <stdio.h>
#include <string>
#include "Hooks.h"
#include "../SDK/IL2CPP.h"
#include "../Features/Features.h"

void Hooks::hkGhostUpdate(void* instance, void* methodInfo) {
     if (Features::CheckToggle() && instance && !IsBadReadPtr(instance, 0x200)) {
          static std::string lastGhost = "";
          void** fields = (void**)instance;

          // Scan memory for the ghost's name string
          for (int i = 0; i < 100; i++) {
               std::string currentName = SDK::IL2CPP_To_String(fields[i]);
               if (!currentName.empty()) {
                    // Find ghost type ID (usually an integer between 0-27)
                    int typeIdx = 0;
                    int* intFields = (int*)instance;
                    for (int j = 0; j < 64; j++) {
                         if (intFields[j] > 0 && intFields[j] <= 27) {
                              typeIdx = intFields[j];
                              break;
                         }
                    }

                    const char* types[] = { "Spirit", "Wraith", "Phantom", "Poltergeist", "Banshee", "Jinn", "Mare", "Revenant", "Shade", "Demon", "Yurei", "Oni", "Yokai", "Hantu", "Goryo", "Myling", "Onryo", "The Twins", "Raiju", "Obake", "Mimic", "Moroi", "Deogen", "Thaye", "None", "Gallu", "Dayan", "Obambo", "Aswang", "Kormos"};

                    if (currentName != lastGhost) {
                         printf("\n[GHOST] %s is a %s\n", currentName.c_str(), types[typeIdx]);
                         lastGhost = currentName;
                    }
                    break;
               }
          }
     }
     oGhostUpdate(instance, methodInfo);
}