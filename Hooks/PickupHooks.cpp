#include "../Features/Pickup.h"
#include "../SDK/IL2CPP.h"
#include "../Utils/Logger.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_set>
#include <stdexcept>

// Build 25811160 metadata31; static REA evidence:
// ev_262837fa5c84178382c3365049812fa0ae43a0d97b2d427c9bd93ea50ba2c6c5
// ev_864706b650ad65bec10ef1f6056634529974277c00a607d540df212dcf956739
// ev_12fa320c401aeffa125d1830333beaa8460e7aa3494da9aee2a0f4d1f0a92f5e
// ev_002be86bf1db7b9198189551ef119a589c8c70b8c672c6c32957d04648ec9f6c
// ev_5e3382ccf47aaee60ddccbf51dba75045d1d594297c173c85b72ee40775525db
// Runtime metadata binding, not these RVAs, determines every invoked method.
namespace {
    struct Vector3 { float x, y, z; };
    struct Quaternion { float x, y, z, w; };
    struct Method { void* info = nullptr; };
    using Invoke = void* (*)(void*, void*, void**, void**);
    using Unbox = void* (*)(void*);
    using Enumerate = void* (*)(void*, void**);
    using MethodName = const char* (*)(void*);
    using ParamCount = uint32_t (*)(void*);
    using HandleNew = uint32_t (*)(void*, bool);
    using HandleFree = void (*)(uint32_t);
    using FieldFind = void* (*)(void*, const char*);
    using FieldRead = void (*)(void*, void*, void*);
    using ObjectClass = void* (*)(void*);
    using Assignable = bool (*)(void*, void*);

    Invoke invoke = nullptr;
    Unbox unbox = nullptr;
    Enumerate methods = nullptr, nestedTypes = nullptr;
    MethodName methodName = nullptr;
    ParamCount paramCount = nullptr;
    HandleNew handleNew = nullptr;
    HandleFree handleFree = nullptr;
    FieldFind fieldFind = nullptr;
    FieldRead fieldRead = nullptr;
    ObjectClass objectClass = nullptr;
    Assignable assignable = nullptr;
    Method aliveMethod, transformMethod, gameObjectMethod, parentMethod, childCountMethod;
    Method positionMethod, setPositionMethod, rotationMethod, raycastMethod;
    Method colliderMethod, hitPointMethod, hitDistanceMethod, bodyMethod;
    Method componentMethod, componentsMethod, mineMethod, childMethod, requestMethod, enabledMethod;
    Method getKinematicMethod, setKinematicMethod, phaseMethod;
    void* photonClass = nullptr;
    void* rigidbodyClass = nullptr;
    void* syncClass = nullptr;
    void* bodySyncClass = nullptr;
    void* playerClass = nullptr;
    void* localClass = nullptr;
    void* ghostClass = nullptr;
    void* equipmentClass = nullptr;
    bool ready = false, healthy = true, callFailed = false;
    const char* message = "Experimental pickup is off.";
    std::mutex statusMutex;
    Pickup::Snapshot status;
    Pickup::Detail::Lease lease;
    void* camera = nullptr;
    void* player = nullptr;
    void* avatar = nullptr;
    bool allowed = false, captured = true;
    uint64_t generation = 0, frameAt = 0;
    float distance = 1.5f;
    Vector3 pivotOffset{};
    uint32_t transformHandle = 0, cameraHandle = 0, playerHandle = 0, avatarHandle = 0;
    std::vector<uint32_t> bodyHandles, viewHandles;
    uint64_t requestedAt = 0;
    bool ownershipRequested = false;
    DWORD gameThread = 0;
    void* initImages[4]{};
    void* inputContext = nullptr;

    template<class T>
    bool Export(HMODULE module, const char* name, T& destination) {
        destination = reinterpret_cast<T>(GetProcAddress(module, name));
        return destination != nullptr;
    }

    bool Bind(Method& method, void* klass, const char* name, uint32_t arguments,
        const char* firstParameter = nullptr) {
        method.info = nullptr;
        if (!klass) return false;
        void* iterator = nullptr;
        while (void* entry = methods(klass, &iterator)) {
            if (std::strcmp(methodName(entry), name) != 0 || paramCount(entry) != arguments) continue;
            if (firstParameter) {
                void* type = SDK::class_from_type(SDK::method_get_param(entry, 0));
                const char* actual = type ? SDK::class_get_name(type) : nullptr;
                if (!actual || std::strcmp(actual, firstParameter) != 0) continue;
            }
            if (method.info) return false;
            method.info = entry;
        }
        if (!method.info) Logger::Log("[ERROR] Pickup: method %s unavailable.", name);
        return method.info != nullptr;
    }

    void* Call(const Method& method, void* self = nullptr, void** args = nullptr) {
        if (callFailed) return nullptr;
        if (!method.info) {
            callFailed = true;
            return nullptr;
        }
        void* exception = nullptr;
        void* result = nullptr;
        __try {
            result = invoke(method.info, self, args, &exception);
        }
        __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION
            ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
            callFailed = true;
            return nullptr;
        }
        if (exception) {
            callFailed = true;
            Logger::Log("[ERROR] Pickup: managed exception; experimental pickup disabled.");
        }
        return result;
    }

    template<class T>
    bool Value(const Method& method, void* self, T& value, void** args = nullptr) {
        void* boxed = Call(method, self, args);
        if (callFailed) return false;
        bool read = false;
        __try {
            void* storage = boxed ? unbox(boxed) : nullptr;
            read = SDK::ReadSafe(storage, value);
        }
        __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION
            ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) { read = false; }
        if (!read) {
            callFailed = true;
            return false;
        }
        return true;
    }

    uint32_t Root(void* object) {
        __try { return handleNew(object, false); }
        __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION
            ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
            callFailed = true;
            return 0;
        }
    }

    bool FreeRoot(uint32_t& handle) {
        if (!handle) return true;
        __try {
            handleFree(handle);
            handle = 0;
            return true;
        }
        __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION
            ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
            callFailed = true;
            return false;
        }
    }

    bool Alive(void* object, bool& result) {
        result = false;
        if (!object) return true;
        void* args[]{object};
        return Value(aliveMethod, nullptr, result, args);
    }

    bool Living(void* object) {
        bool result = false;
        return Alive(object, result) && result;
    }

    void* RuntimeType(void* klass) {
        if (!klass) return nullptr;
        __try { return SDK::type_get_object(SDK::class_get_type(klass)); }
        __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION
            ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) { return nullptr; }
    }

    void* Component(void* transform, void* klass) {
        void* type = RuntimeType(klass);
        if (!type) {
            callFailed = true;
            return nullptr;
        }
        void* args[]{type};
        return Call(componentMethod, transform, args);
    }

    template<class T>
    bool Field(void* object, const char* name, T& result) {
        if (!object || !fieldFind || !fieldRead || !objectClass) return false;
        __try {
            void* field = fieldFind(objectClass(object), name);
            if (!field) return false;
            fieldRead(object, field, &result);
            return true;
        }
        __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION
            ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) { return false; }
    }

    bool OptionalCall(const Method& method, void* self, void** args, void*& result) {
        result = nullptr;
        if (!method.info || !invoke) return false;
        __try {
            void* exception = nullptr;
            result = invoke(method.info, self, args, &exception);
            return !exception;
        }
        __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION
            ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) { return false; }
    }

    template<class T>
    bool OptionalValue(const Method& method, void* self, T& result, void** args = nullptr) {
        void* boxed = nullptr;
        if (!OptionalCall(method, self, args, boxed) || !boxed) return false;
        __try {
            return SDK::ReadSafe(unbox(boxed), result);
        }
        __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION
            ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) { return false; }
    }

    bool OptionalAlive(void* object) {
        bool alive = false;
        void* args[]{object};
        return object && OptionalValue(aliveMethod, nullptr, alive, args) && alive;
    }

    bool ObserverKind(void* observer, bool& transformSync, bool& bodySync) {
        if (!objectClass || !assignable) return false;
        __try {
            void* klass = objectClass(observer);
            transformSync = syncClass && assignable(syncClass, klass);
            bodySync = bodySyncClass && assignable(bodySyncClass, klass);
            return true;
        }
        __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION
            ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) { return false; }
    }

    bool IsPhotonView(void* object) {
        if (!photonClass || !objectClass || !assignable) return false;
        __try { return assignable(photonClass, objectClass(object)); }
        __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION
            ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) { return false; }
    }

    bool PositionObserved(void* view, void* transform) {
        int synchronization = 0, count = 0;
        void* list = nullptr;
        void* items = nullptr;
        if (!Field(view, "Synchronization", synchronization) ||
            !Field(view, "ObservedComponents", list)) return false;
        if (!synchronization || !list) return false;
        if (!Field(list, "_size", count) || !Field(list, "_items", items)) return false;
        if (count < 0 || static_cast<size_t>(count) > Pickup::Detail::MaxElements ||
            (count && !items)) return false;
        uintptr_t capacity = 0;
        if (count && (!SDK::ReadSafe(static_cast<char*>(items) + 0x18, capacity) ||
            static_cast<uintptr_t>(count) > capacity)) return false;
        for (int i = 0; i < count; ++i) {
            void* observer = nullptr;
            if (!SDK::ReadSafe(static_cast<char*>(items) + 0x20 + i * sizeof(void*), observer)) {
                return false;
            }
            if (!observer) continue;
            bool transformSync = false, bodySync = false;
            if (!ObserverKind(observer, transformSync, bodySync)) return false;
            if (!transformSync && !bodySync) continue;
            bool position = false, enabled = false;
            if (!OptionalAlive(observer) || !OptionalValue(enabledMethod, observer, enabled)) return false;
            if (transformSync && !Field(observer, "m_SynchronizePosition", position)) return false;
            if (bodySync) {
                void* body = nullptr;
                if (!Field(observer, "m_Body", body)) return false;
                void* actual = nullptr;
                position = OptionalAlive(body) &&
                    OptionalCall(transformMethod, body, nullptr, actual) && actual == transform;
            }
            void* actual = nullptr;
            if (enabled && position && OptionalCall(transformMethod, observer, nullptr, actual) &&
                actual == transform) return true;
        }
        return false;
    }

    bool InspectViews(void* transform, Pickup::Detail::Candidate& result, bool moved) {
        if (!photonClass || !componentsMethod.info || !mineMethod.info) return false;
        void* type = RuntimeType(photonClass);
        if (!type) return false;
        void* args[]{type};
        void* gameObject = nullptr;
        void* array = nullptr;
        if (!OptionalCall(gameObjectMethod, transform, nullptr, gameObject) || !gameObject ||
            !OptionalCall(componentsMethod, gameObject, args, array)) return false;
        uintptr_t count = 0;
        if (!array || !SDK::ReadSafe(static_cast<char*>(array) + 0x18, count) ||
            count > Pickup::Detail::MaxElements - result.views.size()) return false;
        for (uintptr_t index = 0; index < count; ++index) {
            void* view = nullptr;
            if (!SDK::ReadSafe(static_cast<char*>(array) + 0x20 + index * sizeof(void*), view)) {
                return false;
            }
            if (!view) continue;
            result.networked = true;
            if (!IsPhotonView(view) || !OptionalAlive(view)) return false;
            result.views.push_back(view);
            bool mine = false;
            if (!OptionalValue(mineMethod, view, mine)) return false;
            if (!mine) {
                result.ownershipAllowed = false;
                int option = 0;
                if (Field(view, "OwnershipTransfer", option) &&
                    (option == 1 || option == 2) && requestMethod.info)
                    result.requestableViews.push_back(view);
            }
            // An ancestor's view does not serialize an arbitrary child transform.
            if (!moved || !PositionObserved(view, transform)) result.synchronizationSupported = false;
        }
        return true;
    }

    bool ProbeViews(void* transform, Pickup::Detail::Candidate& result, bool moved) {
        __try { return InspectViews(transform, result, moved); }
        __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION
            ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) { return false; }
    }

    bool Finite(const Vector3& value) {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    }

    bool CameraRay(Vector3& origin, Vector3& forward) {
        if (!Living(camera)) return false;
        void* transform = Call(transformMethod, camera);
        Quaternion rotation{};
        if (!Living(transform) || !Value(positionMethod, transform, origin) ||
            !Value(rotationMethod, transform, rotation)) return false;
        const float norm = rotation.x * rotation.x + rotation.y * rotation.y +
            rotation.z * rotation.z + rotation.w * rotation.w;
        if (!std::isfinite(norm) || norm < 0.000001f) return false;
        forward = {2 * (rotation.x * rotation.z + rotation.w * rotation.y) / norm,
            2 * (rotation.y * rotation.z - rotation.w * rotation.x) / norm,
            1 - 2 * (rotation.x * rotation.x + rotation.y * rotation.y) / norm};
        return Finite(origin) && Finite(forward);
    }

    bool HasAncestor(void* transform, void* ancestor) {
        std::unordered_set<void*> seen;
        for (void* node = transform; node; node = Call(parentMethod, node)) {
            if (!Living(node) || seen.size() == Pickup::Detail::MaxElements ||
                !seen.insert(node).second) {
                callFailed = true;
                return false;
            }
            if (node == ancestor) return true;
        }
        return false;
    }

    bool IsSelf(void* transform) {
        for (void* anchor : {player, avatar, camera}) {
            if (!Living(anchor)) continue;
            void* local = Call(transformMethod, anchor);
            if (!Living(local)) { callFailed = true; return false; }
            if (HasAncestor(transform, local) || HasAncestor(local, transform)) return true;
            if (callFailed) return false;
        }
        return false;
    }

    void Classify(void* transform, Pickup::Detail::Candidate& result) {
        if (!result.actor)
            result.actor = Component(transform, playerClass) || Component(transform, localClass) ||
                Component(transform, ghostClass);
        if (!result.equipment) result.equipment = Component(transform, equipmentClass) != nullptr;
    }

    Pickup::Detail::Candidate Inspect(void* transform, void* body) {
        Pickup::Detail::Candidate result;
        result.transform = transform;
        result.body = body;
        if (!Living(transform)) return result;
        result.self = IsSelf(transform);
        if (result.self || callFailed) return result;
        if (!Value(childCountMethod, transform, result.children)) return result;
        result.hasParent = Living(Call(parentMethod, transform));
        result.ownershipAllowed = true;
        result.synchronizationSupported = true;
        bool rootView = false;
        std::unordered_set<void*> ancestors;
        void* node = transform;
        while (node) {
            if (ancestors.size() == Pickup::Detail::MaxElements ||
                !ancestors.insert(node).second || !Living(node)) return result;
            Classify(node, result);
            if (!ProbeViews(node, result, node == transform)) result.networkingKnown = false;
            if (node == transform) rootView = result.networked;
            node = Call(parentMethod, node);
        }
        if (callFailed) return result;
        std::vector<void*> nodes{transform};
        std::unordered_set<void*> seen{transform}, bodies;
        for (size_t index = 0; index < nodes.size(); ++index) {
            node = nodes[index];
            if (!Living(node)) return result;
            Classify(node, result);
            if (index && !ProbeViews(node, result, true)) result.networkingKnown = false;
            void* attached = Component(node, rigidbodyClass);
            if (attached && bodies.insert(attached).second) result.bodies.push_back(attached);
            int children = 0;
            if (!Value(childCountMethod, node, children) || children < 0 ||
                static_cast<size_t>(children) > Pickup::Detail::MaxElements - nodes.size()) return result;
            for (int child = 0; child < children; ++child) {
                void* args[]{&child};
                void* next = Call(childMethod, node, args);
                if (!next || !seen.insert(next).second) return result;
                nodes.push_back(next);
            }
        }
        std::sort(result.views.begin(), result.views.end(), std::less<void*>{});
        result.views.erase(std::unique(result.views.begin(), result.views.end()), result.views.end());
        if (!result.networkingKnown) {
            result.ownershipAllowed = false;
            result.synchronizationSupported = false;
        }
        if (result.networked && !rootView) result.synchronizationSupported = false;
        result.compoundBody = result.bodies.size() > 1 || result.children > 0;
        result.validHierarchy = !callFailed;
        return result;
    }

    struct Backend {
        bool Alive(void* object, bool& result) { return ::Alive(object, result); }
        bool GetKinematic(void* body, bool& result) {
            return Value(getKinematicMethod, body, result);
        }
        bool SetKinematic(void* body, bool value) {
            void* args[]{&value};
            Call(setKinematicMethod, body, args);
            bool actual = !value;
            return !callFailed && GetKinematic(body, actual) && actual == value;
        }
        bool Pin(const Pickup::Detail::Candidate& candidate) {
            bodyHandles.assign(candidate.bodies.size(), 0);
            viewHandles.assign(candidate.views.size(), 0);
            transformHandle = Root(candidate.transform);
            if (!transformHandle) return false;
            for (size_t i = 0; i < candidate.bodies.size(); ++i)
                if (!(bodyHandles[i] = Root(candidate.bodies[i]))) { Unpin(); return false; }
            for (size_t i = 0; i < candidate.views.size(); ++i)
                if (!(viewHandles[i] = Root(candidate.views[i]))) { Unpin(); return false; }
            return true;
        }
        bool Unpin() {
            for (auto& handle : viewHandles) if (!FreeRoot(handle)) return false;
            for (auto& handle : bodyHandles) if (!FreeRoot(handle)) return false;
            if (!FreeRoot(transformHandle)) return false;
            viewHandles.clear();
            bodyHandles.clear();
            return true;
        }
    } backend;

    bool FreshPermission() {
        const uint64_t now = GetTickCount64();
        return ready && healthy && Pickup::desired.load() && allowed && !captured &&
            frameAt && now >= frameAt && now - frameAt <= 250 && Living(camera);
    }

    void NetworkStatus(const Pickup::Detail::Candidate& candidate) {
        if (candidate.networkingKnown && candidate.networked && candidate.ownershipAllowed &&
            candidate.synchronizationSupported)
            message = "Holding; native Photon position observer available, delivery unverified.";
        else if (candidate.networked && !candidate.ownershipAllowed)
            message = ownershipRequested
                ? "Holding locally; native ownership requested, synchronization unverified."
                : "Holding locally; ownership unavailable/denied, synchronization unverified.";
        else if (candidate.networked)
            message = "Holding locally; unsupported Photon observer, synchronization unverified.";
        else if (!candidate.networkingKnown)
            message = "Holding locally; Photon metadata unavailable, synchronization unverified.";
        else
            message = "Holding local-only collider target; position is not restored on drop.";
    }

    void RequestOwnership(const Pickup::Detail::Candidate& candidate) {
        const uint64_t now = GetTickCount64();
        if (candidate.requestableViews.empty()) { ownershipRequested = false; return; }
        if (requestedAt && now - requestedAt < 1000) return;
        requestedAt = now;
        ownershipRequested = false;
        for (void* view : candidate.requestableViews) {
            void* unused = nullptr;
            if (OptionalAlive(view) && OptionalCall(requestMethod, view, nullptr, unused))
                ownershipRequested = true;
        }
    }

    bool Select() {
        Vector3 origin{}, forward{};
        if (!CameraRay(origin, forward)) return false;
        // Runtime-invoke marshals the actual metadata signature instead of
        // guessing Win64 struct/by-ref argument lowering.
        alignas(16) unsigned char hit[64]{};
        float reach = 2.5f;
        int mask = ~0, includeTriggers = 2;
        void* args[]{&origin, &forward, hit, &reach, &mask, &includeTriggers};
        bool didHit = false;
        if (!Value(raycastMethod, nullptr, didHit, args) || !didHit) return false;
        void* collider = Call(colliderMethod, hit);
        if (!Living(collider)) return false;
        void* transform = Call(transformMethod, collider);
        void* body = Call(bodyMethod, collider);
        if (body && Living(body)) transform = Call(transformMethod, body);
        else body = nullptr;
        // Without an attached body, move the hit collider's transform rather
        // than promoting it to an unrelated PhotonView/scene ancestor.
        auto candidate = Inspect(transform, body);
        if (candidate.self) {
            message = "Not held: your player, camera and their hierarchy are protected.";
            return false;
        }
        if (!Pickup::Detail::Selectable(candidate) || callFailed) {
            message = "Not held: invalid/destroyed hierarchy or memory-sanity limit.";
            return false;
        }
        if (!Pickup::Detail::InScope(candidate, Pickup::scope.load())) {
            message = "Not held: target is outside the selected pickup scope.";
            return false;
        }
        Vector3 pivot{}, point{};
        float hitDistance = 0;
        if (!Value(positionMethod, transform, pivot) ||
            !Value(hitPointMethod, hit, point) || !Value(hitDistanceMethod, hit, hitDistance) ||
            !Finite(pivot) || !Finite(point) || !std::isfinite(hitDistance) ||
            hitDistance < 0 || hitDistance > reach) return false;
        pivotOffset = {pivot.x - point.x, pivot.y - point.y, pivot.z - point.z};
        if (!Finite(pivotOffset)) return false;
        distance = (std::max)(0.5f, hitDistance);
        if (!lease.Begin(backend, candidate, generation)) {
            if (lease.active) {
                callFailed = true;
                return true;
            }
            return false;
        }
        requestedAt = 0;
        ownershipRequested = false;
        RequestOwnership(candidate);
        NetworkStatus(candidate);
        return true;
    }

    bool Release() {
        if (lease.active && healthy) {
            const auto observed = Inspect(lease.object.transform, lease.object.body);
            if (Pickup::Detail::Selectable(observed) && Pickup::Detail::SameScope(lease.object, observed) &&
                !lease.ObserveOwnership(backend, observed, false)) {
                callFailed = true;
                return false;
            }
        }
        if (!lease.Release(backend)) {
            callFailed = true;
            message = "Pickup flag restoration failed; unload blocked. Will retry.";
            return false;
        }
        return backend.Unpin();
    }

    bool Frame() {
        for (auto entry : {std::pair<void*, uint32_t*>{camera, &cameraHandle},
            std::pair<void*, uint32_t*>{player, &playerHandle},
            std::pair<void*, uint32_t*>{avatar, &avatarHandle}}) {
            uint32_t next = entry.first ? Root(entry.first) : 0;
            if (entry.first && !next) { callFailed = true; return false; }
            if (!FreeRoot(*entry.second)) {
                FreeRoot(next);
                return false;
            }
            *entry.second = next;
        }
        const bool permission = FreshPermission();
        if (lease.active && (!permission || lease.generation != generation)) {
            if (!Release()) return false;
            message = "Released; temporary flags restored, object position retained.";
            return !callFailed;
        }
        bool eligible = false;
        if (lease.active && permission && lease.generation == generation) {
            const auto inspected = Inspect(lease.object.transform, lease.object.body);
            eligible = Pickup::Detail::InScope(inspected, Pickup::scope.load()) &&
                Pickup::Detail::SameScope(lease.object, inspected);
            if (eligible) {
                if (!lease.ObserveOwnership(backend, inspected)) {
                    callFailed = true;
                    return false;
                }
                lease.object.networked = inspected.networked;
                lease.object.ownershipAllowed = inspected.ownershipAllowed;
                lease.object.synchronizationSupported = inspected.synchronizationSupported;
                lease.object.networkingKnown = inspected.networkingKnown;
                RequestOwnership(inspected);
                NetworkStatus(inspected);
            }
        }
        const bool wasHeld = lease.active;
        if (!lease.Tick(backend, Pickup::desired.load(), permission, captured, generation, eligible)) {
            callFailed = true;
            return false;
        }
        if (wasHeld && !lease.active)
            message = "Released; temporary flags restored, object position retained.";
        if (!lease.active) return !callFailed;
        Vector3 origin{}, forward{};
        if (!CameraRay(origin, forward)) return Release();
        Vector3 position{origin.x + forward.x * distance + pivotOffset.x,
            origin.y + forward.y * distance + pivotOffset.y,
            origin.z + forward.z * distance + pivotOffset.z};
        if (!Finite(position)) return Release();
        void* args[]{&position};
        Call(setPositionMethod, lease.object.transform, args);
        return !callFailed;
    }

    bool Input() {
        if (!FreshPermission() || !inputContext) {
            const bool held = lease.active;
            if (held) Release();
            return held;
        }
        int phase = 0;
        if (!Value(phaseMethod, inputContext, phase)) return lease.active;
        // Native PCPropGrab.OnPickup likewise acts only on Performed (3).
        if (phase != 3) return lease.active;
        if (lease.active) {
            Release();
            message = lease.active ? "Pickup restoration pending; unload blocked." : "Dropped; object position retained.";
            return true;
        }
        return Select();
    }

    bool Init() {
        HMODULE module = GetModuleHandleW(L"GameAssembly.dll");
        if (!module || !SDK::class_from_name || !SDK::class_get_type || !SDK::type_get_object ||
            !SDK::class_from_type || !SDK::method_get_param || !SDK::class_get_name ||
            !SDK::class_value_size) return false;
        bool exports = Export(module, "il2cpp_runtime_invoke", invoke);
        exports &= Export(module, "il2cpp_object_unbox", unbox);
        exports &= Export(module, "il2cpp_class_get_methods", methods);
        exports &= Export(module, "il2cpp_class_get_nested_types", nestedTypes);
        exports &= Export(module, "il2cpp_method_get_name", methodName);
        exports &= Export(module, "il2cpp_method_get_param_count", paramCount);
        exports &= Export(module, "il2cpp_gchandle_new", handleNew);
        exports &= Export(module, "il2cpp_gchandle_free", handleFree);
        Export(module, "il2cpp_class_get_field_from_name", fieldFind);
        Export(module, "il2cpp_field_get_value", fieldRead);
        Export(module, "il2cpp_object_get_class", objectClass);
        Export(module, "il2cpp_class_is_assignable_from", assignable);
        if (!exports) return false;
        void* assembly = SDK::assembly_open && SDK::domain_get
            ? SDK::assembly_open(SDK::domain_get(), "PhotonUnityNetworking") : nullptr;
        void* photonImage = assembly && SDK::assembly_get_image ? SDK::assembly_get_image(assembly) : nullptr;
        void* component = SDK::class_from_name(initImages[1], "UnityEngine", "Component");
        void* gameObject = SDK::class_from_name(initImages[1], "UnityEngine", "GameObject");
        void* transform = SDK::class_from_name(initImages[1], "UnityEngine", "Transform");
        void* unityObject = SDK::class_from_name(initImages[1], "UnityEngine", "Object");
        void* behaviour = SDK::class_from_name(initImages[1], "UnityEngine", "Behaviour");
        void* physics = SDK::class_from_name(initImages[2], "UnityEngine", "Physics");
        void* collider = SDK::class_from_name(initImages[2], "UnityEngine", "Collider");
        void* hit = SDK::class_from_name(initImages[2], "UnityEngine", "RaycastHit");
        rigidbodyClass = SDK::class_from_name(initImages[2], "UnityEngine", "Rigidbody");
        playerClass = SDK::class_from_name(initImages[0], "", "Player");
        localClass = SDK::class_from_name(initImages[0], "", "LocalPlayer");
        ghostClass = SDK::class_from_name(initImages[0], "", "GhostAI");
        equipmentClass = SDK::class_from_name(initImages[0], "", "Equipment");
        if (!playerClass || !localClass || !ghostClass || !equipmentClass) {
            Logger::Log("[ERROR] Pickup: scope classification metadata unavailable.");
            return false;
        }
        photonClass = photonImage ? SDK::class_from_name(photonImage, "Photon.Pun", "PhotonView") : nullptr;
        syncClass = photonImage ? SDK::class_from_name(photonImage, "Photon.Pun", "PhotonTransformView") : nullptr;
        bodySyncClass = photonImage ? SDK::class_from_name(photonImage, "Photon.Pun", "PhotonRigidbodyView") : nullptr;
        void* inputAction = SDK::class_from_name(initImages[3], "UnityEngine.InputSystem", "InputAction");
        void* callback = nullptr;
        void* iterator = nullptr;
        while (inputAction) {
            void* type = nestedTypes(inputAction, &iterator);
            if (!type) break;
            if (std::strcmp(SDK::class_get_name(type), "CallbackContext") == 0) callback = type;
        }
        uint32_t alignment = 0;
        const int hitSize = hit ? SDK::class_value_size(hit, &alignment) : 0;
        if (!callback ||
            hitSize < 44 || hitSize > 64 || alignment > 16) return false;
        bool ok = Bind(aliveMethod, unityObject, "op_Implicit", 1);
        ok &= Bind(transformMethod, component, "get_transform", 0);
        Bind(gameObjectMethod, component, "get_gameObject", 0);
        ok &= Bind(componentMethod, component, "GetComponent", 1, "Type");
        Bind(componentsMethod, gameObject, "GetComponents", 1, "Type");
        ok &= Bind(parentMethod, transform, "get_parent", 0);
        ok &= Bind(childCountMethod, transform, "get_childCount", 0);
        ok &= Bind(childMethod, transform, "GetChild", 1);
        ok &= Bind(positionMethod, transform, "get_position", 0);
        ok &= Bind(setPositionMethod, transform, "set_position", 1);
        ok &= Bind(rotationMethod, transform, "get_rotation", 0);
        ok &= Bind(raycastMethod, physics, "Raycast", 6);
        ok &= Bind(colliderMethod, hit, "get_collider", 0);
        ok &= Bind(hitPointMethod, hit, "get_point", 0);
        ok &= Bind(hitDistanceMethod, hit, "get_distance", 0);
        ok &= Bind(bodyMethod, collider, "get_attachedRigidbody", 0);
        Bind(mineMethod, photonClass, "get_IsMine", 0);
        Bind(requestMethod, photonClass, "RequestOwnership", 0);
        Bind(enabledMethod, behaviour, "get_isActiveAndEnabled", 0);
        ok &= Bind(getKinematicMethod, rigidbodyClass, "get_isKinematic", 0);
        ok &= Bind(setKinematicMethod, rigidbodyClass, "set_isKinematic", 1);
        ok &= Bind(phaseMethod, callback, "get_phase", 0);
        return ok;
    }

    enum class Operation { Initialize, Frame, Input, Restore };

    bool Execute(Operation operation) {
        switch (operation) {
        case Operation::Initialize: return Init();
        case Operation::Frame: return Frame();
        case Operation::Input: return Input();
        case Operation::Restore:
            if (!Release()) return false;
            if (!FreeRoot(cameraHandle) || !FreeRoot(playerHandle) || !FreeRoot(avatarHandle)) return false;
            camera = player = avatar = nullptr;
            frameAt = 0;
            return true;
        }
        return false;
    }

    bool ExecuteSafely(Operation operation) {
        try { return Execute(operation); }
        catch (const std::bad_alloc&) { callFailed = true; return false; }
        catch (const std::length_error&) { callFailed = true; return false; }
    }

    bool Guard(Operation operation) {
        callFailed = false;
        __try {
            return ExecuteSafely(operation);
        }
        __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION
            ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
            callFailed = true;
            Logger::Log("[ERROR] Pickup: access violation; disabled, retaining restoration state.");
            return false;
        }
    }

    void Publish() {
        if (callFailed) {
            healthy = false;
            Pickup::desired.store(false);
            message = lease.active ? "Pickup fault; restoration pending, unload blocked."
                : "Pickup fault; disabled for this session.";
        }
        std::lock_guard<std::mutex> lock(statusMutex);
        status.available = ready && healthy;
        status.active = lease.active;
        status.restorePending = lease.active && (!healthy || !Pickup::desired.load());
        status.networked = lease.active && lease.object.networked;
        status.faulted = !healthy;
        status.generation = generation;
        status.message = message;
    }
}

bool Pickup::Initialize(void* image, void* unityImage, void* physicsImage, void* inputImage) {
    if (lease.active || ready) return ready && healthy;
    if (!image || !unityImage || !physicsImage || !inputImage) return false;
    initImages[0] = image;
    initImages[1] = unityImage;
    initImages[2] = physicsImage;
    initImages[3] = inputImage;
    ready = Guard(Operation::Initialize);
    message = ready ? "Experimental pickup off; fixture movement can break the map."
        : "Experimental pickup unavailable: required runtime methods/layout not verified.";
    Publish();
    return ready && healthy;
}

void Pickup::Update(void* localCamera, void* localPlayer, bool authorized, bool inputCaptured,
    uint64_t currentGeneration, void* localAvatar) {
    if (gameThread && gameThread != GetCurrentThreadId()) return;
    gameThread = GetCurrentThreadId();
    camera = localCamera;
    player = localPlayer;
    avatar = localAvatar;
    allowed = authorized;
    captured = inputCaptured;
    generation = currentGeneration;
    frameAt = GetTickCount64();
    if (ready) Guard(Operation::Frame);
    Publish();
}

bool Pickup::HandleInput(void* callbackContext) {
    if (!ready || !gameThread || gameThread != GetCurrentThreadId()) return false;
    inputContext = callbackContext;
    const bool wasHeld = lease.active;
    const bool consumed = Guard(Operation::Input);
    inputContext = nullptr;
    Publish();
    // A setter may fault after changing flags: never fall through and let the
    // native pickup also act on an object retained for restoration.
    return consumed || wasHeld || lease.active;
}

bool Pickup::Restore() {
    if (gameThread && gameThread != GetCurrentThreadId()) return false;
    desired.store(false);
    allowed = false;
    const bool restored = Guard(Operation::Restore);
    Publish();
    return restored && !callFailed && !lease.active;
}

Pickup::Snapshot Pickup::Read() {
    std::lock_guard<std::mutex> lock(statusMutex);
    return status;
}
