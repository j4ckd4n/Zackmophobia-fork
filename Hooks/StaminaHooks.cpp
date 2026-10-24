#include "../Features/Stamina.h"
#include "../SDK/IL2CPP.h"
#include "../Utils/Logger.h"
#include <algorithm>
#include <cmath>
#include <mutex>

// Build 25811160 / metadata31, PE 6AC7D141 / image 6EAC000. Static REA:
// ev_804262dd1bce8a85d39b9d63ce7c7b98d7c0b43aabee310d6972123e77e8b962
//   DC0350 Start: +40 = 3; DC05E0 drain: +40 = clamp(+40-dt,0,3),
//   bypassed when +35 is false. +50/+58 are UnityEvents, NOT stamina.
//   DC0410 base Update: regeneration, +38 exhaustion, +3C recovery time,
//   +39/+44 regeneration delay. Do not override these native states.
// ev_91a497fa1c268df0284f08da8aa7277db4ea3ad0c2fd399d821627415eb70445
//   D20300 PC Update calls DC0410, then DC05E0.
// ev_693b1707cd6463b553058b6557c6265e6085c2e83400ce73349e9f01bddd0bc2
//   DD4560 native PreventStaminaDrainForTime coroutine toggles +35.
// ev_3828a5ebed41a320779afbe6acfd7aad172e75798dcc106a0203ebe9de99cb42
//   E03150 VR Update likewise calls DC0410, then DC05E0 at E03666.
// RVAs document evidence only. Runtime metadata binds all methods and layouts.
namespace {
    template<class Function>
    struct Method {
        Function call = nullptr;
        void* info = nullptr;

        bool Bind(void* image, const char* namespaze, const char* type,
            const char* name, int arguments) {
            void* klass = image ? SDK::class_from_name(image, namespaze, type) : nullptr;
            info = klass ? SDK::get_method(klass, name, arguments) : nullptr;
            return SDK::ReadSafe(info, call) && call;
        }
    };

    Method<bool (*)(void*, void*)> alive;
    void* pcClass = nullptr;
    void* vrClass = nullptr;
    bool ready = false;
    std::mutex snapshotMutex;
    Stamina::Snapshot snapshot;
    void* trackedInstance = nullptr;
    void* trackedLocal = nullptr;
    uint64_t lastOverrideAt = 0;

    template<class T>
    bool Field(void* object, size_t offset, T& value) {
        return object && SDK::ReadSafe(static_cast<char*>(object) + offset, value);
    }

    bool Living(void* object) {
        return SDK::IsReadable(object, 0x20) && alive.call(object, alive.info);
    }

    bool WriteDrain(void* instance, bool value) {
        __try {
            return SDK::WriteSafe(static_cast<char*>(instance) + 0x35, value);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    bool Matches(void* klass, size_t offset, void* type) {
        if (!klass || !type) return false;
        void* iterator = nullptr;
        while (void* field = SDK::class_get_fields(klass, &iterator)) {
            if (SDK::field_get_offset(field) == offset) {
                const void* fieldType = SDK::field_get_type(field);
                return fieldType && SDK::class_from_type(fieldType) == type;
            }
        }
        return false;
    }

    bool Inspect(void* instance, Stamina::Snapshot& result, bool& drainAllowed, void*& local) {
        if (!ready) { result.reason = "metadata unavailable; see Logs"; return false; }
        result.reason = "local stamina component unavailable";
        if (!Living(instance)) return false;
        void* klass = SDK::object_get_class(instance);
        if (!klass || (klass != pcClass && klass != vrClass)) return false;

        void* staminaBacklink = nullptr;
        bool dead = true;
        float remaining = 0;
        if (!Field(instance, 0x20, local) || !Living(local) ||
            !Field(local, 0xA0, staminaBacklink) || staminaBacklink != instance ||
            !Field(local, 0x38, dead) ||
            !Field(instance, 0x31, result.sprintEnabled) ||
            !Field(instance, 0x38, result.exhausted) ||
            !Field(instance, 0x35, drainAllowed) ||
            !Field(instance, 0x40, remaining)) return false;
        if (!std::isfinite(remaining) || remaining < 0) {
            result.reason = "invalid native stamina value";
            return false;
        }

        result.available = true;
        result.sprintEnabled = result.sprintEnabled && !dead;
        result.reason = "";
        // Native regeneration can overshoot 3 until the following frame.
        result.current = std::clamp(remaining, 0.0f, result.maximum);
        result.normalized = result.current / result.maximum;
        return true;
    }

    bool SafeInspect(void* instance, Stamina::Snapshot& result, bool& drainAllowed, void*& local) {
        __try { return Inspect(instance, result, drainAllowed, local); }
        __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
            result.available = false;
            result.reason = "native stamina read failed; see Logs";
            return false;
        }
    }

    bool SampleLocal(void* local, void*& instance, Stamina::Snapshot& result) {
        __try {
            if (!ready) { result.reason = "metadata unavailable; see Logs"; return false; }
            if (!Living(local) || !Field(local, 0xA0, instance)) return false;
            void* owner = nullptr;
            bool drain = false;
            if (!Inspect(instance, result, drain, owner)) return false;
            if (owner != local) {
                result.available = false;
                result.reason = "local stamina owner changed";
                return false;
            }
            return true;
        }
        __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
            result.available = false;
            result.reason = "native stamina read failed; see Logs";
            return false;
        }
    }

    void Publish(Stamina::Snapshot value) {
        value.sampledAt = GetTickCount64();
        bool changed = false;
        {
            std::lock_guard<std::mutex> lock(snapshotMutex);
            changed = value.available != snapshot.available || value.reason != snapshot.reason;
            snapshot = value;
        }
        if (changed) Logger::Log("[STAMINA] %s", value.available ? "Local stamina sampled." : value.reason);
    }

    struct DrainScope {
        void* instance = nullptr;
        bool changed = false;

        void Restore() {
            if (!changed) return;
            changed = false;
            // The original can destroy its Unity object. Never resurrect it,
            // retain a raw pointer for later, or write to an expired component.
            bool current = true;
            if (Living(instance) && Field(instance, 0x35, current) && !current)
                WriteDrain(instance, true);
        }

        ~DrainScope() { Restore(); }
    };

    void InvokeOriginal(Stamina::UpdateFunction original, void* instance, void* info, DrainScope& scope) {
        __try { original(instance, info); }
        __finally { scope.Restore(); }
    }
}

bool Stamina::Initialize(void* image) {
    Reset();
    if (!image || !SDK::domain_get || !SDK::assembly_open || !SDK::assembly_get_image ||
        !SDK::class_from_name || !SDK::get_method || !SDK::object_get_class ||
        !SDK::class_get_fields || !SDK::field_get_offset ||
        !SDK::field_get_type || !SDK::class_from_type) return false;

    void* domain = SDK::domain_get();
    if (!domain) return false;
    const auto imageFor = [domain](const char* name) -> void* {
        void* assembly = SDK::assembly_open(domain, name);
        return assembly ? SDK::assembly_get_image(assembly) : nullptr;
    };
    void* core = imageFor("mscorlib");
    void* unity = imageFor("UnityEngine.CoreModule");
    void* boolean = core ? SDK::class_from_name(core, "System", "Boolean") : nullptr;
    void* single = core ? SDK::class_from_name(core, "System", "Single") : nullptr;
    void* stamina = SDK::class_from_name(image, "", "PlayerStamina");
    void* local = SDK::class_from_name(image, "", "LocalPlayer");
    pcClass = SDK::class_from_name(image, "", "PCStamina");
    vrClass = SDK::class_from_name(image, "", "VRStamina");
    ready = (pcClass || vrClass) &&
        Matches(stamina, 0x20, local) && Matches(stamina, 0x31, boolean) &&
        Matches(stamina, 0x35, boolean) && Matches(stamina, 0x38, boolean) &&
        Matches(stamina, 0x40, single) && Matches(local, 0x38, boolean) &&
        Matches(local, 0xA0, stamina) &&
        alive.Bind(unity, "UnityEngine", "Object", "op_Implicit", 1);
    Logger::Log("[STAMINA] Local component metadata %s.", ready ? "resolved" : "unavailable");
    return ready;
}

void Stamina::Reset() {
    ready = false;
    infinite.store(false, std::memory_order_relaxed);
    alive = {};
    pcClass = vrClass = nullptr;
    trackedInstance = trackedLocal = nullptr;
    lastOverrideAt = 0;
    Publish({});
}

void Stamina::Update(UpdateFunction original, void* instance, void* methodInfo) {
    if (!original) {
        Snapshot missing;
        missing.reason = "update hook unavailable";
        Publish(missing);
        return;
    }
    Snapshot before;
    bool drainAllowed = false;
    void* local = nullptr;
    const bool valid = SafeInspect(instance, before, drainAllowed, local);
    const bool suppress = valid && before.sprintEnabled &&
        infinite.load(std::memory_order_relaxed);
    DrainScope scope{instance};
    if (suppress && drainAllowed)
        scope.changed = WriteDrain(instance, false);
    const bool active = suppress && (!drainAllowed || scope.changed);

    InvokeOriginal(original, instance, methodInfo, scope);

    Snapshot after;
    bool afterDrain = false;
    void* afterLocal = nullptr;
    if (valid && SafeInspect(instance, after, afterDrain, afterLocal)) {
        after.infiniteActive = active && after.sprintEnabled;
        trackedInstance = instance;
        trackedLocal = afterLocal;
        lastOverrideAt = after.infiniteActive ? GetTickCount64() : 0;
        Publish(after);
    } else if (!trackedInstance || instance == trackedInstance) {
        lastOverrideAt = 0;
        Publish(valid ? after : before);
    }
}

void Stamina::Observe(void* localController) {
    Snapshot observed;
    void* instance = nullptr;
    if (SampleLocal(localController, instance, observed)) {
        const uint64_t now = GetTickCount64();
        observed.infiniteActive = instance == trackedInstance && infinite.load() &&
            observed.sprintEnabled && lastOverrideAt && now >= lastOverrideAt && now - lastOverrideAt <= 1500;
        if (instance != trackedInstance) lastOverrideAt = 0;
        trackedInstance = instance;
        trackedLocal = localController;
        Publish(observed);
    } else if (!trackedInstance || localController == trackedLocal) {
        lastOverrideAt = 0;
        observed.available = false;
        Publish(observed);
    }
}

Stamina::Snapshot Stamina::Read() {
    std::lock_guard<std::mutex> lock(snapshotMutex);
    Snapshot result = snapshot;
    if (result.available && GetTickCount64() - result.sampledAt > 1500) {
        result.available = false;
        result.infiniteActive = false;
        result.reason = "stale local sample";
    }
    return result;
}
