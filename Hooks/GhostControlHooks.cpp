#include "Hooks.h"
#include "../Features/GhostControl.h"
#include "../Utils/Logger.h"
#include "detours.h"
#include <cstring>

namespace {
    using Vec3 = Radar::Vector3;
    using Quaternion = Radar::Quaternion;
    using Getter = void* (*)(void*, void*);
    using BoolGetter = bool (*)(void*, void*);
    using BoolSetter = void (*)(void*, bool, void*);
    using VectorAccess = void (*)(void*, Vec3*, void*);
    using RotationAccess = void (*)(void*, Quaternion*, void*);
    using Navigation = bool (*)(void*, Vec3*, void*);
    using PublicNavigation = bool (*)(void*, Vec3, void*);
    using PublicVectorSetter = void (*)(void*, Vec3, void*);
    struct NavHit { Vec3 position, normal; float distance; int mask, hit; };
    struct PhysicsHit { Vec3 point, normal; uint32_t face; float distance, u, v; int collider; };
    static_assert(sizeof(NavHit) == 36 && sizeof(PhysicsHit) == 44);

    template<typename T> struct Method {
        T call = nullptr;
        void* info = nullptr;
        bool Bind(void* image, const char* space, const char* type, const char* name, int args) {
            void* klass = image ? SDK::class_from_name(image, space, type) : nullptr;
            info = klass ? SDK::get_method(klass, name, args) : nullptr;
            if (info) SDK::ReadSafe(info, call);
            if (!call) Logger::Log("[ERROR] Ghost control: %s.%s unavailable.", type, name);
            return call != nullptr;
        }
    };
    Method<BoolGetter> alive, enabled;
    Method<BoolSetter> setEnabled;
    Method<Getter> getTransform;
    Method<void* (*)(void*, void*, void*)> addComponent;
    Method<VectorAccess> position, setPosition, nextPosition, setNextPosition;
    Method<RotationAccess> rotation, setRotation;
    Method<void (*)(void*, void*, void*)> copyCamera;
    Method<void (*)(void*, void*)> gameObjectConstructor, destroy, resetPath;
    Method<float (*)(void*)> deltaTime;
    Method<int (*)(void*)> frameCount;
    Method<Navigation> destination, warp;
    Method<VectorAccess> destinationProperty;
    Method<PublicNavigation> publicDestination, publicWarp;
    Method<PublicVectorSetter> publicDestinationProperty;
    Method<bool (*)(Vec3*, NavHit*, float, int, void*)> samplePosition;
    Method<bool (*)(Vec3*, Vec3*, NavHit*, int, void*)> navigationRay;
    Method<bool (*)(Vec3, Vec3, PhysicsHit*, int, int, void*)> cameraRay;
    Method<Hooks::GhostAI_ChangeState_t> changeState;
    Hooks::Update_t originalTick = nullptr;
    BoolSetter originalEnabled = nullptr;
    Navigation originalDestination = nullptr, originalWarp = nullptr;
    VectorAccess originalDestinationProperty = nullptr;
    PublicNavigation originalPublicDestination = nullptr, originalPublicWarp = nullptr;
    PublicVectorSetter originalPublicDestinationProperty = nullptr;
    void* (*objectNew)(void*) = nullptr;
    uint32_t (*pin)(void*, bool) = nullptr;
    void (*unpin)(uint32_t) = nullptr;
    void* (*classMethods)(void*, void**) = nullptr;
    void* gameObjectClass = nullptr;
    const void* cameraType = nullptr;
    void* ghostClass = nullptr;
    void* agentClass = nullptr;
    void* floatClass = nullptr;
    bool ready = false, healthy = true, busy = false;
    std::atomic<bool> installed{false};
    std::mutex statusMutex;
    GhostControl::Snapshot status;

    struct Session {
        GhostControl::Context context;
        void* machine = nullptr;
        void* agent = nullptr;
        void* transform = nullptr;
        void* mouse = nullptr;
        void* cameraObject = nullptr;
        void* camera = nullptr;
        void* cameraTransform = nullptr;
        uint32_t objectHandle = 0, cameraHandle = 0, transformHandle = 0, ghostTransformHandle = 0;
        bool active = false, stopping = false, cameraChanged = false, requestingHunt = false;
        bool sourceEnabled = false, huntEnded = false;
        int lastFrame = -1;
        float yaw = 0, pitch = 0;
    } session;
    struct PlayerScope {
        void* player = nullptr;
        void* controller = nullptr;
        uint8_t movement = 0, look = 0;
        bool changed = false;
    } pendingPlayerRestore;
    unsigned playerDepth = 0;

    template<typename T> bool Field(void* object, size_t offset, T& out) {
        return object && SDK::ReadSafe(static_cast<char*>(object) + offset, out);
    }
    template<typename T> bool Write(void* object, size_t offset, const T& value) {
        return object && SDK::WriteSafe(static_cast<char*>(object) + offset, value);
    }
    bool Alive(void* object) {
        return object && alive.call && SDK::IsReadable(object, 0x20) && alive.call(object, alive.info);
    }
    void* FieldType(void* klass, size_t offset) {
        if (!klass) return nullptr;
        void* iterator = nullptr;
        while (void* field = SDK::class_get_fields(klass, &iterator))
            if (SDK::field_get_offset(field) == offset)
                return SDK::class_from_type(SDK::field_get_type(field));
        return nullptr;
    }
    bool Valid(const Vec3& p) {
        return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) &&
            std::fabs(p.x) < 100000 && std::fabs(p.y) < 100000 && std::fabs(p.z) < 100000;
    }
    bool SupportedBuild(void* module) {
        IMAGE_DOS_HEADER dos{};
        IMAGE_NT_HEADERS64 nt{};
        return SDK::ReadSafe(module, dos) && dos.e_magic == IMAGE_DOS_SIGNATURE &&
            dos.e_lfanew > 0 && dos.e_lfanew < 0x10000 &&
            SDK::ReadSafe(static_cast<char*>(module) + dos.e_lfanew, nt) &&
            nt.Signature == IMAGE_NT_SIGNATURE && nt.FileHeader.TimeDateStamp == 0x6AC7D141 &&
            nt.OptionalHeader.SizeOfImage == 0x06EAC000;
    }
    bool Living(void* player) {
        uint8_t dead = 1, dying = 1;
        return Alive(player) && Field(player, 0x28, dead) && Field(player, 0x29, dying) && !dead && !dying;
    }
    bool RestorePlayer(const PlayerScope& scope) {
        __try {
            if (!scope.changed || !Alive(scope.controller) || !Alive(scope.player)) return true;
            uint8_t dead = 0, dying = 0;
            if (!Field(scope.player, 0x28, dead) || !Field(scope.player, 0x29, dying)) return false;
            if (dead || dying) return true;
            return Write(scope.controller, 0x20, scope.movement) && Write(scope.controller, 0x21, scope.look);
        }
        __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
            return false;
        }
    }
    bool FreezePlayer(PlayerScope& scope) {
        __try {
            if (!Field(scope.controller, 0x20, scope.movement) || !Field(scope.controller, 0x21, scope.look))
                return false;
            if (scope.movement > 1 || scope.look > 1) return false;
            scope.changed = true;
            return Write(scope.controller, 0x20, uint8_t{0}) && Write(scope.controller, 0x21, uint8_t{0});
        }
        __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
            return false;
        }
    }
    void Message(const char* message) {
        std::lock_guard<std::mutex> lock(statusMutex);
        if (status.message != message) {
            status.message = message;
            Logger::Log("[CONTROL] %s", message);
        }
    }
    void Fault(const char* message) {
        healthy = false;
        GhostControl::desired.store(false);
        Message(message);
    }
    bool Current(const GhostControl::Context& context) {
        return context.authorized && context.ghost == session.context.ghost &&
            context.player == session.context.player && context.local == session.context.local &&
            context.controller == session.context.controller && context.camera == session.context.camera &&
            context.generation == session.context.generation && Living(context.player);
    }
    bool HuntState(void*& state) {
        void* owner = nullptr;
        void* agent = nullptr;
        if (!Field(session.machine, 0x10, state) || !state ||
            !Field(state, 0x10, owner) || owner != session.context.ghost ||
            !Field(state, 0x18, agent) || agent != session.agent) return false;
        void* klass = SDK::object_get_class(state);
        return FieldType(klass, 0x10) == ghostClass && FieldType(klass, 0x18) == agentClass &&
            FieldType(klass, 0x88) == floatClass && FieldType(klass, 0x8C) == floatClass;
    }
    bool RestoreCore(const GhostControl::Context& context) {
        GhostControl::huntRequested.store(false);
        session.stopping = true;
        if (playerDepth || !RestorePlayer(pendingPlayerRestore)) return false;
        pendingPlayerRestore = {};
        if (session.cameraChanged && Alive(session.context.camera)) {
            originalEnabled(session.context.camera, session.sourceEnabled, setEnabled.info);
        }
        session.cameraChanged = false;
        if (Alive(session.camera)) originalEnabled(session.camera, false, setEnabled.info);
        if (Alive(session.cameraObject)) destroy.call(session.cameraObject, destroy.info);
        if (session.objectHandle) { unpin(session.objectHandle); session.objectHandle = 0; }
        if (session.cameraHandle) { unpin(session.cameraHandle); session.cameraHandle = 0; }
        if (session.transformHandle) { unpin(session.transformHandle); session.transformHandle = 0; }
        if (session.ghostTransformHandle) { unpin(session.ghostTransformHandle); session.ghostTransformHandle = 0; }
        session.camera = session.cameraObject = session.cameraTransform = nullptr;
        // State exit invokes ResetHuntEffects and its authenticated native RPCs.
        // Never end somebody else's hunt after host/session authority changes.
        if (session.active && Alive(session.context.ghost) && context.authorized &&
            context.ghost == session.context.ghost && context.generation == session.context.generation) {
            int state = -1;
            if (!Field(session.context.ghost, 0x30, state)) return false;
            if (state != 0) {
                Hooks::hkGhostAI_ChangeState(session.context.ghost, 0, nullptr, false, changeState.info);
                if (!Field(session.context.ghost, 0x30, state) || state != 0) return false;
            }
        }
        session = {};
        return true;
    }

    bool Start(const GhostControl::Context& context) {
        if (!context.authorized || !Living(context.player) || !Alive(context.controller) || !Alive(context.camera)) {
            Message("Control requires a living local PC player and private/offline host authority.");
            return false;
        }
        session.context = context;
        if (!Field(context.ghost, 0x28, session.machine) || !session.machine ||
            !Field(context.ghost, 0x40, session.agent) || !Alive(session.agent) ||
            !Field(context.controller, 0x38, session.mouse) || !session.mouse) {
            Message("Ghost/controller layout unavailable; control not started.");
            return false;
        }
        session.transform = getTransform.call(context.ghost, getTransform.info);
        Quaternion facing;
        if (!Alive(session.transform)) {
            Message("Ghost transform unavailable; control not started.");
            return false;
        }
        session.ghostTransformHandle = pin(session.transform, false);
        if (!session.ghostTransformHandle) {
            Message("Could not root the controlled ghost transform.");
            return false;
        }
        rotation.call(session.transform, &facing, rotation.info);
        if (!Radar::Heading(facing, session.yaw)) {
            Message("Ghost heading unavailable; control not started.");
            return false;
        }
        int state = -1;
        if (!Field(context.ghost, 0x30, state)) {
            Message("Ghost state unreadable; control not started.");
            return false;
        }
        if (state != 0 && state != 2)
            Hooks::hkGhostAI_ChangeState(context.ghost, 0, nullptr, false, changeState.info);
        if (!Field(context.ghost, 0x30, state) || (state != 0 && state != 2)) {
            Message("Could not pause the native ghost state; control not started.");
            return false;
        }
        session.active = true;
        void* hunt = nullptr;
        if (state == 2 && !HuntState(hunt)) {
            Message("Hunt state layout unsupported; restoring native control.");
            return false;
        }
        session.cameraObject = objectNew(gameObjectClass);
        if (!session.cameraObject || !(session.objectHandle = pin(session.cameraObject, false))) {
            Message("Could not allocate/root the temporary camera GameObject.");
            return false;
        }
        gameObjectConstructor.call(session.cameraObject, gameObjectConstructor.info);
        void* type = SDK::type_get_object(cameraType);
        session.camera = type ? addComponent.call(session.cameraObject, type, addComponent.info) : nullptr;
        if (!Alive(session.camera) || !(session.cameraHandle = pin(session.camera, false))) {
            Message("Could not create/root the temporary camera component.");
            return false;
        }
        session.cameraTransform = getTransform.call(session.camera, getTransform.info);
        if (!Alive(session.cameraTransform) || !(session.transformHandle = pin(session.cameraTransform, false))) {
            Message("Could not create/root the temporary camera transform.");
            return false;
        }
        copyCamera.call(session.camera, context.camera, copyCamera.info);
        session.sourceEnabled = enabled.call(context.camera, enabled.info);
        session.cameraChanged = true;
        originalEnabled(context.camera, false, setEnabled.info);
        originalEnabled(session.camera, true, setEnabled.info);
        resetPath.call(session.agent, resetPath.info);
        Message(state == 2 ? "Controlling the existing hunt; native kill rules remain active." :
            "Controlling ghost without hunting. Use Start controlled hunt to initiate a hunt.");
        return true;
    }

    void StartControlledHunt() {
        int state = -1;
        if (!session.active || session.stopping ||
            !Field(session.context.ghost, 0x30, state) || state == 2) {
            Message("Hunt request rejected: active non-hunting control is required.");
            return;
        }
        session.requestingHunt = true;
        __try {
            Hooks::hkGhostAI_ChangeState(session.context.ghost, 2, nullptr, true, changeState.info);
        }
        __finally {
            session.requestingHunt = false;
        }
        if (!Field(session.context.ghost, 0x30, state) || state != 2) {
            Message("The game rejected hunt entry; non-hunting control remains active.");
            return;
        }
        void* hunt = nullptr;
        if (!HuntState(hunt)) {
            Fault("Controlled hunt layout unsupported; releasing control.");
            return;
        }
        resetPath.call(session.agent, resetPath.info);
        session.huntEnded = false;
        Message("Controlled hunt initiated; native grace, kills and multikill rules remain active.");
    }

    bool Frame() {
        const int frame = frameCount.call(frameCount.info);
        if (frame == session.lastFrame) return true;
        session.lastFrame = frame;
        float dt = deltaTime.call(deltaTime.info);
        float horizontal = 0, vertical = 0, lookX = 0, lookY = 0, sensitivityX = 0, sensitivityY = 0;
        uint8_t movementEnabled = 0, lookEnabled = 0;
        if (!std::isfinite(dt) || dt < 0 ||
            !Field(session.context.controller, 0x20, movementEnabled) || movementEnabled > 1 ||
            !Field(session.context.controller, 0x21, lookEnabled) || lookEnabled > 1 ||
            !Field(session.context.controller, 0x70, horizontal) ||
            !Field(session.context.controller, 0x74, vertical) ||
            !Field(session.mouse, 0xB0, lookX) || !Field(session.mouse, 0xB4, lookY) ||
            !Field(session.mouse, 0x10, sensitivityX) || !Field(session.mouse, 0x14, sensitivityY) ||
            !std::isfinite(lookX) || !std::isfinite(lookY) ||
            !std::isfinite(horizontal) || !std::isfinite(vertical) ||
            !std::isfinite(GhostControl::speed.load()) || !std::isfinite(GhostControl::cameraDistance.load()) ||
            !std::isfinite(sensitivityX) || !std::isfinite(sensitivityY)) return false;
        const bool captured = GhostControl::inputCaptured.load();
        constexpr float radians = 0.017453292519943295f;
        if (!captured && lookEnabled) {
            session.yaw = std::remainder(session.yaw + lookX * sensitivityX * radians, 6.28318530718f);
            session.pitch = std::clamp(session.pitch - lookY * sensitivityY * radians, -1.3f, 1.3f);
        }
        if (!std::isfinite(session.yaw) || !std::isfinite(session.pitch)) return false;
        bool mayMove = !captured && movementEnabled;
        int state = -1;
        if (!Field(session.context.ghost, 0x30, state)) return false;
        if (state == 2) {
            void* hunt = nullptr;
            float grace = 0;
            if (!HuntState(hunt) || !Field(hunt, 0x8C, grace) || !std::isfinite(grace)) return false;
            mayMove = mayMove && grace < 0;
        }
        Vec3 ghostPosition, navigationPosition;
        position.call(session.transform, &ghostPosition, position.info);
        nextPosition.call(session.agent, &navigationPosition, nextPosition.info);
        if (!Valid(ghostPosition) || !Valid(navigationPosition)) return false;
        const Vec3 offset{ghostPosition.x - navigationPosition.x, ghostPosition.y - navigationPosition.y,
            ghostPosition.z - navigationPosition.z};
        const Vec3 step = GhostControl::Step(mayMove ? horizontal : 0, mayMove ? vertical : 0,
            session.yaw, GhostControl::speed.load(), dt);
        if (step.x || step.z) {
            Vec3 target{navigationPosition.x + step.x, navigationPosition.y, navigationPosition.z + step.z};
            NavHit sampled{}, edge{};
            if (samplePosition.call(&target, &sampled, 0.4f, -1, samplePosition.info) && Valid(sampled.position)) {
                target = sampled.position;
                if (navigationRay.call(&navigationPosition, &target, &edge, -1, navigationRay.info))
                    target = edge.position;
                if (!Valid(target)) return false;
                setNextPosition.call(session.agent, &target, setNextPosition.info);
                nextPosition.call(session.agent, &navigationPosition, nextPosition.info);
                if (!Valid(navigationPosition)) return false;
                ghostPosition = {navigationPosition.x + offset.x, navigationPosition.y + offset.y,
                    navigationPosition.z + offset.z};
                setPosition.call(session.transform, &ghostPosition, setPosition.info);
            }
        }
        Quaternion heading = GhostControl::ViewRotation(session.yaw, 0);
        setRotation.call(session.transform, &heading, setRotation.info);
        Quaternion view = GhostControl::ViewRotation(session.yaw, session.pitch);
        const Vec3 forward{std::sin(session.yaw) * std::cos(session.pitch), -std::sin(session.pitch),
            std::cos(session.yaw) * std::cos(session.pitch)};
        Vec3 eye{ghostPosition.x, ghostPosition.y + 1.6f, ghostPosition.z};
        const bool first = GhostControl::firstPerson.load();
        const float distance = first ? -0.25f : std::clamp(GhostControl::cameraDistance.load(), 0.75f, 5.0f);
        Vec3 cameraPosition{eye.x - forward.x * distance, eye.y - forward.y * distance,
            eye.z - forward.z * distance};
        if (!first) {
            PhysicsHit hit{};
            if (cameraRay.call(eye, cameraPosition, &hit, -5, 1, cameraRay.info)) {
                if (!std::isfinite(hit.distance)) return false;
                const float safeDistance = std::clamp(hit.distance - 0.15f, 0.05f, distance);
                cameraPosition = {eye.x - forward.x * safeDistance, eye.y - forward.y * safeDistance,
                    eye.z - forward.z * safeDistance};
            }
        }
        setPosition.call(session.cameraTransform, &cameraPosition, setPosition.info);
        setRotation.call(session.cameraTransform, &view, setRotation.info);
        return true;
    }

    void UpdateCore(const GhostControl::Context& context) {
        if (session.active && (!Current(context) || !GhostControl::desired.load() || !healthy)) {
            GhostControl::desired.store(false);
            if (!RestoreCore(context)) { Message("Control restoration pending; unload blocked."); return; }
            Message("Native player camera, input, and ghost AI restored.");
        }
        if (session.stopping && !RestoreCore(context)) return;
        if (!GhostControl::desired.load()) {
            if (GhostControl::huntRequested.exchange(false))
                Message("Hunt request rejected: ghost control is not active.");
            return;
        }
        if (!ready || !healthy || !installed) {
            GhostControl::desired.store(false);
            Message("Ghost control unavailable; see initialization/error logs.");
            return;
        }
        if (!session.active && !Start(context)) {
            GhostControl::desired.store(false);
            Logger::Log("[ERROR] Ghost control could not start; restoring any acquired camera/hunt state.");
            if (!RestoreCore(context)) Message("Failed start: restoration pending; unload blocked.");
            return;
        }
        if (GhostControl::huntRequested.exchange(false)) StartControlledHunt();
        if (!healthy) { RestoreCore(context); return; }
        if (!Frame()) {
            Fault("Control sampling/movement failed; restoring native state.");
            RestoreCore(context);
        }
    }
    void Publish(const GhostControl::Context& context) {
        const bool available = ready && healthy && installed && context.authorized && Living(context.player);
        int state = -1;
        const bool hunting = session.active && Field(session.context.ghost, 0x30, state) && state == 2;
        std::lock_guard<std::mutex> lock(statusMutex);
        status.sampledAt = GetTickCount64();
        status.available = available;
        status.active = session.active && !session.stopping;
        status.restoring = session.stopping;
        status.hunting = status.active && hunting;
    }
    bool TickCore(void* machine) {
        if (!session.active || session.stopping || machine != session.machine) return true;
        const auto context = Hooks::ReadHostContext();
        if (!Current(context) || !GhostControl::desired.load()) {
            GhostControl::desired.store(false);
            RestoreCore(context);
            return true;
        }
        int state = -1;
        if (!Field(context.ghost, 0x30, state)) {
            Fault("Ghost state unreadable; releasing control.");
            return true;
        }
        if (state != 2) {
            if (!session.huntEnded) {
                session.huntEnded = true;
                Message("Ghost AI paused; non-hunting control remains active. A hunt can be started separately.");
            }
            return false;
        }
        void* hunt = nullptr;
        float duration = 0, grace = 0;
        const float dt = deltaTime.call(deltaTime.info);
        // Only cancel the ordinary countdown. A native nonpositive timer or kill/end
        // condition must still terminate the hunt; grace and all other timers run.
        if (!HuntState(hunt) || !Field(hunt, 0x88, duration) || !Field(hunt, 0x8C, grace) ||
            !std::isfinite(duration) || !std::isfinite(grace) ||
            !std::isfinite(dt) || dt < 0 ||
            (duration > 0 && grace < 0 && !Write(hunt, 0x88, duration + dt))) {
            Fault("Hunt timer layout unsupported; releasing control.");
            GhostControl::Restore();
        }
        return true;
    }
    void Tick(void* machine, void* info) {
        bool run = true;
        __try { run = TickCore(machine); }
        __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
            Fault("Native control tick faulted; overrides disabled, restoration required.");
        }
        if (run) originalTick(machine, info);
        __try {
            if (session.active && !session.stopping && machine == session.machine && Alive(session.transform)) {
                Quaternion heading = GhostControl::ViewRotation(session.yaw, 0);
                setRotation.call(session.transform, &heading, setRotation.info);
            }
        }
        __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
            Fault("Controlled facing update faulted; releasing ghost control.");
        }
    }
    bool SetDestination(void* agent, Vec3* target, void* info) {
        if (session.active && !session.stopping && agent == session.agent) return false;
        return originalDestination(agent, target, info);
    }
    bool Warp(void* agent, Vec3* target, void* info) {
        if (session.active && !session.stopping && agent == session.agent) return false;
        return originalWarp(agent, target, info);
    }
    void SetDestinationProperty(void* agent, Vec3* target, void* info) {
        if (session.active && !session.stopping && agent == session.agent) return;
        originalDestinationProperty(agent, target, info);
    }
    // IL2CPP's public wrappers call the engine icall directly, bypassing _Injected.
    bool SetPublicDestination(void* agent, Vec3 target, void* info) {
        if (session.active && !session.stopping && agent == session.agent) return false;
        return originalPublicDestination(agent, target, info);
    }
    bool PublicWarp(void* agent, Vec3 target, void* info) {
        if (session.active && !session.stopping && agent == session.agent) return false;
        return originalPublicWarp(agent, target, info);
    }
    void SetPublicDestinationProperty(void* agent, Vec3 target, void* info) {
        if (session.active && !session.stopping && agent == session.agent) return;
        originalPublicDestinationProperty(agent, target, info);
    }
    void SetEnabled(void* component, bool value, void* info) {
        if (session.active && !session.stopping && session.cameraChanged && component == session.context.camera) {
            session.sourceEnabled = value;
            value = false;
        }
        originalEnabled(component, value, info);
    }
}

void GhostControl::Initialize(void* image, void* unityImage, void* navigationImage, void* physicsImage) {
    HMODULE module = GetModuleHandleW(L"GameAssembly.dll");
    if (!module || !image || !unityImage || !navigationImage || !physicsImage || !SupportedBuild(module)) {
        Message("Installed binary does not match the recovered control build, or a Unity module is missing; control disabled.");
        return;
    }
    const uintptr_t base = reinterpret_cast<uintptr_t>(module);
    objectNew = reinterpret_cast<decltype(objectNew)>(GetProcAddress(module, "il2cpp_object_new"));
    pin = reinterpret_cast<decltype(pin)>(GetProcAddress(module, "il2cpp_gchandle_new"));
    unpin = reinterpret_cast<decltype(unpin)>(GetProcAddress(module, "il2cpp_gchandle_free"));
    classMethods = reinterpret_cast<decltype(classMethods)>(GetProcAddress(module, "il2cpp_class_get_methods"));
    if (!objectNew || !pin || !unpin || !classMethods || !SDK::class_get_fields ||
        !SDK::field_get_type || !SDK::class_from_type || !SDK::class_value_size) {
        Message("Required IL2CPP exports unavailable; ghost control disabled.");
        return;
    }
    ghostClass = SDK::class_from_name(image, "", "GhostAI");
    agentClass = SDK::class_from_name(navigationImage, "UnityEngine.AI", "NavMeshAgent");
    gameObjectClass = SDK::class_from_name(unityImage, "UnityEngine", "GameObject");
    void* cameraClass = SDK::class_from_name(unityImage, "UnityEngine", "Camera");
    cameraType = cameraClass ? SDK::class_get_type(cameraClass) : nullptr;
    void* machineClass = FieldType(ghostClass, 0x28);
    floatClass = FieldType(ghostClass, 0xC8);
    bool valid = module && agentClass && gameObjectClass && cameraType && floatClass &&
        FieldType(ghostClass, 0x40) == agentClass;
    valid &= changeState.Bind(image, "", "GhostAI", "ChangeState", 3);
    // Obfuscated state-machine code is build-specific; do not reuse it after updates.
    valid &= reinterpret_cast<uintptr_t>(changeState.call) == base + 0x2021290;
    void* iterator = nullptr;
    while (void* method = machineClass ? classMethods(machineClass, &iterator) : nullptr) {
        Hooks::Update_t code = nullptr;
        if (SDK::ReadSafe(method, code) && reinterpret_cast<uintptr_t>(code) == base + 0x21CCE90)
            originalTick = code;
    }
    valid &= originalTick != nullptr;
    valid &= alive.Bind(unityImage, "UnityEngine", "Object", "op_Implicit", 1);
    valid &= enabled.Bind(unityImage, "UnityEngine", "Behaviour", "get_enabled", 0);
    valid &= setEnabled.Bind(unityImage, "UnityEngine", "Behaviour", "set_enabled", 1);
    valid &= getTransform.Bind(unityImage, "UnityEngine", "Component", "get_transform", 0);
    valid &= position.Bind(unityImage, "UnityEngine", "Transform", "get_position_Injected", 1);
    valid &= setPosition.Bind(unityImage, "UnityEngine", "Transform", "set_position_Injected", 1);
    valid &= rotation.Bind(unityImage, "UnityEngine", "Transform", "get_rotation_Injected", 1);
    valid &= setRotation.Bind(unityImage, "UnityEngine", "Transform", "set_rotation_Injected", 1);
    valid &= nextPosition.Bind(navigationImage, "UnityEngine.AI", "NavMeshAgent", "get_nextPosition_Injected", 1);
    valid &= setNextPosition.Bind(navigationImage, "UnityEngine.AI", "NavMeshAgent", "set_nextPosition_Injected", 1);
    valid &= resetPath.Bind(navigationImage, "UnityEngine.AI", "NavMeshAgent", "ResetPath", 0);
    valid &= destination.Bind(navigationImage, "UnityEngine.AI", "NavMeshAgent", "SetDestination_Injected", 1);
    valid &= destinationProperty.Bind(navigationImage, "UnityEngine.AI", "NavMeshAgent", "set_destination_Injected", 1);
    valid &= warp.Bind(navigationImage, "UnityEngine.AI", "NavMeshAgent", "Warp_Injected", 1);
    valid &= publicDestination.Bind(navigationImage, "UnityEngine.AI", "NavMeshAgent", "SetDestination", 1);
    valid &= publicDestinationProperty.Bind(navigationImage, "UnityEngine.AI", "NavMeshAgent", "set_destination", 1);
    valid &= publicWarp.Bind(navigationImage, "UnityEngine.AI", "NavMeshAgent", "Warp", 1);
    valid &= samplePosition.Bind(navigationImage, "UnityEngine.AI", "NavMesh", "SamplePosition_Injected", 4);
    valid &= navigationRay.Bind(navigationImage, "UnityEngine.AI", "NavMesh", "Raycast_Injected", 4);
    valid &= cameraRay.Bind(physicsImage, "UnityEngine", "Physics", "Linecast", 5);
    valid &= copyCamera.Bind(unityImage, "UnityEngine", "Camera", "CopyFrom", 1);
    valid &= gameObjectConstructor.Bind(unityImage, "UnityEngine", "GameObject", ".ctor", 0);
    valid &= addComponent.Bind(unityImage, "UnityEngine", "GameObject", "Internal_AddComponentWithType", 1);
    valid &= destroy.Bind(unityImage, "UnityEngine", "Object", "Destroy", 1);
    valid &= deltaTime.Bind(unityImage, "UnityEngine", "Time", "get_deltaTime", 0);
    valid &= frameCount.Bind(unityImage, "UnityEngine", "Time", "get_frameCount", 0);
    void* controllerClass = SDK::class_from_name(image, "", "FirstPersonController");
    void* localClass = SDK::class_from_name(image, "", "LocalPlayer");
    void* pcClass = SDK::class_from_name(image, "", "LocalPCPlayer");
    void* playerClass = SDK::class_from_name(image, "", "Player");
    void* mouseClass = SDK::class_from_name(image, "", "MouseLook");
    valid &= FieldType(pcClass, 0x128) == controllerClass && FieldType(localClass, 0x30) == cameraClass &&
        FieldType(localClass, 0x20) == playerClass && FieldType(controllerClass, 0x38) == mouseClass;
    void* booleanClass = FieldType(playerClass, 0x28);
    void* vectorClass = SDK::class_from_name(unityImage, "UnityEngine", "Vector2");
    valid &= booleanClass && vectorClass && FieldType(controllerClass, 0x20) == booleanClass &&
        FieldType(controllerClass, 0x21) == booleanClass && FieldType(controllerClass, 0x70) == vectorClass &&
        FieldType(mouseClass, 0x10) == floatClass && FieldType(mouseClass, 0x14) == floatClass &&
        FieldType(mouseClass, 0xB0) == floatClass && FieldType(mouseClass, 0xB4) == floatClass;
    uint32_t alignment = 0;
    void* navHit = SDK::class_from_name(navigationImage, "UnityEngine.AI", "NavMeshHit");
    void* physicsHit = SDK::class_from_name(physicsImage, "UnityEngine", "RaycastHit");
    valid &= navHit && SDK::class_value_size(navHit, &alignment) == sizeof(NavHit);
    valid &= physicsHit && SDK::class_value_size(physicsHit, &alignment) == sizeof(PhysicsHit);
    originalEnabled = setEnabled.call;
    originalDestination = destination.call;
    originalWarp = warp.call;
    originalDestinationProperty = destinationProperty.call;
    originalPublicDestination = publicDestination.call;
    originalPublicWarp = publicWarp.call;
    originalPublicDestinationProperty = publicDestinationProperty.call;
    ready = valid;
    Message(ready ? "Ghost control ready (experimental; not live-tested)." :
        "Unsupported build or missing camera/navigation methods; ghost control disabled.");
}

long GhostControl::InstallHooks(bool attach) {
    if (!ready) return NO_ERROR;
    LONG error = NO_ERROR;
    const auto hook = [attach, &error](PVOID* original, PVOID replacement) {
        const LONG result = attach ? DetourAttach(original, replacement) : DetourDetach(original, replacement);
        if (result != NO_ERROR) error = result;
    };
    hook(reinterpret_cast<PVOID*>(&originalTick), Tick);
    hook(reinterpret_cast<PVOID*>(&originalDestination), SetDestination);
    hook(reinterpret_cast<PVOID*>(&originalWarp), Warp);
    hook(reinterpret_cast<PVOID*>(&originalDestinationProperty), SetDestinationProperty);
    hook(reinterpret_cast<PVOID*>(&originalPublicDestination), SetPublicDestination);
    hook(reinterpret_cast<PVOID*>(&originalPublicWarp), PublicWarp);
    hook(reinterpret_cast<PVOID*>(&originalPublicDestinationProperty), SetPublicDestinationProperty);
    hook(reinterpret_cast<PVOID*>(&originalEnabled), SetEnabled);
    return error;
}

void GhostControl::SetHooksInstalled(bool value) { installed = value; }

void GhostControl::Update(const Context& context) {
    if (busy) return;
    busy = true;
    __try { UpdateCore(context); Publish(context); }
    __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        Fault("Ghost control raised an access violation; disabled. Restoration must complete before unload.");
    }
    busy = false;
}

bool GhostControl::Restore() {
    desired.store(false);
    __try {
        const bool restored = RestoreCore(Hooks::ReadHostContext());
        if (!restored) Message("Control restoration pending; unload blocked.");
        return restored;
    }
    __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        Fault("Ghost control restoration faulted; unload blocked until restoration succeeds.");
        return false;
    }
}

bool GhostControl::ControlsPlayer(void* controller) {
    return session.active && !session.stopping && controller == session.context.controller;
}

bool GhostControl::BlocksInput(void* instance, bool door) {
    void* local = nullptr;
    return session.active && !session.stopping && Field(instance, door ? 0x58 : 0x20, local) &&
        local == session.context.local;
}

bool GhostControl::AllowTransition(void* ghost, int state) {
    if (!session.active || session.stopping || ghost != session.context.ghost) return true;
    if (state == 2) return session.requestingHunt;
    int current = -1;
    return state != 2 && Field(ghost, 0x30, current) && current == 2;
}

void GhostControl::PlayerUpdate(UpdateFunction original, void* controller, void* info) {
    if (!ControlsPlayer(controller)) { original(controller, info); return; }
    PlayerScope scope{session.context.player, controller};
    if (!FreezePlayer(scope)) {
        if (!RestorePlayer(scope)) pendingPlayerRestore = scope;
        Fault("Could not freeze/restore the original player; releasing ghost control.");
        Restore();
        return;
    }
    ++playerDepth;
    __try {
        original(controller, info);
    }
    __finally {
        --playerDepth;
        if (!RestorePlayer(scope)) {
            pendingPlayerRestore = scope;
            Fault("Player movement restoration failed; ghost control disabled.");
        }
    }
}

GhostControl::Snapshot GhostControl::Read() {
    std::lock_guard<std::mutex> lock(statusMutex);
    return status;
}
