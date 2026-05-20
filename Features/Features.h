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
    inline const char* cGhostName = nullptr; // set to current ghost's name when GhostTypeDisplay is enabled
    inline const char* cGhostType = nullptr; // set to current ghost's type when GhostTypeDisplay is enabled
    inline int cGhostTypeId = -1; // set to current ghost's type ID (0-29) when GhostTypeDisplay is enabled
}
