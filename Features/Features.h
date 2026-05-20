#pragma once

namespace Features {
    inline bool bStaminaEnabled   = false;
    inline bool bSpeedEnabled     = false;
    inline float fSprintValue     = 6.0f;
    inline bool bGhostTypeEnabled = false;
    inline bool bForceTarot       = false;
    inline bool bPerfectGame      = false;
    inline bool bBonusReward      = false;
    inline bool bNoKick           = false;
    inline bool bNoSanityLoss     = false;
    inline bool bForceHunting     = false; // triggers once then resets
    inline char* cPlayerPosSource = nullptr; // set to "cache" when position is updated from cached transform, "live" when read directly from FPC each time
    inline float cPlayerPos[3]; // updated every 120 ticks when FPCUpdate hook is active
}
