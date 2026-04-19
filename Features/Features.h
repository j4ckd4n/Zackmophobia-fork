#pragma once
#include <windows.h>
#include <stdio.h>

namespace Features {
     inline bool bEnabled = true;

     // Checks for F6 keypress to toggle the mod on/off
     inline bool CheckToggle() {
          if (GetAsyncKeyState(VK_F6) & 1) {
               bEnabled = !bEnabled;
               printf("\n[SYSTEM] Mod Status: %s\n", bEnabled ? "ON" : "OFF");
          }
          return bEnabled;
     }
}