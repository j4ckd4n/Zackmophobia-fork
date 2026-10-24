#include "../Features/Pickup.h"
#include <cassert>
#include <iostream>
#include <vector>
#include <map>
#include "../Hooks/PickupHooks.cpp"

namespace {
    struct Mock {
        bool alive = true;
        bool kinematic = false;
        bool setterFails = false;
        bool readerFails = false;
        bool pinFails = false;
        bool aliveFails = false;
        bool pinned = false;
        int writes = 0;
        bool Alive(void*, bool& result) {
            result = alive;
            return !aliveFails;
        }
        bool GetKinematic(void*, bool& result) {
            result = kinematic;
            return !readerFails;
        }
        bool SetKinematic(void*, bool value) {
            ++writes;
            kinematic = value;
            return !setterFails;
        }
        bool Pin(const Pickup::Detail::Candidate&) {
            pinned = !pinFails;
            return !pinFails;
        }
        bool Unpin() { pinned = false; return true; }
    };

    Pickup::Detail::Candidate Candidate(bool physics = true) {
        Pickup::Detail::Candidate value;
        value.transform = reinterpret_cast<void*>(1);
        value.body = physics ? reinterpret_cast<void*>(2) : nullptr;
        value.hasParent = true;
        value.children = 0;
        value.validHierarchy = true;
        value.ownershipAllowed = true;
        value.compoundBody = false;
        return value;
    }

    void Selection() {
        auto candidate = Candidate();
        assert(Pickup::Detail::Selectable(candidate));
        candidate = Candidate(false);
        assert(Pickup::Detail::Selectable(candidate)); // Fixed fixture with no rigidbody.
        candidate.hasParent = false;
        assert(Pickup::Detail::Selectable(candidate)); // Root colliders are permitted.
        candidate = Candidate();
        candidate.children = 1;
        assert(Pickup::Detail::Selectable(candidate));
        candidate = Candidate();
        candidate.validHierarchy = false;
        assert(!Pickup::Detail::Selectable(candidate));
        candidate = Candidate();
        candidate.ownershipAllowed = false;
        assert(Pickup::Detail::Selectable(candidate)); // Ownership does not gate local movement.
        candidate = Candidate();
        candidate.compoundBody = true;
        assert(Pickup::Detail::Selectable(candidate));
        candidate.networked = true;
        assert(Pickup::Detail::Selectable(candidate)); // A view alone is not replication, but may move locally.
        candidate.synchronizationSupported = true;
        assert(Pickup::Detail::Selectable(candidate));
        candidate.ownershipAllowed = false;
        assert(Pickup::Detail::Selectable(candidate)); // A request is NOT authority or a prerequisite.
        candidate = Candidate();
        candidate.children = -1;
        assert(!Pickup::Detail::Selectable(candidate));
        candidate = Candidate();
        candidate.bodies.resize(Pickup::Detail::MaxElements + 1);
        assert(!Pickup::Detail::Selectable(candidate));
        candidate = Candidate();
        candidate.transform = nullptr;
        assert(!Pickup::Detail::Selectable(candidate));
    }

    void RestoreFlags() {
        for (bool original : {false, true}) {
            Mock backend;
            backend.kinematic = original;
            Pickup::Detail::Lease lease;
            assert(lease.Begin(backend, Candidate(), 10));
            assert(backend.kinematic && backend.pinned && lease.active);
            assert(!lease.Begin(backend, Candidate(), 10));
            assert(lease.Release(backend));
            assert(backend.kinematic == original && !backend.pinned && !lease.active);
            assert(lease.Release(backend));
            assert(backend.writes == 2);
        }
        Mock fixture;
        Pickup::Detail::Lease lease;
        assert(lease.Begin(fixture, Candidate(false), 10));
        assert(lease.Release(fixture));
        assert(fixture.writes == 0);
    }

    void ReleaseTriggers() {
        for (int trigger = 0; trigger < 5; ++trigger) {
            Mock backend;
            Pickup::Detail::Lease lease;
            assert(lease.Begin(backend, Candidate(), 10));
            assert(lease.Tick(backend, true, true, false, 10, true));
            assert(lease.active);
            assert(lease.Tick(backend, trigger != 0, trigger != 1, trigger == 2,
                trigger == 3 ? 11 : 10, trigger != 4));
            assert(!lease.active && !backend.kinematic && !backend.pinned);
        }
    }

    void FailedAndDestroyedRestore() {
        Mock backend;
        Pickup::Detail::Lease lease;
        backend.setterFails = true; // Fault may occur AFTER the mutation.
        assert(!lease.Begin(backend, Candidate(), 10));
        assert(lease.active && lease.restoreKinematic && backend.pinned && backend.kinematic);
        assert(!lease.Release(backend));
        assert(lease.active && lease.restoreKinematic && backend.pinned);
        backend.setterFails = false;
        assert(lease.Release(backend));
        assert(!lease.active && !backend.kinematic && !backend.pinned);

        assert(lease.Begin(backend, Candidate(), 10));
        backend.aliveFails = true;
        assert(!lease.Release(backend)); // Unknown/invalid pointer must block unload.
        assert(lease.active && backend.pinned);
        backend.aliveFails = false;
        backend.alive = false; // A genuinely destroyed native body has no flags to restore.
        const int writes = backend.writes;
        assert(lease.Release(backend));
        assert(!lease.active && !backend.pinned && backend.writes == writes);
    }

    void RejectedAcquisition() {
        Mock backend;
        Pickup::Detail::Lease lease;
        auto remote = Candidate();
        remote.ownershipAllowed = false;
        assert(lease.Begin(backend, remote, 10));
        assert(lease.Release(backend));
        backend.writes = 0;
        backend.readerFails = true;
        assert(!lease.Begin(backend, Candidate(), 10));
        backend.readerFails = false;
        backend.pinFails = true;
        assert(!lease.Begin(backend, Candidate(), 10));
        assert(!lease.active && !backend.pinned && backend.writes == 0);
    }

    void CompoundRestoreAndAuthority() {
        struct CompoundMock : Mock {
            bool flags[3]{false, true, false};
            int failAt = -1;
            bool GetKinematic(void* body, bool& result) {
                result = flags[reinterpret_cast<uintptr_t>(body) - 2];
                return true;
            }
            bool SetKinematic(void* body, bool value) {
                const auto index = reinterpret_cast<uintptr_t>(body) - 2;
                flags[index] = value;
                ++writes;
                return index != failAt;
            }
        } backend;
        auto compound = Candidate();
        compound.compoundBody = true;
        compound.children = 6;
        compound.bodies = {reinterpret_cast<void*>(2), reinterpret_cast<void*>(3),
            reinterpret_cast<void*>(4)};
        compound.networked = true;
        compound.synchronizationSupported = true;
        Pickup::Detail::Lease lease;
        backend.failAt = 1;
        assert(!lease.Begin(backend, compound, 10));
        assert(lease.active && backend.flags[0] && backend.flags[1] && !backend.flags[2]);
        backend.failAt = -1;
        assert(lease.Release(backend));
        assert(!backend.flags[0] && backend.flags[1] && !backend.flags[2]);
        assert(backend.writes == 4); // Untouched third body needs no restoration.

        assert(lease.Begin(backend, compound, 10));
        auto lost = compound;
        lost.ownershipAllowed = false;
        assert(lease.Tick(backend, true, true, false, 10, Pickup::Detail::Selectable(lost)));
        assert(lease.active); // Lost authority downgrades status, not local movement.
        assert(lease.Release(backend));
        assert(!backend.flags[0] && backend.flags[1] && !backend.flags[2]);

        assert(Pickup::Detail::SameScope(compound, compound));
        auto changed = compound;
        changed.bodies[2] = reinterpret_cast<void*>(5);
        assert(!Pickup::Detail::SameScope(compound, changed));
        changed = compound;
        changed.views.push_back(reinterpret_cast<void*>(6));
        assert(Pickup::Detail::SameScope(compound, changed)); // Network discovery cannot release the grab.
    }
}

namespace NativeBackendTest {
    struct Object {
        void* klass = nullptr;
        Object* transform = this;
        Object* parent = nullptr;
        Object* body = nullptr;
        Object* view = nullptr;
        Object* observer = nullptr;
        void* actor = nullptr;
        std::vector<Object*> children;
        std::vector<Object*> viewComponents;
        std::vector<char> array;
        bool mine = true, position = true, enabled = true, kinematic = false;
        int synchronization = 1, ownership = 1;
    };
    int playerType, localType, ghostType, photonType, bodyType, syncType, bodySyncType, equipmentType, propType;
    std::map<uint32_t, void*> roots;
    uint32_t nextHandle = 0;
    bool av = false, boxedBool = false, requestFails = false, requestFaults = false, probeFaults = false;
    bool wrongViewType = false;
    int boxedInt = 0, writes = 0, requests = 0, positionWrites = 0;
    Object* targetCollider = nullptr;
    Object* failedKinematicRead = nullptr;

    void* Array(Object* object, Object* value) {
        const uintptr_t count = value ? 1 : 0;
        object->array.resize(0x20 + count * sizeof(void*));
        std::memcpy(object->array.data() + 0x18, &count, sizeof(count));
        std::memcpy(object->array.data() + 0x20, &value, sizeof(value));
        return object->array.data();
    }
    void* InvokeMock(void* method, void* self, void** args, void** exception) {
        auto object = static_cast<Object*>(self);
        if (method == &aliveMethod) {
            boxedBool = args[0] != nullptr;
            return &boxedBool;
        }
        if (method == &componentMethod) {
            if (args[0] == rigidbodyClass) return object->body;
            if (args[0] == photonClass) return object->view;
            return args[0] == object->actor ? self : nullptr;
        }
        if (method == &transformMethod) return object->transform;
        if (method == &gameObjectMethod) return self;
        if (method == &parentMethod) return object->parent;
        if (method == &componentsMethod) {
            if (probeFaults) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
            if (!object->viewComponents.empty()) {
                for (auto view : object->viewComponents) view->klass = photonClass;
                const uintptr_t count = object->viewComponents.size();
                object->array.resize(0x20 + count * sizeof(void*));
                std::memcpy(object->array.data() + 0x18, &count, sizeof(count));
                std::memcpy(object->array.data() + 0x20, object->viewComponents.data(), count * sizeof(void*));
                return object->array.data();
            }
            if (object->view) object->view->klass = wrongViewType ? nullptr : photonClass;
            return Array(object, object->view);
        }
        if (method == &childCountMethod) {
            boxedInt = static_cast<int>(object->children.size());
            return &boxedInt;
        }
        if (method == &childMethod) return object->children.at(*static_cast<int*>(args[0]));
        if (method == &mineMethod || method == &enabledMethod || method == &getKinematicMethod) {
            if (method == &getKinematicMethod && object == failedKinematicRead)
                RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
            boxedBool = method == &mineMethod ? object->mine :
                method == &enabledMethod ? object->enabled : object->kinematic;
            return &boxedBool;
        }
        if (method == &setKinematicMethod) {
            object->kinematic = *static_cast<bool*>(args[0]);
            ++writes;
            if (av) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
            return nullptr;
        }
        if (method == &requestMethod) {
            ++requests;
            if (requestFaults) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
            if (requestFails) *exception = &photonType;
            return nullptr;
        }
        if (method == &setPositionMethod) { ++positionWrites; return nullptr; }
        if (method == &raycastMethod) {
            assert(*static_cast<int*>(args[4]) == ~0 && *static_cast<int*>(args[5]) == 2);
            boxedBool = targetCollider != nullptr;
            return &boxedBool;
        }
        if (method == &colliderMethod) return targetCollider;
        if (method == &bodyMethod) return object->body;
        if (method == &phaseMethod) { boxedInt = 3; return &boxedInt; }
        if (method == &positionMethod || method == &hitPointMethod) {
            static Vector3 position{0, 0, 1};
            return &position;
        }
        if (method == &rotationMethod) {
            static Quaternion rotation{0, 0, 0, 1};
            return &rotation;
        }
        if (method == &hitDistanceMethod) {
            static float hitDistance = 1;
            return &hitDistance;
        }
        assert(false);
        return nullptr;
    }
    void Reset() {
        assert(!lease.active);
        assert(roots.empty());
        callFailed = av = requestFails = requestFaults = probeFaults = false;
        wrongViewType = false;
        ownershipRequested = false;
        writes = requests = positionWrites = 0;
        requestedAt = 0;
        targetCollider = nullptr;
        failedKinematicRead = nullptr;
        photonClass = &photonType;
        playerClass = &playerType; localClass = &localType;
        ghostClass = &ghostType; equipmentClass = &equipmentType;
        Pickup::scope.store(Pickup::Scope::Everything);
        rigidbodyClass = &bodyType; syncClass = &syncType; bodySyncClass = &bodySyncType;
        SDK::class_get_type = [](void* klass) -> const void* { return klass; };
        SDK::type_get_object = [](const void* type) -> void* { return const_cast<void*>(type); };
        invoke = InvokeMock;
        unbox = [](void* value) { return value; };
        objectClass = [](void* object) { return static_cast<Object*>(object)->klass; };
        assignable = [](void* expected, void* actual) { return expected == actual; };
        fieldFind = [](void*, const char* name) -> void* { return const_cast<char*>(name); };
        fieldRead = [](void* self, void* field, void* out) {
            auto object = static_cast<Object*>(self);
            const auto name = static_cast<const char*>(field);
            if (!std::strcmp(name, "Synchronization")) *static_cast<int*>(out) = object->synchronization;
            else if (!std::strcmp(name, "OwnershipTransfer")) *static_cast<int*>(out) = object->ownership;
            else if (!std::strcmp(name, "ObservedComponents")) *static_cast<void**>(out) = self;
            else if (!std::strcmp(name, "_size")) *static_cast<int*>(out) = object->observer ? 1 : 0;
            else if (!std::strcmp(name, "_items")) *static_cast<void**>(out) = Array(object, object->observer);
            else if (!std::strcmp(name, "m_SynchronizePosition")) *static_cast<bool*>(out) = object->position;
            else if (!std::strcmp(name, "m_Body")) *static_cast<void**>(out) = object->body;
            else assert(false);
        };
        handleNew = [](void* object, bool) -> uint32_t {
            roots[++nextHandle] = object;
            return nextHandle;
        };
        handleFree = [](uint32_t handle) { assert(roots.erase(handle) == 1); };
        for (auto method : {&aliveMethod, &componentMethod, &transformMethod, &gameObjectMethod,
            &parentMethod, &componentsMethod, &childCountMethod, &childMethod, &mineMethod,
            &enabledMethod, &getKinematicMethod, &setKinematicMethod, &requestMethod, &setPositionMethod,
            &raycastMethod, &colliderMethod, &bodyMethod, &phaseMethod, &positionMethod,
            &hitPointMethod, &rotationMethod, &hitDistanceMethod})
            method->info = method;
        ::player = ::camera = ::avatar = nullptr;
        gameThread = GetCurrentThreadId();
        healthy = ready = true;
    }
    void SelectionAndReplication() {
        Reset();
        Object scene, root, child, firstBody, secondBody, view, observer;
        root.parent = &scene; root.children = {&child}; root.body = &firstBody;
        child.parent = &root; child.body = &secondBody;
        auto candidate = Inspect(&root, &firstBody);
        assert(Pickup::Detail::Selectable(candidate) && candidate.bodies.size() == 2 && !candidate.networked);
        root.parent = nullptr;
        assert(Pickup::Detail::Selectable(Inspect(&root, &firstBody))); // Standalone compound root.
        root.body = nullptr;
        assert(Pickup::Detail::Selectable(Inspect(&root, nullptr))); // Static root collider.
        root.parent = &scene;
        assert(Pickup::Detail::Selectable(Inspect(&root, nullptr))); // Static hierarchy fixture.
        child.actor = &playerType;
        assert(Pickup::Detail::Selectable(Inspect(&root, nullptr)));
        child.actor = &ghostType;
        assert(Pickup::Detail::Selectable(Inspect(&root, nullptr)));
        child.actor = &equipmentType;
        assert(Pickup::Detail::Selectable(Inspect(&root, nullptr)));
        child.actor = nullptr;
        root.view = &view;
        candidate = Inspect(&root, nullptr);
        assert(Pickup::Detail::Selectable(candidate) && !candidate.synchronizationSupported);
        view.observer = &observer; observer.klass = syncClass; observer.transform = &root;
        assert(Pickup::Detail::Selectable(Inspect(&root, nullptr)));
        root.parent = nullptr;
        assert(Pickup::Detail::Selectable(Inspect(&root, nullptr)));
        root.parent = &scene;
        scene.actor = &ghostType;
        assert(Pickup::Detail::Selectable(Inspect(&root, nullptr)));
        scene.actor = nullptr;
        root.children.resize(128, &child);
        assert(!Pickup::Detail::Selectable(Inspect(&root, nullptr))); // Repeated/cyclic nodes are invalid.
        root.children = {&child};
        observer.position = false;
        assert(!Inspect(&root, nullptr).synchronizationSupported);
        observer.position = true; observer.enabled = false;
        assert(!Inspect(&root, nullptr).synchronizationSupported);
        observer.enabled = true; view.synchronization = 0;
        assert(!Inspect(&root, nullptr).synchronizationSupported);
        view.synchronization = 1; observer.klass = bodySyncClass;
        assert(!Inspect(&root, nullptr).synchronizationSupported);
        observer.body = &firstBody; firstBody.transform = &root;
        root.body = &firstBody;
        assert(Pickup::Detail::Selectable(Inspect(&root, nullptr)));
        observer.klass = syncClass;
        view.synchronization = 1; view.mine = false;
        candidate = Inspect(&root, nullptr);
        assert(Pickup::Detail::Selectable(candidate) && !candidate.requestableViews.empty());
        view.ownership = 0;
        assert(Inspect(&root, nullptr).requestableViews.empty());
        view.mine = true; root.view = nullptr; scene.view = &view; observer.transform = &scene;
        candidate = Inspect(&root, nullptr);
        assert(Pickup::Detail::Selectable(candidate) && !candidate.synchronizationSupported);
        assert(!callFailed && writes == 0 && positionWrites == 0);
    }
    void ScopeAndSelfProtection() {
        Reset();
        Object scene, local, cam, avatar, limb, remote, remoteLimb, item, fixture, collider, body;
        local.parent = avatar.parent = remote.parent = item.parent = fixture.parent = &scene;
        cam.parent = &local;
        limb.parent = &avatar;
        remoteLimb.parent = &remote;
        remote.actor = &playerType;
        item.actor = &equipmentType;
        local.children = {&cam};
        avatar.children = {&limb};
        remote.children = {&remoteLimb};
        ::player = &local;
        ::camera = &cam;
        ::avatar = &avatar;
        for (auto target : {&local, &cam, &avatar, &limb, &scene}) {
            const auto rejected = Inspect(target, nullptr);
            assert(rejected.self && !Pickup::Detail::Selectable(rejected));
            for (auto selected : {Pickup::Scope::Equipment, Pickup::Scope::Props,
                Pickup::Scope::Actors, Pickup::Scope::Everything})
                assert(!Pickup::Detail::InScope(rejected, selected));
        }
        auto actor = Inspect(&remoteLimb, nullptr);
        auto equipment = Inspect(&item, nullptr);
        auto prop = Inspect(&fixture, nullptr);
        assert(actor.actor && !actor.self && equipment.equipment && !equipment.actor && !prop.actor);
        using Pickup::Detail::InScope;
        using Pickup::Scope;
        assert(InScope(equipment, Scope::Equipment) && !InScope(actor, Scope::Equipment) &&
            !InScope(prop, Scope::Equipment));
        assert(InScope(equipment, Scope::Props) && InScope(prop, Scope::Props) &&
            !InScope(actor, Scope::Props));
        assert(InScope(actor, Scope::Actors) && !InScope(equipment, Scope::Actors) &&
            !InScope(prop, Scope::Actors));
        assert(InScope(actor, Scope::Everything) && InScope(equipment, Scope::Everything) &&
            InScope(prop, Scope::Everything));
        remote.actor = &ghostType;
        assert(Inspect(&remote, nullptr).actor);
        remote.children.push_back(&item);
        item.parent = &remote;
        const auto actorWithEquipment = Inspect(&remote, nullptr);
        assert(actorWithEquipment.actor && actorWithEquipment.equipment &&
            !InScope(actorWithEquipment, Scope::Equipment));

        Pickup::desired.store(true);
        Pickup::Update(&cam, &local, true, false, 60, &avatar);
        collider.transform = &limb;
        collider.body = &body;
        body.transform = &avatar;
        targetCollider = &collider;
        int context = 0;
        assert(!Pickup::HandleInput(&context));
        assert(!lease.active && !body.kinematic && writes == 0 && positionWrites == 0);
        assert(std::strstr(message, "protected"));

        collider.body = nullptr;
        collider.transform = &fixture;
        assert(Pickup::HandleInput(&context) && lease.active);
        Pickup::scope.store(Scope::Actors);
        Pickup::Update(&cam, &local, true, false, 60, &avatar);
        assert(!lease.active && positionWrites == 0);
        assert(!Pickup::HandleInput(&context) && std::strstr(message, "scope"));
        Pickup::scope.store(Scope::Everything);
        fixture.body = &fixture;
        assert(Pickup::HandleInput(&context) && fixture.kinematic);
        // A held hierarchy becoming the local avatar must release before moving.
        Pickup::Update(&cam, &local, true, false, 60, &fixture);
        assert(!lease.active && !fixture.kinematic && positionWrites == 0);
        assert(Pickup::Restore() && roots.empty());
    }
    void AuthorityLossAndFaultedRestore() {
        Reset();
        Object scene, root, body, view, observer, local, cam;
        root.parent = &scene; root.body = &body; root.view = &view;
        view.observer = &observer; observer.klass = syncClass; observer.transform = &root;
        assert(lease.Begin(backend, Inspect(&root, &body), 20));
        assert(roots.size() == 3 && body.kinematic);
        ::player = &local; ::camera = &cam;
        allowed = true; captured = false; generation = 20; frameAt = GetTickCount64();
        Pickup::desired.store(true);
        view.mine = false;
        assert(Guard(Operation::Frame)); // Continue locally and explicitly downgrade status.
        assert(lease.active && body.kinematic && positionWrites == 1 && roots.size() == 5);
        assert(std::strstr(message, "synchronization unverified"));
        assert(Pickup::Restore() && roots.empty());

        view.mine = true;
        assert(lease.Begin(backend, Inspect(&root, &body), 20));
        av = true;
        assert(!Guard(Operation::Restore)); // Setter faults after applying restoration.
        assert(lease.active && lease.restoreKinematic && roots.size() == 3);
        av = false;
        assert(Guard(Operation::Restore) && !lease.active && roots.empty() && !body.kinematic);
        Pickup::desired.store(false);
    }
    void NativeRequestThenGrabCompound() {
        Reset();
        Object scene, root, child, collider, body, view, observer, local, cam;
        root.parent = &scene; root.children = {&child}; root.body = &body; root.view = &view;
        child.parent = &root; collider.transform = &child; collider.body = &body;
        body.transform = &root;
        view.observer = &observer; observer.klass = syncClass; observer.transform = &root;
        view.mine = false;
        targetCollider = &collider;
        Pickup::desired.store(true);
        Pickup::Update(&cam, &local, true, false, 30);
        int context = 0;
        assert(Pickup::HandleInput(&context));
        assert(requests == 1 && lease.active && writes == 1 && roots.size() == 5);
        assert(std::strstr(message, "requested") && !inputContext);
        Pickup::Update(&cam, &local, true, false, 30); // No second press; request remains throttled.
        assert(requests == 1 && lease.active);
        view.mine = true;
        Pickup::Update(&cam, &local, true, false, 30);
        assert(lease.active && lease.object.transform == &root && body.kinematic);
        assert(std::strstr(message, "observer available") && lease.object.ownershipAllowed);
        assert(roots.size() == 5);
        assert(positionWrites == 2);
        assert(Pickup::HandleInput(&context));
        assert(!lease.active && !body.kinematic && positionWrites == 2); // Drop never undoes position.
        assert(Pickup::Restore() && roots.empty());

        Pickup::desired.store(true);
        Pickup::Update(&cam, &local, true, false, 31);
        view.mine = false; view.ownership = 0;
        assert(Pickup::HandleInput(&context)); // Fixed remote ownership: local grab, no forged authority.
        assert(requests == 1 && lease.active && body.kinematic && !view.mine);
        assert(std::strstr(message, "synchronization unverified"));
        assert(Pickup::HandleInput(&context));
        view.mine = true;
        frameAt = GetTickCount64() - 1000;
        assert(!Pickup::HandleInput(&context)); // Stale input cannot acquire anything.
        assert(!lease.active);
        assert(Pickup::Restore() && roots.empty());
    }
    void OptionalNetworkFailuresAndCameraOnlyContext() {
        for (int mode = 0; mode < 8; ++mode) {
            Reset();
            Object root, collider, body, view, observer, cam;
            collider.transform = &root; collider.body = &body; body.transform = &root;
            root.body = &body; root.view = &view;
            view.observer = &observer; observer.klass = syncClass; observer.transform = &root;
            view.mine = mode == 0;
            if (mode == 0) observer.klass = &equipmentType; // Unknown observer on an owned actor/equipment.
            if (mode == 1) view.ownership = 0;
            requestFails = mode == 2;
            requestFaults = mode == 3;
            if (mode == 4) photonClass = nullptr;
            if (mode == 5) { syncClass = bodySyncClass = nullptr; fieldFind = nullptr; }
            probeFaults = mode == 6;
            wrongViewType = mode == 7;
            root.actor = &playerType;
            targetCollider = &collider;
            Pickup::desired.store(true);
            Pickup::Update(&cam, nullptr, true, false, 40);
            int context = 0;
            assert(Pickup::HandleInput(&context));
            assert(lease.active && body.kinematic && healthy && !callFailed);
            assert(std::strstr(message, "unverified"));
            Pickup::Update(&cam, nullptr, true, false, 40);
            assert(lease.active && positionWrites == 1 && healthy);
            assert(Pickup::Restore() && !body.kinematic && roots.empty());
        }
    }
    void LargeHierarchyAndMinimalColliderSelection() {
        Reset();
        Object root, cam, collider;
        std::vector<Object> children(200);
        for (auto& child : children) {
            child.parent = &root;
            child.body = &child;
            root.children.push_back(&child);
        }
        auto candidate = Inspect(&root, nullptr);
        assert(Pickup::Detail::Selectable(candidate) && candidate.bodies.size() == 200);
        assert(lease.Begin(backend, candidate, 50) && roots.size() == 201);
        for (const auto& child : children) assert(child.kinematic);
        assert(lease.Release(backend) && roots.empty());
        for (const auto& child : children) assert(!child.kinematic);

        Object ancestorView, observer;
        root.view = &ancestorView; ancestorView.observer = &observer;
        observer.klass = syncClass; observer.transform = &root;
        children[0].body = nullptr;
        children[0].actor = &equipmentType;
        collider.transform = &children[0];
        targetCollider = &collider;
        Pickup::desired.store(true);
        Pickup::Update(&cam, nullptr, true, false, 51);
        int context = 0;
        assert(Pickup::HandleInput(&context));
        assert(lease.active && lease.object.transform == &children[0]); // Never lift an unrelated ancestor scene.
        assert(Pickup::Restore() && roots.empty());
    }
    void AcquisitionFaultRetainsRestoration() {
        Reset();
        Object root, collider, body, cam;
        root.body = &body; body.transform = &root;
        collider.transform = &root; collider.body = &body; targetCollider = &collider;
        Pickup::desired.store(true);
        Pickup::Update(&cam, nullptr, true, false, 60);
        av = true;
        int context = 0;
        assert(Pickup::HandleInput(&context));
        assert(lease.active && body.kinematic && Pickup::Read().restorePending && !healthy);
        assert(!Pickup::Restore() && lease.active && !roots.empty());
        av = false;
        assert(Pickup::Restore() && !body.kinematic && roots.empty());
    }
    void ManyViewsAndDisabledNativeFallthrough() {
        Reset();
        Object root, collider, observer, cam;
        std::vector<Object> views(70);
        for (auto& view : views) {
            view.observer = &observer;
            view.mine = false;
            root.viewComponents.push_back(&view);
        }
        observer.klass = syncClass; observer.transform = &root;
        collider.transform = &root; targetCollider = &collider;
        Pickup::desired.store(true);
        Pickup::Update(&cam, nullptr, true, false, 70);
        int context = 0;
        assert(Pickup::HandleInput(&context));
        assert(lease.active && lease.object.views.size() == 70 && requests == 70 && roots.size() == 72);
        assert(Pickup::Restore() && roots.empty());
        Pickup::Update(&cam, nullptr, true, false, 70);
        assert(!Pickup::HandleInput(&context)); // Disabled experimental mode leaves native pickup usable.
        assert(!lease.active && !positionWrites);
        assert(Pickup::Restore() && roots.empty());
    }
    void OwnershipApprovalReconcilesObservedPhysics() {
        for (int mode = 0; mode < 5; ++mode) {
            Reset();
            Object root, collider, body, fixture, view, observer, cam;
            root.body = &body; body.transform = &root; root.children = {&fixture};
            fixture.parent = &root; fixture.body = &fixture;
            body.kinematic = fixture.kinematic = true;
            root.view = &view; view.mine = false; view.observer = &observer;
            observer.klass = syncClass; observer.transform = &root;
            collider.transform = &root; collider.body = &body; targetCollider = &collider;
            Pickup::desired.store(true);
            Pickup::Update(&cam, nullptr, true, false, 80);
            int context = 0;
            assert(Pickup::HandleInput(&context) && lease.active);
            assert(lease.originals[0] && lease.originals[1]);
            view.mine = true;
            if (mode == 0) {
                Pickup::Update(&cam, nullptr, true, false, 80);
                assert(lease.originals[0] && lease.originals[1]); // Approval alone cannot make fixtures dynamic.
            }
            body.kinematic = false; // Observed native physics change, possibly after the approval frame.
            if (mode == 1) av = true; // Reassertion faults after setting the first body back to true.
            if (mode == 2) failedKinematicRead = &fixture; // Partial capture must finish before restoring anything.
            if (mode == 3) {
                assert(Pickup::HandleInput(&context)); // Approval immediately followed by a drop, no intervening frame.
            } else {
                if (mode == 4) Pickup::desired.store(false);
                Pickup::Update(&cam, nullptr, true, false, 80);
            }
            if (mode == 1 || mode == 2) {
                assert(lease.active && Pickup::Read().restorePending);
                assert(!Pickup::Restore()); // Failed native operation continues to block unload.
                assert(lease.active && !lease.originals[0] && lease.originals[1]);
                av = false;
                failedKinematicRead = nullptr;
            } else if (mode == 0) {
                assert(lease.active && body.kinematic && fixture.kinematic);
                assert(!lease.originals[0] && lease.originals[1]);
            }
            assert(Pickup::Restore() && roots.empty());
            assert(!body.kinematic && fixture.kinematic);
        }
    }
}

int main() {
    assert(!Pickup::desired.load());
    Selection();
    RestoreFlags();
    ReleaseTriggers();
    FailedAndDestroyedRestore();
    RejectedAcquisition();
    CompoundRestoreAndAuthority();
    NativeBackendTest::SelectionAndReplication();
    NativeBackendTest::ScopeAndSelfProtection();
    NativeBackendTest::AuthorityLossAndFaultedRestore();
    NativeBackendTest::NativeRequestThenGrabCompound();
    NativeBackendTest::OptionalNetworkFailuresAndCameraOnlyContext();
    NativeBackendTest::LargeHierarchyAndMinimalColliderSelection();
    NativeBackendTest::AcquisitionFaultRetainsRestoration();
    NativeBackendTest::ManyViewsAndDisabledNativeFallthrough();
    NativeBackendTest::OwnershipApprovalReconcilesObservedPhysics();
    std::cout << "Pickup tests passed.\n";
}
