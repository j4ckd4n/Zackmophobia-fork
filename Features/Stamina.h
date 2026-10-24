#pragma once
#include <atomic>
#include <cstdint>

namespace Stamina {
    using UpdateFunction = void (*)(void* instance, void* methodInfo);

    struct Snapshot {
        uint64_t sampledAt = 0; // GetTickCount64 milliseconds, not game time.
        bool available = false;
        bool sprintEnabled = false;
        bool exhausted = false;
        bool infiniteActive = false;
        float current = 0; // Native sprint seconds, clamped for display only.
        float maximum = 3;
        float normalized = 0;
        const char* reason = "waiting for local controller";
    };

    // Suppresses drain during the native update, not sprinting itself. Does not
    // refill the reserve or clear exhaustion; native recovery still runs.
    inline std::atomic_bool infinite{false};

    // Initialize before installing hooks; Reset after detaching/quiescing hooks.
    // Both bind/reset operations require exclusive access against Update.
    bool Initialize(void* image);
    void Reset();

    // Game thread only. Wrap PCStamina.Update and VRStamina.Update, NOT just
    // PlayerStamina.Update: the concrete methods drain AFTER calling the base.
    // Do not detour the base as well. No object or changed field survives this call.
    void Update(UpdateFunction original, void* instance, void* methodInfo);

    // Read-only game-thread sampling from the active local controller, including
    // the lobby where networked avatars or stamina Update may be unavailable.
    void Observe(void* localController);

    // Thread-safe, contains no Unity references. Samples expire after 1500 ms.
    Snapshot Read();
}
