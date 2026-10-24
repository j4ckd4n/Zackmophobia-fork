#pragma once
#include <cstdint>
#include <string>

namespace Recovery {
    struct Snapshot {
        bool available = false;
        bool pending = false;
        bool restorePending = false;
        uint64_t sampledAt = 0;
        std::string reason = "Waiting for local controller.";
        std::string message;
    };

    void Initialize(void* image, void* unityImage);
    // Native calls and Reset run on the game thread; Queue/Read are UI-safe.
    void Update(void* local, bool eligible, uint64_t generation);
    bool Reset();
    bool Queue(uint64_t now);
    Snapshot Read();
}
