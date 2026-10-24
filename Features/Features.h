#pragma once
#include <atomic>
#include <mutex>
#include <string>

namespace Features {
    inline std::atomic<bool> bGhostVisible{false};
    inline std::atomic<bool> bDeadInteraction{false};
    inline bool bSpeedEnabled     = false;
    inline float fSprintValue     = 6.0f;
    inline bool bGhostTypeEnabled = true;
    inline bool bForceTarot       = false;
    inline bool bPerfectGame      = false;
    inline bool bBonusReward      = false;
    inline bool bNoKick           = false;
    inline bool bNoSanityLoss     = false;
    inline bool bForceHunting     = false; // triggers once then resets

    struct GhostDisplay {
        std::string name;
        const char* type = nullptr;
        int typeId = -1;
        const char* state = nullptr;
        const char* positionSource = nullptr;
        float position[3] = {};
        bool positionValid = false;
        const char* historicalStates[10] = {};
    };

    inline std::mutex ghostDisplayMutex;
    inline GhostDisplay ghostDisplay;

}
