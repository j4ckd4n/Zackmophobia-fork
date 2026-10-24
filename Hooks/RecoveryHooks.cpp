#include "../Features/Recovery.h"
#include "../Features/Pickup.h"
#include "../SDK/IL2CPP.h"
#include "../Utils/Logger.h"
#include <cmath>
#include <cstring>
#include <mutex>

// Build 25811160: native spawn lists, not cached player coordinates.
// REA ev_c1f549c5ef4206085b1e2141e05670d8084dfbcf651e447218c95c2ad5358170
// REA ev_62d13b10c54dfb2fb1b602ab8128ae7519f6d98570aeb8aceed1e85d4970d194
// REA ev_24e97b5d71302ee4686fae65fe7ae700ea068879e6820951abbd0d271e35cef8
namespace {
    struct Vector3 { float x, y, z; };
    void* (*invoke)(void*, void*, void**, void**) = nullptr;
    void* (*unbox)(void*) = nullptr;
    uint32_t (*pin)(void*, bool) = nullptr;
    void (*unpin)(uint32_t) = nullptr;
    bool (*assignable)(void*, void*) = nullptr;
    void* aliveMethod = nullptr;
    void* findMethod = nullptr;
    void* positionMethod = nullptr;
    void* enabledMethod = nullptr;
    void* setEnabledMethod = nullptr;
    void* teleportMethod = nullptr;
    void* mainType = nullptr;
    void* multiplayerType = nullptr;
    void* localType = nullptr;
    void* transformType = nullptr;
    bool ready = false, healthy = true, failed = false;
    DWORD gameThread = 0;
    constexpr uint64_t MaxAge = 750;
    std::mutex mutex;
    Recovery::Snapshot status;
    struct Target {
        void* local = nullptr;
        void* physics = nullptr;
        void* owner = nullptr;
        Vector3 position{};
        const char* reason = "Waiting for local controller.";
    } current;
    uint64_t currentGeneration = 0;
    struct Request {
        bool pending = false;
        void* local = nullptr;
        void* owner = nullptr;
        uint64_t generation = 0;
        uint64_t at = 0;
    } request;
    struct RestoreState {
        void* local = nullptr;
        void* physics = nullptr;
        uint32_t localRoot = 0, physicsRoot = 0;
        bool active = false;
    } restoration;

    template<class T>
    bool Field(void* object, size_t offset, T& value) {
        return object && SDK::ReadSafe(static_cast<char*>(object) + offset, value);
    }

    bool TypeAt(void* klass, size_t offset, const char* name) {
        void* iterator = nullptr;
        while (klass) {
            void* field = SDK::class_get_fields(klass, &iterator);
            if (!field) break;
            if (SDK::field_get_offset(field) != offset) continue;
            void* type = SDK::class_from_type(SDK::field_get_type(field));
            const char* actual = type ? SDK::class_get_name(type) : nullptr;
            return actual && std::strcmp(actual, name) == 0;
        }
        return false;
    }

    void* Call(void* method, void* self = nullptr, void** args = nullptr) {
        if (!method) { failed = true; return nullptr; }
        void* exception = nullptr;
        void* result = invoke(method, self, args, &exception);
        if (exception) failed = true;
        return result;
    }

    template<class T>
    bool Value(void* method, void* self, T& value, void** args = nullptr) {
        void* boxed = Call(method, self, args);
        return boxed && SDK::ReadSafe(unbox(boxed), value);
    }

    bool Alive(void* object) {
        if (!object) return false;
        bool alive = false;
        void* args[]{object};
        if (!Value(aliveMethod, nullptr, alive, args)) failed = true;
        return alive;
    }

    bool Instance(void* object, void* type) {
        return Alive(object) && type && assignable(type, SDK::object_get_class(object));
    }

    bool Finite(Vector3 value) {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) &&
            std::fabs(value.x) < 100000 && std::fabs(value.y) < 100000 && std::fabs(value.z) < 100000;
    }

    void* Find(void* klass) {
        void* type = SDK::type_get_object(SDK::class_get_type(klass));
        if (!type) { failed = true; return nullptr; }
        void* args[]{type};
        return Call(findMethod, nullptr, args);
    }

    Target Capture(void* local) {
        Target target;
        target.local = local;
        if (!Instance(local, localType)) return target;
        uint8_t dead = 0, teleporting = 0;
        bool enabled = false;
        target.reason = "Local player physics unavailable.";
        if (!Field(local, 0x38, dead) || dead > 1 ||
            !Field(local, 0xAE, teleporting) || teleporting > 1 ||
            !Field(local, 0x88, target.physics) || !Alive(target.physics)) return target;
        if (dead) { target.reason = "Unavailable while dead."; return target; }
        if (teleporting) { target.reason = "A native teleport is already running."; return target; }
        if (!Value(enabledMethod, target.physics, enabled) || !enabled) return target;
        target.owner = Find(multiplayerType);
        size_t offset = 0x28;
        if (!Alive(target.owner) && !failed) {
            target.owner = Find(mainType);
            offset = 0x38;
        }
        target.reason = "Truck/lobby spawn unavailable.";
        void* list = nullptr;
        void* items = nullptr;
        int count = 0;
        uintptr_t capacity = 0;
        if (failed || !Alive(target.owner) || !Field(target.owner, offset, list) ||
            !Field(list, 0x18, count) || count <= 0 || count > 64 ||
            !Field(list, 0x10, items) || !Field(items, 0x18, capacity) ||
            capacity < static_cast<uintptr_t>(count) || capacity > 64) return target;
        for (int index = 0; index < count; ++index) {
            void* anchor = nullptr;
            if (!Field(items, 0x20 + index * sizeof(void*), anchor)) return target;
            if (!Instance(anchor, transformType)) continue;
            if (Value(positionMethod, anchor, target.position) && Finite(target.position)) {
                target.reason = nullptr;
                return target;
            }
            if (failed) return target;
        }
        return target;
    }

    bool RestoreNative() {
        if (!restoration.active) return true;
        bool restored = true;
        if (Alive(restoration.physics)) {
            bool enabled = true, actual = false;
            void* args[]{&enabled};
            Call(setEnabledMethod, restoration.physics, args);
            restored = Value(enabledMethod, restoration.physics, actual) && actual;
        }
        if (Alive(restoration.local))
            restored = SDK::WriteSafe(static_cast<char*>(restoration.local) + 0xAE, uint8_t{0}) && restored;
        if (!restored || failed) return false;
        if (restoration.physicsRoot) { unpin(restoration.physicsRoot); restoration.physicsRoot = 0; }
        if (restoration.localRoot) { unpin(restoration.localRoot); restoration.localRoot = 0; }
        restoration = {};
        return true;
    }

    bool Teleport(const Target& target) {
        restoration.local = target.local;
        restoration.physics = target.physics;
        restoration.active = true;
        restoration.localRoot = pin(target.local, false);
        restoration.physicsRoot = pin(target.physics, false);
        if (!restoration.localRoot || !restoration.physicsRoot) {
            failed = true;
            return false;
        }
        Vector3 position = target.position;
        void* args[]{&position};
        Call(teleportMethod, target.local, args);
        return !failed;
    }

    enum class Operation { Capture, Teleport, Restore };
    Target captured;
    bool Guard(Operation operation, void* local = nullptr) {
        failed = false;
        __try {
            switch (operation) {
            case Operation::Capture: captured = Capture(local); return !failed;
            case Operation::Teleport: return Teleport(captured);
            case Operation::Restore: return RestoreNative();
            }
        }
        __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION
            ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
            failed = true;
        }
        return false;
    }

    void Message(const char* text) {
        if (status.message == text) return;
        status.message = text;
        Logger::Log("[RECOVERY] %s", text);
    }

    bool SupportedBuild(void* module) {
        IMAGE_DOS_HEADER dos{};
        IMAGE_NT_HEADERS64 nt{};
        return SDK::ReadSafe(module, dos) && dos.e_magic == IMAGE_DOS_SIGNATURE &&
            dos.e_lfanew > 0 && dos.e_lfanew < 0x10000 &&
            Field(module, dos.e_lfanew, nt) && nt.Signature == IMAGE_NT_SIGNATURE &&
            nt.FileHeader.TimeDateStamp == 0x6AC7D141 && nt.OptionalHeader.SizeOfImage == 0x06EAC000;
    }

    bool InitializeNative(void* image, void* unityImage) {
        HMODULE module = GetModuleHandleW(L"GameAssembly.dll");
        if (!module || !image || !unityImage || !SupportedBuild(module)) return false;
        invoke = reinterpret_cast<decltype(invoke)>(GetProcAddress(module, "il2cpp_runtime_invoke"));
        unbox = reinterpret_cast<decltype(unbox)>(GetProcAddress(module, "il2cpp_object_unbox"));
        pin = reinterpret_cast<decltype(pin)>(GetProcAddress(module, "il2cpp_gchandle_new"));
        unpin = reinterpret_cast<decltype(unpin)>(GetProcAddress(module, "il2cpp_gchandle_free"));
        assignable = reinterpret_cast<decltype(assignable)>(GetProcAddress(module, "il2cpp_class_is_assignable_from"));
        if (!invoke || !unbox || !pin || !unpin || !assignable) return false;
        localType = SDK::class_from_name(image, "", "LocalPlayer");
        mainType = SDK::class_from_name(image, "", "MainManager");
        multiplayerType = SDK::class_from_name(image, "", "MultiplayerController");
        transformType = SDK::class_from_name(unityImage, "UnityEngine", "Transform");
        void* behaviour = SDK::class_from_name(unityImage, "UnityEngine", "Behaviour");
        void* object = SDK::class_from_name(unityImage, "UnityEngine", "Object");
        if (!localType || !mainType || !multiplayerType || !transformType || !behaviour || !object) return false;
        teleportMethod = SDK::get_method(localType, "TeleportPlayer", 1);
        uintptr_t code = 0;
        if (!SDK::ReadSafe(teleportMethod, code) || code != reinterpret_cast<uintptr_t>(module) + 0xBD4DE0 ||
            !TypeAt(localType, 0x88, "PhysicsCharacterController") ||
            !TypeAt(localType, 0x38, "Boolean") || !TypeAt(localType, 0xAE, "Boolean") ||
            !TypeAt(mainType, 0x38, "List`1") || !TypeAt(multiplayerType, 0x28, "List`1")) return false;
        aliveMethod = SDK::get_method(object, "op_Implicit", 1);
        positionMethod = SDK::get_method(transformType, "get_position", 0);
        enabledMethod = SDK::get_method(behaviour, "get_enabled", 0);
        setEnabledMethod = SDK::get_method(behaviour, "set_enabled", 1);
        const auto methods = reinterpret_cast<void* (*)(void*, void**)>(GetProcAddress(module, "il2cpp_class_get_methods"));
        const auto name = reinterpret_cast<const char* (*)(void*)>(GetProcAddress(module, "il2cpp_method_get_name"));
        const auto count = reinterpret_cast<uint32_t (*)(void*)>(GetProcAddress(module, "il2cpp_method_get_param_count"));
        if (!methods || !name || !count) return false;
        void* iterator = nullptr;
        while (void* method = methods(object, &iterator)) {
            if (std::strcmp(name(method), "FindObjectOfType") || count(method) != 1) continue;
            void* parameter = SDK::class_from_type(SDK::method_get_param(method, 0));
            if (parameter && !std::strcmp(SDK::class_get_name(parameter), "Type")) findMethod = method;
        }
        return aliveMethod && positionMethod && enabledMethod && setEnabledMethod && findMethod;
    }
}

void Recovery::Initialize(void* image, void* unityImage) {
    ready = InitializeNative(image, unityImage);
    if (!ready) {
        std::lock_guard<std::mutex> lock(mutex);
        status.reason = "Unsupported recovery metadata; see Logs.";
        Logger::Log("[ERROR] Recovery: native teleport/spawn metadata unavailable.");
    }
}

void Recovery::Update(void* local, bool eligible, uint64_t generation) {
    if (gameThread && gameThread != GetCurrentThreadId()) return;
    gameThread = GetCurrentThreadId();
    std::lock_guard<std::mutex> lock(mutex);
    const uint64_t now = GetTickCount64();
    if (!Guard(Operation::Restore)) {
        healthy = false;
        status.available = false;
        status.restorePending = true;
        Message("Recovery physics restoration pending; unload blocked.");
        return;
    }
    status.restorePending = false;
    if (!request.pending && eligible && local == current.local && generation == currentGeneration &&
        status.sampledAt && now >= status.sampledAt && now - status.sampledAt < 200) return;
    Request queued = request;
    request = {};
    status.pending = false;
    status.sampledAt = now;
    status.available = false;
    if (!ready || !healthy || !eligible) {
        if (ready) status.reason = healthy ? "Release ghost control or wait for the local controller."
            : "Recovery disabled after a native fault; see Logs.";
        if (queued.pending) Message("Recovery cancelled: local controls are unavailable.");
        return;
    }
    if (!Guard(Operation::Capture, local)) {
        healthy = false;
        status.reason = "Native recovery lookup failed; see Logs.";
        Message("Recovery lookup faulted; disabled.");
        return;
    }
    current = captured;
    currentGeneration = generation;
    status.reason = current.reason ? current.reason : "";
    status.available = !current.reason;
    if (!queued.pending) return;
    if (!status.available || queued.local != current.local || queued.owner != current.owner ||
        queued.generation != generation || now < queued.at || now - queued.at > MaxAge) {
        Message("Recovery cancelled: scene/player changed, request expired or spawn unavailable.");
        return;
    }
    if (!Pickup::Restore()) {
        Message("Recovery cancelled: held-object restoration is pending.");
        return;
    }
    const bool teleported = Guard(Operation::Teleport);
    const bool restored = Guard(Operation::Restore);
    status.restorePending = !restored;
    if (!teleported || !restored) {
        healthy = false;
        status.available = false;
        status.reason = "Native recovery failed; see Logs.";
        Message(restored ? "Recovery teleport failed; physics restored."
            : "Recovery failed; physics restoration pending, unload blocked.");
        return;
    }
    Message("Returned to the truck/lobby spawn.");
}

bool Recovery::Reset() {
    if (gameThread && gameThread != GetCurrentThreadId()) return false;
    std::lock_guard<std::mutex> lock(mutex);
    request = {};
    current = {};
    status.available = status.pending = false;
    status.sampledAt = 0;
    const bool restored = Guard(Operation::Restore);
    status.restorePending = !restored;
    if (!restored) Message("Recovery physics restoration pending; unload blocked.");
    return restored;
}

bool Recovery::Queue(uint64_t now) {
    std::lock_guard<std::mutex> lock(mutex);
    if (!status.available || request.pending || status.restorePending || !status.sampledAt ||
        now < status.sampledAt || now - status.sampledAt > MaxAge) {
        Message("Recovery request rejected: unavailable, stale or already pending.");
        return false;
    }
    request = {true, current.local, current.owner, currentGeneration, now};
    status.pending = true;
    Message("Recovery queued.");
    return true;
}

Recovery::Snapshot Recovery::Read() {
    std::lock_guard<std::mutex> lock(mutex);
    return status;
}
