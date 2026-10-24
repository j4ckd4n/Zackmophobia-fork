#include <array>
#include <cassert>
#include <limits>
#include <thread>

// Include the backend to substitute its native bindings without exporting a test-only API.
#include "../Hooks/RadarHooks.cpp"

namespace {
    void* pickupCamera = nullptr;
    void* pickupLocal = nullptr;
    void* staminaLocal = nullptr;
    void* recoveryLocal = nullptr;
    bool recoveryEligible = false, recoveryRestores = true;
    bool pickupAllowed = false, consumePickup = false;
    bool controlOwnsPlayer = false;
    unsigned pickupInputs = 0;
}

void GhostControl::Initialize(void*, void*, void*, void*) {}
void GhostControl::SetHooksInstalled(bool) {}
void GhostControl::Update(const Context&) {}
GhostControl::Snapshot GhostControl::Read() { return {}; }
bool GhostControl::Restore() { return true; }
bool GhostControl::ControlsPlayer(void*) { return controlOwnsPlayer; }
bool GhostControl::BlocksInput(void*, bool) { return false; }
bool Pickup::Initialize(void*, void*, void*, void*) { return false; }
void Pickup::Update(void* camera, void* local, bool allowed, bool, uint64_t, void*) {
    pickupCamera = camera;
    pickupLocal = local;
    pickupAllowed = allowed;
}
bool Pickup::HandleInput(void*) { ++pickupInputs; return consumePickup; }
bool Pickup::Restore() { return true; }
void Recovery::Initialize(void*, void*) {}
void Recovery::Update(void* local, bool eligible, uint64_t) {
    recoveryLocal = local;
    recoveryEligible = eligible;
}
bool Recovery::Reset() { return recoveryRestores; }
void Stamina::Observe(void* local) { staminaLocal = local; }
void WorldActions::Initialize(void*, void*, void*) {}
void WorldActions::Update(const GhostControl::Context&, bool) {}
void WorldActions::Reset() {}

namespace {
    constexpr float pi = 3.14159265358979323846f;

    bool Near(float a, float b) {
        return std::fabs(a - b) < 0.0001f;
    }

    void TestMathAndQueue() {
        float heading = 0;
        assert(Radar::Heading({}, heading) && Near(heading, 0));
        assert(Radar::Heading({0, 1, 0, 1}, heading) && Near(heading, pi / 2));
        assert(Radar::Heading({0, -2, 0, 2}, heading) && Near(heading, -pi / 2));
        assert(Radar::Heading({0, 1, 0, 0}, heading) && Near(std::fabs(heading), pi));
        assert(!Radar::Heading({1, 0, 0, 1}, heading));
        assert(!Radar::Heading({0, 0, 0, 0}, heading));
        assert(!Radar::Heading({0, std::numeric_limits<float>::quiet_NaN(), 0, 1}, heading));

        float x = 0, y = 0;
        const Radar::Vector3 origin{10, 5, 20};
        assert(Radar::Project(origin, {13, 100, 24}, 5, x, y) && Near(x, 0.6f) && Near(y, -0.8f));
        assert(!Radar::Project(origin, {14, 5, 24}, 5, x, y));
        assert(!Radar::Project(origin, {10, 5, 20}, 0, x, y));
        assert(!Radar::Project(origin, {std::numeric_limits<float>::infinity(), 5, 20}, 5, x, y));
        assert(!Radar::Project(origin, origin, std::numeric_limits<float>::quiet_NaN(), x, y));
        assert(Radar::Project(origin, {15, 5, 20}, 5, x, y, pi / 2) &&
            Near(x, 0) && Near(y, -1));
        assert(Radar::Project(origin, {10, 5, 25}, 5, x, y, pi / 2) &&
            Near(x, -1) && Near(y, 0));
        assert(Radar::Project(origin, {5, 5, 20}, 5, x, y, -pi / 2) &&
            Near(x, 0) && Near(y, -1));
        assert(!Radar::Project(origin, origin, 5, x, y, std::numeric_limits<float>::quiet_NaN()));

        assert(Radar::MayAct(true, true, false, false, true));
        assert(Radar::MayAct(true, false, true, true, true));
        assert(!Radar::MayAct(true, false, true, false, true));
        assert(!Radar::MayAct(true, false, false, true, true));
        assert(!Radar::MayAct(true, true, true, true, false));
        assert(!Radar::MayAct(false, true, true, true, true));

        Radar::Snapshot data;
        data.sampledAt = 1000;
        data.generation = 7;
        data.actionsAllowed = true;
        Radar::Player player;
        player.actorId = 2;
        player.instance = 0x10000;
        player.lifeValid = true;
        data.players.push_back(player);
        Radar::snapshot = data;
        Radar::requests.clear();
        assert(Radar::IsFresh(data, 2500) && !Radar::IsFresh(data, 2501));
        assert(!Radar::IsFresh(data, 999));
        assert(!Radar::Queue(2, 6, Radar::Action::Kill, 1000));
        assert(!Radar::Queue(2, 7, Radar::Action::Kill, 2501));
        assert(!Radar::Queue(2, 7, Radar::Action::Revive, 1000));
        assert(!Radar::Queue(99, 7, Radar::Action::Kill, 1000));
        assert(Radar::Queue(2, 7, Radar::Action::Kill, 1000));
        Radar::snapshot.players[0].pending = false;
        assert(!Radar::Queue(2, 7, Radar::Action::Kill, 1000));
        Hooks::ResetRadar();
        assert(Radar::requests.empty() && !Radar::Read().actionsAllowed);

        Radar::snapshot = data;
        Radar::snapshot.players[0].dying = true;
        assert(!Radar::Queue(2, 7, Radar::Action::Kill, 1000));
        Radar::snapshot = data;
        Radar::snapshot.actionsAllowed = false;
        assert(!Radar::Queue(2, 7, Radar::Action::Kill, 1000));
    }

    struct Transform {
        char header[0x20]{};
        Radar::Vector3 position;
        Radar::Quaternion rotation;
    };
    struct PlayerArray {
        char header[0x18]{};
        uintptr_t count = 2;
        void* entries[3]{};
    } roster;
    PlayerArray cursedArray, rendererArray;

    std::array<char, 0x180> localAvatar{}, remoteAvatar{}, replacement{};
    std::array<char, 0x40> localSanity{}, remoteSanity{}, localOwner{}, remoteOwner{};
    std::array<char, 0x80> ghostObject{}, boneObject{};
    std::array<char, 0xA0> collectableObject{};
    std::array<char, 0x20> headObject{}, roomObject{}, otherRoom{};
    std::array<char, 0x40> curseOne{}, curseTwo{}, curseThree{}, rendererOne{}, rendererTwo{}, unrelatedModel{};
    std::array<char, 0xB0> modelObject{};
    std::array<char, 0x180> localController{};
    std::array<char, 0x100> grabController{};
    std::array<char, 0x80> doorController{};
    Transform localPose, remotePose, ghostPose, bonePose, headPose, cursePoseOne, cursePoseTwo, cursePoseThree;
    void* activeRoom = roomObject.data();
    int transformMethodMarker = 0, headMethodMarker = 0, boneTypeMarker = 0;
    int cursedTypeMarker = 0, pcClassMarker = 0, curseClassOne = 0, curseClassTwo = 0;
    bool rendererOneEnabled = false, rendererTwoEnabled = true, unrelatedEnabled = false;
    bool faultRendererRestore = false;
    bool isHost = true, isPrivate = true, isInRoom = true;
    bool replaceTarget = false, faultPosition = false, faultAction = false;
    bool rejectSanity = false, ignoreSanity = false;
    void* destroyedObject = nullptr;
    int kills = 0, revives = 0, headReads = 0;
    int localActor = 1, remoteActor = 2;
    int sanityCalls = 0, sanityRoots = 0, rpcMarker = 0, objectMarker = 0, floatMarker = 0;
    float boxedInsanity = 0;
    std::array<char, 0x30> sanityName{}, sanityParameters{}, sanityValue{};

    template<typename T, size_t N>
    void Put(std::array<char, N>& object, size_t offset, T value) {
        assert(offset + sizeof(T) <= N);
        std::memcpy(object.data() + offset, &value, sizeof(value));
    }

    void* GetAvatar(int32_t actor, void*) {
        assert(actor == localActor || actor == remoteActor);
        return actor == localActor ? localAvatar.data() : replaceTarget ? replacement.data() : remoteAvatar.data();
    }

    void* ComponentTransform(void* component, void* method) {
        assert(method == &transformMethodMarker);
        if (component == localAvatar.data()) return &localPose;
        if (component == remoteAvatar.data() || component == replacement.data()) return &remotePose;
        if (component == ghostObject.data()) return &ghostPose;
        if (component == curseOne.data()) return &cursePoseOne;
        if (component == curseTwo.data()) return &cursePoseTwo;
        if (component == curseThree.data()) return &cursePoseThree;
        assert(component == boneObject.data());
        return &bonePose;
    }

    void* GameObjectTransform(void* object, void* method) {
        assert(object == headObject.data() && method == &headMethodMarker);
        ++headReads;
        return &headPose;
    }

    void GetPosition(void* transform, Hooks::Vector3* output, void*) {
        if (faultPosition) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
        const auto& position = static_cast<Transform*>(transform)->position;
        *output = {position.x, position.y, position.z};
    }

    void ConfigureBackend() {
        Hooks::ResetRadar();
        ready = actionsReady = actionsHealthy = boneLayoutValid = true;
        worldHealthy = true;
        visibilityReady = interactionReady = false;
        ghostTransitions = interactionDepth = 0;
        pendingInteractionRestore = {};
        faultRendererRestore = false;
        cleanupRequested.store(false);
        cleanupComplete.store(false);
        Hooks::SetWorldHooksInstalled(true);
        Features::bGhostVisible.store(false);
        Features::bDeadInteraction.store(false);
        cursedType = nullptr;
        findCursed.call = nullptr;
        rosterUnavailable = replaceTarget = faultPosition = faultAction = false;
        rejectSanity = ignoreSanity = false;
        sanityCalls = sanityRoots = 0;
        sanityReady = true;
        sanityRpc = &rpcMarker;
        sanityObjectClass = &objectMarker;
        sanityFloatClass = &floatMarker;
        sanityStringNew = [](const char* name) -> void* {
            assert(std::strcmp(name, "NetworkedUpdatePlayerSanity") == 0);
            return sanityName.data();
        };
        sanityArrayNew = [](void* klass, uintptr_t count) -> void* {
            assert(klass == &objectMarker && count == 1);
            return sanityParameters.data();
        };
        sanityBox = [](void* klass, void* value) -> void* {
            assert(klass == &floatMarker);
            boxedInsanity = *static_cast<float*>(value);
            return sanityValue.data();
        };
        sanityBarrier = [](void* array, void** slot, void* value) {
            assert(array == sanityParameters.data() &&
                slot == reinterpret_cast<void**>(sanityParameters.data() + 0x20) && value == sanityValue.data());
            *slot = value;
        };
        sanityPin = [](void*, bool) -> uint32_t { return static_cast<uint32_t>(++sanityRoots); };
        sanityUnpin = [](uint32_t) { --sanityRoots; };
        sanityInvoke = [](void* method, void* view, void** arguments, void** exception) -> void* {
            assert(method == &rpcMarker && sanityRoots == 3);
            assert(arguments[0] == sanityName.data() && *static_cast<int*>(arguments[1]) == 0 &&
                arguments[2] == sanityParameters.data());
            ++sanityCalls;
            if (faultAction) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
            if (rejectSanity) *exception = &rpcMarker;
            else if (!ignoreSanity) {
                if (view == localOwner.data()) Put(localSanity, 0x30, boxedInsanity);
                else { assert(view == remoteOwner.data()); Put(remoteSanity, 0x30, boxedInsanity); }
            }
            return nullptr;
        };
        destroyedObject = nullptr;
        isHost = isPrivate = isInRoom = true;
        localActor = 1; remoteActor = 2;
        kills = revives = headReads = 0;
        localAvatar.fill(0);
        remoteAvatar.fill(0);
        replacement.fill(0);
        ghostObject.fill(0);
        Put(localAvatar, 0x40, static_cast<void*>(headObject.data()));
        Put(remoteAvatar, 0x40, static_cast<void*>(headObject.data()));
        Put(localAvatar, 0x90, static_cast<void*>(localSanity.data()));
        Put(remoteAvatar, 0x90, static_cast<void*>(remoteSanity.data()));
        Put(localSanity, 0x28, static_cast<void*>(localAvatar.data()));
        Put(remoteSanity, 0x28, static_cast<void*>(remoteAvatar.data()));
        Put(localSanity, 0x20, static_cast<void*>(localOwner.data()));
        Put(remoteSanity, 0x20, static_cast<void*>(remoteOwner.data()));
        Put(localSanity, 0x30, 23.0f);
        Put(remoteSanity, 0x30, 100.0f);
        Put(boneObject, 0x60, static_cast<void*>(collectableObject.data()));
        Put(collectableObject, 0x9C, uint8_t{0});
        localPose.position = {10, 2, 0};
        remotePose.position = {15, 2, 0};
        ghostPose.position = {20, 3, 0};
        bonePose.position = {10, 2, 5};
        headPose.rotation = {0, 1, 0, 1};
        roster.count = 2;
        roster.entries[0] = localOwner.data();
        roster.entries[1] = remoteOwner.data();
        activeRoom = roomObject.data();
        Hooks::gCurrentGhostAI = reinterpret_cast<Hooks::GhostAI_o*>(ghostObject.data());
        Hooks::oGetTransform = ComponentTransform;
        Hooks::getTransformMethodInfo = &transformMethodMarker;
        Hooks::oGetPositionInjected = GetPosition;
        headTransform.call = GameObjectTransform;
        headTransform.info = &headMethodMarker;
        objectAlive.call = [](void* object, void*) { return object && object != destroyedObject; };
        rotation.call = [](void* transform, Radar::Quaternion* output, void*) {
            *output = static_cast<Transform*>(transform)->rotation;
        };
        playerList.call = [](void*) -> void* { return &roster; };
        localPlayer.call = [](void*) -> void* { return localOwner.data(); };
        currentRoom.call = [](void*) -> void* { return activeRoom; };
        inRoom.call = [](void*) { return isInRoom; };
        offlineMode.call = [](void*) { return false; };
        masterClient.call = [](void*) { return isHost; };
        roomVisible.call = [](void*, void*) { return !isPrivate; };
        actorNumber.call = [](void* owner, void*) -> int32_t {
            assert(owner == localOwner.data() || owner == remoteOwner.data());
            return owner == localOwner.data() ? localActor : remoteActor;
        };
        nickName.call = [](void*, void*) -> void* { return nullptr; };
        gamePlayer.call = GetAvatar;
        boneType = &boneTypeMarker;
        SDK::type_get_object = [](const void* type) -> void* {
            assert(type == &boneTypeMarker || type == &cursedTypeMarker);
            return const_cast<void*>(type);
        };
        SDK::object_get_class = [](void* object) -> void* {
            if (object == localController.data()) return &pcClassMarker;
            if (object == curseOne.data() || object == curseThree.data()) return &curseClassOne;
            if (object == curseTwo.data()) return &curseClassTwo;
            return nullptr;
        };
        SDK::class_get_name = [](void* klass) -> const char* {
            if (klass == &curseClassOne) return "MusicBox";
            if (klass == &curseClassTwo) return "OuijaBoard";
            return "LocalPCPlayer";
        };
        findBone.call = [](void* type, void*) -> void* {
            assert(type == &boneTypeMarker);
            return boneObject.data();
        };
        beginDeath.call = [](void* player, int32_t deathType, void*) {
            assert(player == remoteAvatar.data() && deathType == 1);
            ++kills;
            if (faultAction) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
        };
        revivePlayer.call = [](void* player, bool removeHighPriestess, void*) {
            assert(player == remoteAvatar.data() && !removeHighPriestess);
            ++revives;
        };
    }

    Radar::Snapshot Sample() {
        lastSample = 0;
        Hooks::UpdateRadar();
        return Radar::Read();
    }

    void TestCursedObjects() {
        ConfigureBackend();
        cursedType = &cursedTypeMarker;
        cursedArray.count = 3;
        cursedArray.entries[0] = curseOne.data();
        cursedArray.entries[1] = curseTwo.data();
        cursedArray.entries[2] = curseThree.data();
        cursePoseOne.position = {11, 2, 1};
        cursePoseTwo.position = {12, 3, 2};
        cursePoseThree.position = {13, 4, 3};
        findCursed.call = [](void* type, void*) -> void* {
            assert(type == &cursedTypeMarker);
            return &cursedArray;
        };
        auto data = Sample();
        assert(data.cursedObjects.size() == 3);
        assert(data.cursedObjects[0].name == "Music box" && data.cursedObjects[1].name == "Ouija board");
        assert(data.cursedObjects[2].name == "Music box" && data.cursedObjects[2].pose.position.x == 13);
        destroyedObject = curseTwo.data();
        assert(Sample().cursedObjects.size() == 2);
        cursePoseThree.position.x = std::numeric_limits<float>::quiet_NaN();
        assert(Sample().cursedObjects.size() == 1);
        cursedArray.count = 129;
        data = Sample();
        assert(data.cursedObjects.empty() && data.message.find("Cursed-object") != std::string::npos);
        cursedArray.count = 0;
        assert(Sample().cursedObjects.empty());
        Hooks::ResetRadar();
        assert(Radar::Read().cursedObjects.empty());
    }

    void ConfigureWorld() {
        ConfigureBackend();
        visibilityReady = interactionReady = true;
        localPCClass = &pcClassMarker;
        modelObject.fill(0);
        localController.fill(0);
        grabController.fill(0);
        doorController.fill(0);
        Put(ghostObject, 0x60, static_cast<void*>(modelObject.data()));
        Put(modelObject, 0x28, static_cast<void*>(ghostObject.data()));
        Put(modelObject, 0x30, static_cast<void*>(&rendererArray));
        rendererArray.count = 2;
        rendererArray.entries[0] = rendererOne.data();
        rendererArray.entries[1] = rendererTwo.data();
        rendererOneEnabled = false;
        rendererTwoEnabled = true;
        rendererEnabled.call = [](void* renderer, void*) {
            assert(renderer == rendererOne.data() || renderer == rendererTwo.data());
            return renderer == rendererOne.data() ? rendererOneEnabled : rendererTwoEnabled;
        };
        setRendererEnabled.call = [](void* renderer, bool enabled, void*) {
            assert(renderer == rendererOne.data() || renderer == rendererTwo.data());
            if (faultRendererRestore && !enabled)
                RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
            if (renderer == rendererOne.data()) rendererOneEnabled = enabled;
            else rendererTwoEnabled = enabled;
        };
        Hooks::oGhostVisibility = [](void* model, bool visible, void*) {
            if (model == unrelatedModel.data()) unrelatedEnabled = visible;
            else {
                assert(model == modelObject.data());
                rendererOneEnabled = rendererTwoEnabled = visible;
            }
        };
        Put(localAvatar, 0x110, static_cast<void*>(localController.data()));
        Put(localController, 0x20, static_cast<void*>(localAvatar.data()));
        Put(localController, 0x148, static_cast<void*>(grabController.data()));
        Put(localController, 0x158, static_cast<void*>(doorController.data()));
        Put(grabController, 0x20, static_cast<void*>(localController.data()));
        Put(doorController, 0x58, static_cast<void*>(localController.data()));
    }

    void TestLobbyPickup() {
        ConfigureWorld();
        Hooks::gCurrentGhostAI = nullptr;
        activeRoom = nullptr;
        isHost = isPrivate = isInRoom = false;
        Put(localController, 0x20, static_cast<void*>(nullptr));
        Put(localController, 0x30, static_cast<void*>(headObject.data()));
        Put(localController, 0x128, static_cast<void*>(headObject.data()));
        gamePlayer.call = [](int32_t, void*) -> void* {
            assert(false); // Lobby pickup must not dereference the match's Network singleton.
            return nullptr;
        };
        staminaLocal = nullptr;
        assert(UpdatePickup(grabController.data()));
        assert(pickupAllowed && pickupCamera == headObject.data() &&
            pickupLocal == localController.data() && staminaLocal == localController.data() &&
            recoveryLocal == localController.data() && recoveryEligible);
        GhostControl::desired.store(true);
        assert(UpdatePickup(grabController.data()) && !recoveryEligible);
        GhostControl::desired.store(false);
        consumePickup = true;
        pickupInputs = 0;
        Hooks::worldInputHooks[1].original = [](void*, void*, void*) { assert(false); };
        Hooks::worldInputHooks[1].hook(grabController.data(), nullptr, nullptr);
        assert(pickupInputs == 1 && pickupAllowed);
        consumePickup = false;
        controlOwnsPlayer = true;
        assert(!UpdatePickup(grabController.data()) && !pickupAllowed && !recoveryEligible);
        controlOwnsPlayer = false;
        cleanupRequested.store(true);
        assert(!UpdatePickup(grabController.data()) && !pickupAllowed && !recoveryEligible);
        recoveryRestores = false;
        ServiceWorldOptions();
        assert(!cleanupComplete.load());
        recoveryRestores = true;
        ServiceWorldOptions();
        assert(cleanupComplete.load());
        cleanupComplete.store(false);
        cleanupRequested.store(false);
        Put(localController, 0x148, static_cast<void*>(nullptr));
        assert(!UpdatePickup(grabController.data()) && !pickupAllowed);
    }

    void TestVisibility() {
        ConfigureWorld();
        Features::bGhostVisible.store(true);
        assert(Sample().ghostVisibilityActive && rendererOneEnabled && rendererTwoEnabled);
        Features::bGhostVisible.store(false);
        assert(!Sample().ghostVisibilityActive && !rendererOneEnabled && rendererTwoEnabled);
        Features::bGhostVisible.store(true);
        Sample();
        Hooks::hkGhostVisibility(modelObject.data(), false, nullptr);
        assert(rendererOneEnabled && rendererTwoEnabled);
        Features::bGhostVisible.store(false);
        Sample();
        assert(!rendererOneEnabled && !rendererTwoEnabled);
        Features::bGhostVisible.store(true);
        Sample();
        Hooks::hkGhostVisibility(unrelatedModel.data(), false, nullptr);
        assert(!unrelatedEnabled && rendererOneEnabled);
        assert(Hooks::BeginGhostTransition(ghostObject.data()));
        assert(!rendererOneEnabled && !rendererTwoEnabled);
        Hooks::hkGhostVisibility(modelObject.data(), false, nullptr);
        Sample();
        assert(!rendererOneEnabled && !rendererTwoEnabled);
        Hooks::EndGhostTransition();
        assert(Sample().ghostVisibilityActive && rendererOneEnabled);
        Put(ghostObject, 0x30, int32_t{2});
        assert(!Sample().ghostVisibilityActive && !rendererOneEnabled && !rendererTwoEnabled);
        Hooks::hkGhostVisibility(modelObject.data(), true, nullptr);
        assert(rendererOneEnabled && rendererTwoEnabled);
        Hooks::hkGhostVisibility(modelObject.data(), false, nullptr);
        assert(!rendererOneEnabled && !rendererTwoEnabled);
        Put(ghostObject, 0x30, int32_t{0});
        assert(Sample().ghostVisibilityActive);
        isHost = false;
        assert(!Sample().ghostVisibilityActive && !rendererOneEnabled);
        isHost = true;
        Sample();
        Hooks::gCurrentGhostAI = nullptr;
        assert(!Sample().ghostVisibilityActive && !rendererOneEnabled);

        ConfigureWorld();
        Features::bGhostVisible.store(true);
        Sample();
        faultRendererRestore = true;
        cleanupRequested.store(true);
        Hooks::UpdateRadar();
        assert(!cleanupComplete.load() && !worldHealthy && visibleModel);
        faultRendererRestore = false;
        Hooks::UpdateRadar();
        assert(cleanupComplete.load() && !visibleModel && !rendererOneEnabled && rendererTwoEnabled);
        cleanupRequested.store(false);

        ConfigureWorld();
        Features::bGhostVisible.store(true);
        Sample();
        bool unloaded = false;
        std::thread unload([&unloaded] { unloaded = Hooks::PrepareWorldOptionsForUnload(); });
        const uint64_t deadline = GetTickCount64() + 1000;
        while (!cleanupRequested.load() && GetTickCount64() < deadline) Sleep(1);
        assert(cleanupRequested.load());
        Hooks::UpdateRadar();
        unload.join();
        assert(unloaded && !visibleModel && !rendererOneEnabled && rendererTwoEnabled);
        assert(!Features::bGhostVisible.load() && !Features::bDeadInteraction.load());
        cleanupRequested.store(false);

        ConfigureWorld();
        Features::bGhostVisible.store(true);
        Sample();
        assert(!Hooks::PrepareWorldOptionsForUnload());
        assert(visibleModel && !cleanupRequested.load());
        Sample();
        assert(!visibleModel && !rendererOneEnabled && rendererTwoEnabled);
        ConfigureWorld();
        Features::bGhostVisible.store(true);
        Sample();
        destroyedObject = rendererOne.data();
        Features::bGhostVisible.store(false);
        Sample();
        assert(rendererTwoEnabled && originalRenderers.empty());

        ConfigureWorld();
        Features::bGhostVisible.store(true);
        rendererArray.count = 129;
        Sample();
        assert(!Features::bGhostVisible.load() && originalRenderers.empty());

        ConfigureWorld();
        Features::bGhostVisible.store(true);
        Sample();
        cleanupRequested.store(true);
        Hooks::UpdateRadar();
        assert(cleanupComplete.load() && !visibleModel && !rendererOneEnabled && rendererTwoEnabled);
        cleanupRequested.store(false);
        Features::bGhostVisible.store(false);
        Hooks::SetWorldHooksInstalled(false);
        Features::bGhostVisible.store(true);
        assert(!Sample().ghostVisibilityActive && !rendererOneEnabled);
    }

    void AssertInteractionScope(void* instance, void*) {
        assert(instance == grabController.data() || instance == doorController.data());
        uint8_t actualDead = 0, localDead = 1, mode = 0, blocked = 0;
        assert(ReadField(localAvatar.data(), 0x28, actualDead) && actualDead == 1);
        assert(ReadField(localController.data(), 0x38, localDead) && localDead == 0);
        assert(ReadField(grabController.data(), 0xE0, mode) && mode == 1);
        assert(ReadField(grabController.data(), 0xE1, blocked) && blocked == 1);
    }

    bool InvokeFaultingInput() {
        __try {
            Hooks::worldInputHooks[0].hook(grabController.data(), nullptr, nullptr);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    void TestDeadInteraction() {
        ConfigureWorld();
        Put(localAvatar, 0x28, uint8_t{1});
        Put(localController, 0x38, uint8_t{1});
        Put(grabController, 0xE1, uint8_t{1});
        Features::bDeadInteraction.store(true);
        assert(Sample().deadInteractionAvailable && Sample().deadInteractionActive);
        for (auto& input : Hooks::worldInputHooks) {
            input.original = [](void* instance, void*, void*) { AssertInteractionScope(instance, nullptr); };
            const bool door = std::strcmp(input.type, "DragRigidbodyUse") == 0;
            input.hook(door ? doorController.data() : grabController.data(), nullptr, nullptr);
            assert(DeadController(localAvatar.data(), localController.data()) && interactionDepth == 0);
            uint8_t mode = 1;
            assert(ReadField(grabController.data(), 0xE0, mode) && mode == 0);
        }
        Hooks::oPropGrabUpdate = AssertInteractionScope;
        Hooks::oDoorFixedUpdate = AssertInteractionScope;
        Hooks::hkPropGrabUpdate(grabController.data(), nullptr);
        Hooks::hkDoorFixedUpdate(doorController.data(), nullptr);
        assert(DeadController(localAvatar.data(), localController.data()));
        assert(!BeginInteraction(remoteAvatar.data(), false).changed);
        Features::bDeadInteraction.store(false);
        assert(!BeginInteraction(grabController.data(), false).changed);
        Features::bDeadInteraction.store(true);
        isHost = false;
        assert(!BeginInteraction(grabController.data(), false).changed);
        isHost = true;
        isPrivate = false;
        assert(!BeginInteraction(grabController.data(), false).changed);
        isPrivate = true;
        Put(localAvatar, 0x28, uint8_t{0});
        assert(!BeginInteraction(grabController.data(), false).changed);
        Put(localAvatar, 0x28, uint8_t{1});
        Put(localController, 0x38, uint8_t{0});
        assert(!BeginInteraction(grabController.data(), false).changed);
        Put(localController, 0x38, uint8_t{1});
        localPCClass = nullptr;
        assert(!Sample().deadInteractionAvailable && !BeginInteraction(grabController.data(), false).changed);
        localPCClass = &pcClassMarker;
        cleanupRequested.store(true);
        assert(!BeginInteraction(grabController.data(), false).changed);
        cleanupRequested.store(false);
        Hooks::worldInputHooks[0].original = [](void*, void*, void*) {
            RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
        };
        assert(!InvokeFaultingInput());
        assert(DeadController(localAvatar.data(), localController.data()) && interactionDepth == 0);
        uint8_t mode = 1;
        assert(ReadField(grabController.data(), 0xE0, mode) && mode == 0);
        Hooks::worldInputHooks[0].original = [](void*, void*, void*) {
            Put(localAvatar, 0x28, uint8_t{0});
            Put(localController, 0x38, uint8_t{0});
            Put(grabController, 0xE0, uint8_t{1});
        };
        Hooks::worldInputHooks[0].hook(grabController.data(), nullptr, nullptr);
        assert(!DeadController(localAvatar.data(), localController.data()) && interactionDepth == 0);
        assert(ReadField(grabController.data(), 0xE0, mode) && mode == 1);

        ConfigureWorld();
        Put(localAvatar, 0x28, uint8_t{1});
        Put(localController, 0x38, uint8_t{1});
        Features::bDeadInteraction.store(true);
        Hooks::worldInputHooks[0].original = [](void*, void*, void*) {
            Put(localAvatar, 0x28, uint8_t{255});
        };
        Hooks::worldInputHooks[0].hook(grabController.data(), nullptr, nullptr);
        assert(pendingInteractionRestore.changed && !worldHealthy && interactionDepth == 0);
        cleanupRequested.store(true);
        Hooks::UpdateRadar();
        assert(!cleanupComplete.load());
        Put(localAvatar, 0x28, uint8_t{1});
        Hooks::UpdateRadar();
        assert(cleanupComplete.load() && !pendingInteractionRestore.changed);
        assert(DeadController(localAvatar.data(), localController.data()));
        assert(ReadField(grabController.data(), 0xE0, mode) && mode == 0);
        cleanupRequested.store(false);
    }

    void TestBackend() {
        ConfigureBackend();
        auto data = Sample();
        assert(data.players.size() == 2 && data.actionsAllowed);
        assert(data.center.positionValid && data.center.position.x == 10);
        assert(data.ghost.positionValid && data.ghost.position.x == 20);
        assert(data.bone.positionValid && data.bone.position.z == 5);
        assert(headReads == 2 && data.players[1].pose.headingValid &&
            Near(data.players[1].pose.heading, pi / 2));
        assert(data.players[0].sanityValid && data.players[0].sanity == 77);
        assert(data.players[1].sanityValid && data.players[1].sanity == 0);
        Put(remoteSanity, 0x30, std::numeric_limits<float>::quiet_NaN());
        assert(!Sample().players[1].sanityValid);
        Put(remoteSanity, 0x30, 101.0f);
        assert(!Sample().players[1].sanityValid);
        Put(collectableObject, 0x9C, uint8_t{1});
        assert(!Sample().bone.positionValid);

        assert(Radar::Queue(2, data.generation, Radar::Action::Kill, GetTickCount64()));
        isHost = false;
        data = Sample();
        assert(!data.actionsAllowed && kills == 0 && Radar::requests.empty());
        isHost = true;
        data = Sample();
        assert(Radar::Queue(2, data.generation, Radar::Action::Kill, GetTickCount64()));
        replaceTarget = true;
        Sample();
        assert(kills == 0);
        replaceTarget = false;
        data = Sample();
        assert(Radar::Queue(2, data.generation, Radar::Action::Kill, GetTickCount64()));
        data = Sample();
        assert(kills == 1 && data.players[1].pending);
        assert(!Radar::Queue(2, data.generation, Radar::Action::Kill, GetTickCount64()));
        Put(remoteAvatar, 0x29, uint8_t{1});
        assert(Sample().players[1].pending);
        Put(remoteAvatar, 0x28, uint8_t{1});
        data = Sample();
        assert(data.players[1].dead && !data.players[1].pending);
        assert(data.players[1].pose.positionValid && data.players[1].pose.position.x == 15);
        assert(data.players[1].pose.headingValid && Near(data.players[1].pose.heading, pi / 2));
        assert(data.message.find("confirmed") != std::string::npos);
        assert(Radar::Queue(2, data.generation, Radar::Action::Revive, GetTickCount64()));
        data = Sample();
        assert(revives == 1 && data.players[1].pending);
        pending.front().deadline = GetTickCount64();
        data = Sample();
        assert(!data.players[1].pending && revives == 1);
        assert(data.message.find("not retried") != std::string::npos);

        ConfigureBackend();
        data = Sample();
        assert(Radar::Queue(2, data.generation, Radar::Action::Kill, GetTickCount64()));
        activeRoom = otherRoom.data();
        const auto changed = Sample();
        assert(changed.generation != data.generation && kills == 0 && Radar::requests.empty());
        assert(!Radar::Queue(2, data.generation, Radar::Action::Kill, GetTickCount64()));

        data = Sample();
        assert(Radar::Queue(2, data.generation, Radar::Action::Kill, GetTickCount64()));
        roster.count = 17;
        data = Sample();
        assert(!data.actionsAllowed && data.players.empty() && kills == 0);

        ConfigureBackend();
        data = Sample();
        assert(Radar::Queue(2, data.generation, Radar::Action::Kill, GetTickCount64()));
        roster.entries[0] = nullptr;
        data = Sample();
        assert(!data.actionsAllowed && data.players.empty() && kills == 0);

        ConfigureBackend();
        data = Sample();
        assert(Radar::Queue(2, data.generation, Radar::Action::Kill, GetTickCount64()));
        isInRoom = false;
        data = Sample();
        assert(!data.actionsAllowed && data.players.empty() && kills == 0);

        ConfigureBackend();
        data = Sample();
        assert(Radar::Queue(2, data.generation, Radar::Action::Kill, GetTickCount64()));
        destroyedObject = remoteAvatar.data();
        data = Sample();
        assert(!data.players[1].lifeValid && !data.players[1].pose.positionValid && kills == 0);

        ConfigureBackend();
        data = Sample();
        assert(Radar::Queue(2, data.generation, Radar::Action::Kill, GetTickCount64()));
        Put(remoteAvatar, 0x29, uint8_t{1});
        data = Sample();
        assert(data.players[1].dying && kills == 0);

        ConfigureBackend();
        data = Sample();
        assert(Radar::Queue(2, data.generation, Radar::Action::Kill, GetTickCount64()));
        faultAction = true;
        data = Sample();
        assert(ready && !data.actionsAllowed && pending.empty() && kills == 1);
        assert(data.message.find("Native player action failed") != std::string::npos);
        assert(!Radar::Queue(2, data.generation, Radar::Action::Kill, GetTickCount64()));
        Sample();
        assert(kills == 1);

        ConfigureBackend();
        Hooks::gCurrentGhostAI = nullptr;
        data = Sample();
        assert(!data.actionsAllowed && !data.center.positionValid && !data.ghost.positionValid);
        assert(data.players.size() == 2 && !data.players[0].lifeValid);

        ConfigureBackend();
        faultPosition = true;
        data = Sample();
        assert(!ready && !data.actionsAllowed && !data.sampledAt && Radar::requests.empty());
        assert(data.message.find("access violation") != std::string::npos);
    }

    void TestSanityControl() {
        ConfigureBackend();
        auto data = Sample();
        assert(data.sanityAvailable && data.remoteSanityAllowed);
        assert(!Radar::Queue(2, data.generation, Radar::Action::SetSanity, GetTickCount64(), -1));
        assert(!Radar::Queue(2, data.generation, Radar::Action::SetSanity, GetTickCount64(), 101));
        assert(Radar::Queue(2, data.generation, Radar::Action::SetSanity, GetTickCount64(), 100));
        data = Sample();
        assert(sanityCalls == 1 && sanityRoots == 0 && Near(boxedInsanity, 0) && data.players[1].pending);
        data = Sample();
        assert(Near(data.players[1].sanity, 100) && !data.players[1].pending);
        assert(Radar::Queue(1, data.generation, Radar::Action::SetSanity, GetTickCount64(), 0));
        Sample();
        data = Sample();
        assert(sanityCalls == 2 && sanityRoots == 0 && Near(data.players[0].sanity, 0) && !data.players[0].pending);

        assert(Radar::Queue(2, data.generation, Radar::Action::SetSanity, GetTickCount64(), 50));
        isHost = false;
        data = Sample();
        assert(sanityCalls == 2 && !data.actionsAllowed);
        ConfigureBackend();
        data = Sample();
        assert(Radar::Queue(2, data.generation, Radar::Action::SetSanity, GetTickCount64(), 50));
        replaceTarget = true;
        Sample();
        assert(!sanityCalls);

        ConfigureBackend();
        data = Sample();
        assert(Radar::Queue(2, data.generation, Radar::Action::SetSanity, GetTickCount64(), 50));
        Put(remoteSanity, 0x20, static_cast<void*>(nullptr));
        data = Sample();
        assert(data.actionsAllowed && !data.players[1].pending && !sanityCalls);
        assert(data.message.find("unavailable") != std::string::npos);

        ConfigureBackend();
        ignoreSanity = true;
        data = Sample();
        assert(Radar::Queue(2, data.generation, Radar::Action::SetSanity, GetTickCount64(), 50));
        data = Sample();
        assert(data.players[1].pending && sanityCalls == 1);
        pending.front().deadline = GetTickCount64();
        data = Sample();
        assert(!data.players[1].pending && sanityCalls == 1 && data.message.find("not retried") != std::string::npos);

        ConfigureBackend();
        data = Sample();
        assert(Radar::Queue(2, data.generation, Radar::Action::SetSanity, GetTickCount64(), 50));
        rejectSanity = true;
        data = Sample();
        assert(!data.actionsAllowed && sanityCalls == 1 && !sanityRoots && pending.empty());
        assert(data.message.find("failed") != std::string::npos);

        ConfigureBackend();
        data = Sample();
        assert(Radar::Queue(1, data.generation, Radar::Action::SetSanity, GetTickCount64(), 50));
        faultAction = true;
        data = Sample();
        assert(!data.actionsAllowed && sanityCalls == 1 && !sanityRoots && pending.empty());

        ConfigureBackend();
        data = Sample();
        Radar::snapshot.remoteSanityAllowed = false;
        assert(!Radar::Queue(2, data.generation, Radar::Action::SetSanity, GetTickCount64(), 50));
        ConfigureBackend();
        localActor = 2; remoteActor = 1;
        data = Sample();
        assert(data.actionsAllowed && !data.remoteSanityAllowed);
        assert(!Radar::Queue(1, data.generation, Radar::Action::SetSanity, GetTickCount64(), 50));
        assert(Radar::Queue(2, data.generation, Radar::Action::SetSanity, GetTickCount64(), 50));
        Sample();
        data = Sample();
        assert(sanityCalls == 1 && Near(data.players[0].sanity, 50) && !data.players[0].pending);
        sanityReady = false;
        data = Sample();
        assert(!data.sanityAvailable);
        assert(!Radar::Queue(2, data.generation, Radar::Action::SetSanity, GetTickCount64(), 50));
    }
}

int main() {
    TestMathAndQueue();
    TestBackend();
    TestSanityControl();
    TestCursedObjects();
    TestLobbyPickup();
    TestVisibility();
    TestDeadInteraction();
}
