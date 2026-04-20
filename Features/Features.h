#pragma once
#include <windows.h>
#include <stdio.h>

namespace Features {
     inline bool bEnabled = false;
     inline bool bSprintEnabled = false;
     inline bool bStaminaEnabled = false;

     // Checks for F6 keypress to toggle the other mods on/off
     inline bool CheckToggle() 
     {
          if (GetAsyncKeyState(VK_F6) & 1) 
          {
               bEnabled = !bEnabled;
               printf("\n[SYSTEM] Other Mods: %s\n", bEnabled ? "ON" : "OFF");
          }
          return bEnabled;
     }

     // Checks for F5 keypress to toggle the speed mod on/off
     inline bool CheckToggleSpeed() 
     {
          if (GetAsyncKeyState(VK_F5) & 1) 
          {
               bSprintEnabled = !bSprintEnabled;
               printf("\n[SYSTEM] Speed Mod: %s\n", bSprintEnabled ? "ON" : "OFF");
          }
          return bSprintEnabled;
     }

     // Checks for F4 keypress to toggle the stamina mod on/off
     inline bool CheckToggleStamina() 
     {
          if (GetAsyncKeyState(VK_F4) & 1) 
          {
               bStaminaEnabled = !bStaminaEnabled;
               printf("\n[SYSTEM] Stamina Mod: %s\n", bStaminaEnabled ? "ON" : "OFF");
          }
          return bStaminaEnabled;
     }
}