#pragma once
#include "GhostControl.h"
#include <array>

namespace WorldActions {
    enum class Kind { UnlockEntrance, NormalInteraction, Whisper, NearbyLight, SpecialAbility, Count };
    struct Option {
        Kind kind{};
        std::string label;
        bool available = false;
        std::string reason;
    };
    struct Snapshot {
        bool available = false;
        bool faulted = false;
        bool pending = false;
        uint64_t generation = 0;
        uint64_t sampledAt = 0;
        unsigned lockedEntrances = 0;
        std::array<Option, static_cast<size_t>(Kind::Count)> options{};
        std::string message;
    };

    // Initialize once before servicing; Update/Reset run on the game thread.
    // Pass active OR restoring possession, not merely the requested toggle.
    // Only Update invokes native actions. Queue/Read are safe for the UI thread.
    // Queue uses GetTickCount64 milliseconds, rejects snapshots/requests >750 ms,
    // and holds at most one request. Reset on session generation change/unload.
    // SpecialAbility requests native state 15 for verified current-type cases
    // only. Check option labels/reasons: Demon can initiate a hunt, other
    // abilities can move the ghost/drain sanity, and native eligibility applies.
    // All ghost actions are disabled during possession/hunts/events.
    void Initialize(void* domain, void* image, void* unityImage);
    void Update(const GhostControl::Context& context, bool possessed = false);
    Snapshot Read();
    bool Queue(Kind kind, uint64_t generation, uint64_t now);
    void Reset();
}
