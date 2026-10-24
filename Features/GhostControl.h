#pragma once
#include "Radar.h"
#include <atomic>

namespace GhostControl {
    using UpdateFunction = void (*)(void*, void*);
    inline std::atomic<bool> desired{false};
    inline std::atomic<bool> huntRequested{false};
    inline std::atomic<bool> inputCaptured{false};
    inline std::atomic<float> speed{1.7f};
    inline std::atomic<float> cameraDistance{3.0f};
    inline std::atomic<bool> firstPerson{false};
    inline std::atomic<int> key{0x76}; // F7

    struct Context {
        void* ghost = nullptr;
        void* player = nullptr;
        void* local = nullptr;
        void* controller = nullptr;
        void* camera = nullptr;
        uint64_t generation = 0;
        bool authorized = false;
    };

    struct Snapshot {
        bool available = false;
        bool active = false;
        bool hunting = false;
        bool restoring = false;
        uint64_t sampledAt = 0;
        std::string message;
    };

    inline Radar::Vector3 Step(float horizontal, float vertical, float yaw, float metersPerSecond, float dt) {
        if (!std::isfinite(horizontal) || !std::isfinite(vertical) || !std::isfinite(yaw) ||
            !std::isfinite(metersPerSecond) || !std::isfinite(dt) || dt <= 0) return {};
        const float magnitude = std::sqrt(horizontal * horizontal + vertical * vertical);
        if (magnitude > 1) { horizontal /= magnitude; vertical /= magnitude; }
        const float distance = std::clamp(metersPerSecond, 0.2f, 6.0f) * (std::min)(dt, 0.1f);
        return {(horizontal * std::cos(yaw) + vertical * std::sin(yaw)) * distance, 0,
            (vertical * std::cos(yaw) - horizontal * std::sin(yaw)) * distance};
    }

    inline Radar::Quaternion ViewRotation(float yaw, float pitch) {
        return {std::sin(pitch * 0.5f) * std::cos(yaw * 0.5f),
            std::sin(yaw * 0.5f) * std::cos(pitch * 0.5f),
            -std::sin(yaw * 0.5f) * std::sin(pitch * 0.5f),
            std::cos(yaw * 0.5f) * std::cos(pitch * 0.5f)};
    }

    void Initialize(void* image, void* unityImage, void* navigationImage, void* physicsImage);
    long InstallHooks(bool attach);
    void SetHooksInstalled(bool installed);
    void Update(const Context& context);
    bool Restore();
    bool ControlsPlayer(void* controller);
    bool BlocksInput(void* instance, bool door);
    bool AllowTransition(void* ghost, int state);
    void PlayerUpdate(UpdateFunction original, void* controller, void* info);
    Snapshot Read();
}

namespace Hooks {
    GhostControl::Context ReadHostContext();
}
