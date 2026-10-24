#include <array>
#include <cassert>
#include <iostream>
#include <limits>
#include <map>
#include "../Hooks/RecoveryHooks.cpp"

namespace {
    bool pickupRestores = true;
    int pickupRestoresCalled = 0;
}

bool Pickup::Restore() { ++pickupRestoresCalled; return pickupRestores; }

namespace {
    std::array<char, 0xB0> local{}, otherLocal{};
    std::array<char, 0x48> mainOwner{}, missionOwner{};
    std::array<char, 0x20> lobbyList{}, missionList{};
    std::array<char, 0x30> lobbyItems{}, missionItems{};
    int physics, otherPhysics, lobbyAnchor, missionAnchor;
    int localClassMarker, transformClassMarker, mainClassMarker, multiplayerClassMarker;
    int exceptionMarker;
    struct Object {
        void* type = nullptr;
        bool alive = true;
        bool enabled = true;
        Vector3 position{};
    };
    std::map<void*, Object> objects;
    std::map<uint32_t, void*> roots;
    uint32_t nextRoot = 0;
    void* mainObject = nullptr;
    void* missionObject = nullptr;
    bool boxedBool = false, restoreFails = false;
    int teleportFailure = 0, teleports = 0;

    template<class T, size_t N>
    void Put(std::array<char, N>& bytes, size_t offset, T value) {
        assert(offset + sizeof(T) <= N);
        std::memcpy(bytes.data() + offset, &value, sizeof(T));
    }

    void* Invoke(void* method, void* self, void** args, void** exception) {
        if (method == &aliveMethod) {
            auto found = objects.find(args[0]);
            boxedBool = found != objects.end() && found->second.alive;
            return &boxedBool;
        }
        if (method == &findMethod)
            return args[0] == mainType ? mainObject : missionObject;
        if (method == &positionMethod) return &objects.at(self).position;
        if (method == &enabledMethod) {
            boxedBool = objects.at(self).enabled;
            return &boxedBool;
        }
        if (method == &setEnabledMethod) {
            objects.at(self).enabled = *static_cast<bool*>(args[0]);
            if (restoreFails) *exception = &exceptionMarker;
            return nullptr;
        }
        if (method == &teleportMethod) {
            ++teleports;
            void* body = nullptr;
            assert(Field(self, 0x88, body));
            objects.at(body).enabled = false;
            assert(SDK::WriteSafe(static_cast<char*>(self) + 0xAE, uint8_t{1}));
            if (teleportFailure == 2) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
            if (teleportFailure) { *exception = &exceptionMarker; return nullptr; }
            objects.at(self).position = *static_cast<Vector3*>(args[0]);
            objects.at(body).enabled = true;
            assert(SDK::WriteSafe(static_cast<char*>(self) + 0xAE, uint8_t{0}));
            return nullptr;
        }
        assert(false);
        return nullptr;
    }

    void ResetFixture() {
        restoreFails = false;
        assert(Recovery::Reset());
        assert(roots.empty());
        objects.clear();
        local.fill(0); otherLocal.fill(0);
        mainOwner.fill(0); missionOwner.fill(0);
        localType = &localClassMarker; transformType = &transformClassMarker;
        mainType = &mainClassMarker; multiplayerType = &multiplayerClassMarker;
        objects[local.data()].type = objects[otherLocal.data()].type = localType;
        objects[&physics] = {}; objects[&otherPhysics] = {};
        objects[mainOwner.data()].type = mainType;
        objects[missionOwner.data()].type = multiplayerType;
        objects[&lobbyAnchor] = {transformType, true, true, {12, 1, -8}};
        objects[&missionAnchor] = {transformType, true, true, {-40, 3, 22}};
        Put(local, 0x88, static_cast<void*>(&physics));
        Put(otherLocal, 0x88, static_cast<void*>(&otherPhysics));
        Put(mainOwner, 0x38, static_cast<void*>(lobbyList.data()));
        Put(missionOwner, 0x28, static_cast<void*>(missionList.data()));
        Put(lobbyList, 0x10, static_cast<void*>(lobbyItems.data()));
        Put(missionList, 0x10, static_cast<void*>(missionItems.data()));
        Put(lobbyList, 0x18, int{1}); Put(missionList, 0x18, int{1});
        Put(lobbyItems, 0x18, uintptr_t{1}); Put(missionItems, 0x18, uintptr_t{1});
        Put(lobbyItems, 0x20, static_cast<void*>(&lobbyAnchor));
        Put(missionItems, 0x20, static_cast<void*>(&missionAnchor));
        mainObject = mainOwner.data(); missionObject = nullptr;
        teleportFailure = teleports = pickupRestoresCalled = 0;
        pickupRestores = true;
        status = {}; current = {}; currentGeneration = 0;
        ready = healthy = true;
        gameThread = GetCurrentThreadId();
        invoke = Invoke;
        unbox = [](void* value) { return value; };
        SDK::class_get_type = [](void* type) -> const void* { return type; };
        SDK::type_get_object = [](const void* type) -> void* { return const_cast<void*>(type); };
        SDK::object_get_class = [](void* object) { return objects.at(object).type; };
        assignable = [](void* expected, void* actual) { return expected == actual; };
        pin = [](void* object, bool) -> uint32_t { roots[++nextRoot] = object; return nextRoot; };
        unpin = [](uint32_t root) { assert(roots.erase(root) == 1); };
        aliveMethod = &aliveMethod; findMethod = &findMethod; positionMethod = &positionMethod;
        enabledMethod = &enabledMethod; setEnabledMethod = &setEnabledMethod;
        teleportMethod = &teleportMethod;
    }

    void ObserveAndQueue(void* who = local.data()) {
        status.sampledAt = 0;
        Recovery::Update(who, true, 3);
        assert(Recovery::Read().available);
        assert(Recovery::Queue(Recovery::Read().sampledAt));
    }

    void SpawnAndRequestValidation() {
        ResetFixture();
        Recovery::Update(local.data(), true, 3);
        auto snapshot = Recovery::Read();
        assert(snapshot.available && !snapshot.pending && teleports == 0 && roots.empty());
        assert(!Recovery::Queue(snapshot.sampledAt + MaxAge + 1));
        assert(!Recovery::Queue(snapshot.sampledAt - 1));
        assert(Recovery::Queue(snapshot.sampledAt));
        assert(!Recovery::Queue(snapshot.sampledAt));
        Recovery::Update(local.data(), true, 3);
        auto position = objects.at(local.data()).position;
        assert(teleports == 1 && pickupRestoresCalled == 1 && roots.empty());
        assert(position.x == 12 && position.y == 1 && position.z == -8);
        assert(objects.at(&physics).enabled && local[0xAE] == 0);

        missionObject = missionOwner.data();
        ObserveAndQueue();
        Recovery::Update(local.data(), true, 3);
        position = objects.at(local.data()).position;
        assert(teleports == 2 && position.x == -40 && position.y == 3 && position.z == 22);

        ObserveAndQueue();
        Recovery::Update(local.data(), true, 4);
        assert(teleports == 2 && !Recovery::Read().pending);
        ObserveAndQueue();
        Recovery::Update(otherLocal.data(), true, 3);
        assert(teleports == 2);
        ObserveAndQueue();
        missionObject = nullptr;
        Recovery::Update(local.data(), true, 3);
        assert(teleports == 2); // Same controller, different scene owner.
        ObserveAndQueue();
        request.at = GetTickCount64() - MaxAge - 1;
        Recovery::Update(local.data(), true, 3);
        assert(teleports == 2);
        ObserveAndQueue();
        Recovery::Update(local.data(), false, 3);
        assert(teleports == 2 && !Recovery::Read().available);
    }

    void UnavailableAndPickupRestoration() {
        ResetFixture();
        Put(lobbyList, 0x18, int{65});
        Recovery::Update(local.data(), true, 3);
        assert(!Recovery::Read().available && !Recovery::Queue(GetTickCount64()));
        Put(lobbyList, 0x18, int{1});
        objects[&lobbyAnchor].position.x = std::numeric_limits<float>::quiet_NaN();
        status.sampledAt = 0;
        Recovery::Update(local.data(), true, 3);
        assert(!Recovery::Read().available && teleports == 0);
        objects[&lobbyAnchor].position = {12, 1, -8};
        Put(local, 0x38, uint8_t{1});
        status.sampledAt = 0;
        Recovery::Update(local.data(), true, 3);
        assert(!Recovery::Read().available && Recovery::Read().reason == "Unavailable while dead.");
        Put(local, 0x38, uint8_t{0});
        ObserveAndQueue();
        pickupRestores = false;
        Recovery::Update(local.data(), true, 3);
        assert(teleports == 0 && roots.empty() && !Recovery::Read().pending);
        assert(Recovery::Read().message.find("held-object") != std::string::npos);
    }

    void FailedTeleportRestoration() {
        for (int failure : {1, 2}) {
            ResetFixture();
            ObserveAndQueue();
            teleportFailure = failure;
            Recovery::Update(local.data(), true, 3);
            assert(teleports == 1 && objects.at(&physics).enabled && local[0xAE] == 0 && roots.empty());
            assert(!Recovery::Read().available && !Recovery::Read().restorePending);
        }
        ResetFixture();
        ObserveAndQueue();
        teleportFailure = 1;
        restoreFails = true;
        Recovery::Update(local.data(), true, 3);
        assert(Recovery::Read().restorePending && roots.size() == 2);
        assert(!Recovery::Reset() && roots.size() == 2);
        restoreFails = false;
        assert(Recovery::Reset() && roots.empty() && objects.at(&physics).enabled && local[0xAE] == 0);
    }
}

int main() {
    SpawnAndRequestValidation();
    UnavailableAndPickupRestoration();
    FailedTeleportRestoration();
    std::cout << "Recovery tests passed.\n";
}
