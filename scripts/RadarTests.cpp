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
    void* blockedInputOwner = nullptr;
}

void GhostControl::Initialize(void*, void*, void*, void*) {}
void GhostControl::SetHooksInstalled(bool) {}
void GhostControl::Update(const Context&) {}
void GhostControl::PlayerUpdate(UpdateFunction original, void* instance, void* info) { original(instance, info); }
GhostControl::Snapshot GhostControl::Read() { return {}; }
bool GhostControl::Restore() { return true; }
bool GhostControl::ControlsPlayer(void*) { return controlOwnsPlayer; }
bool GhostControl::BlocksInput(void* instance, bool door) {
    return !door && blockedInputOwner && instance == blockedInputOwner;
}
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
    struct ReviveSources {
        char header[0x18]{};
        uintptr_t count = 1;
        int32_t entries[8]{};
    } reviveSources;
    std::array<char, 0x40> levelValuesObject{};
    std::array<char, 0xB8> difficultyObject{};

    std::array<char, 0x180> localAvatar{}, remoteAvatar{}, replacement{};
    std::array<char, 0xC8> localCorpse{}, remoteCorpse{};
    std::array<char, 0x120> localAnimations{}, remoteAnimations{};
    std::array<char, 0x20> localAnimator{}, remoteAnimator{};
    std::array<char, 0x40> localSanity{}, remoteSanity{}, localOwner{}, remoteOwner{};
    std::array<char, 0x80> ghostObject{}, boneObject{};
    std::array<char, 0xA0> collectableObject{};
    std::array<char, 0x20> headObject{}, roomObject{}, otherRoom{};
    std::array<char, 0x40> curseOne{}, curseTwo{}, curseThree{}, rendererOne{}, rendererTwo{}, unrelatedModel{};
    std::array<char, 0xB0> modelObject{};
    std::array<char, 0x180> localController{};
    std::array<char, 0x100> grabController{};
    std::array<char, 0x80> doorController{};
    std::array<char, 0x48> flashlightController{}, remoteFlashlight{};
    std::array<char, 0xC0> journalObject{};
    std::array<char, 0x48> menuController{};
    std::array<char, 0xA0> movementController{};
    Transform localPose, remotePose, ghostPose, bonePose, headPose, cursePoseOne, cursePoseTwo, cursePoseThree;
    void* activeRoom = roomObject.data();
    int transformMethodMarker = 0, headMethodMarker = 0, boneTypeMarker = 0;
    int cursedTypeMarker = 0, pcClassMarker = 0, curseClassOne = 0, curseClassTwo = 0;
    bool rendererOneEnabled = false, rendererTwoEnabled = true, unrelatedEnabled = false;
    bool faultRendererRestore = false;
    bool isHost = true, isPrivate = true, isInRoom = true;
    bool replaceTarget = false, faultPosition = false, faultAction = false;
    bool rejectSanity = false, ignoreSanity = false;
    bool reviveRpc = false, rejectRevive = false;
    void* expectedReviveView = nullptr;
    void* destroyedObject = nullptr;
    int kills = 0, revives = 0, headReads = 0;
    int lastDeathType = -1, cursorMode = 1;
    bool cursorShown = false;
    void* lastDeathPlayer = nullptr;
    unsigned nativeInputs = 0;
    bool faultCursorRestore = false;
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
        overlayReady = false;
        overlayHealthy = true;
        cursorState = {};
        pendingMovementRestore = {};
        Features::bOverlayInput.store(false);
        GhostControl::inputCaptured.store(false);
        inputPhase.call = nullptr;
        ghostTransitions = interactionDepth = movementDepth = 0;
        pendingInteractionRestore = {};
        faultRendererRestore = false;
        blockedInputOwner = nullptr;
        cleanupRequested.store(false);
        cleanupComplete.store(false);
        Hooks::SetWorldHooksInstalled(true);
        Features::bGhostVisible.store(false);
        Features::bDeadInteraction.store(false);
        cursedType = nullptr;
        findCursed.call = nullptr;
        rosterUnavailable = replaceTarget = faultPosition = faultAction = false;
        rejectSanity = ignoreSanity = false;
        reviveRpc = rejectRevive = false;
        expectedReviveView = remoteOwner.data();
        sanityCalls = sanityRoots = 0;
        sanityReady = true;
        reviveReady = true;
        levelValues.call = [](void*) -> void* { return levelValuesObject.data(); };
        reviveSources = {};
        reviveSources.entries[0] = 1;
        Put(levelValuesObject, 0x38, static_cast<void*>(difficultyObject.data()));
        Put(difficultyObject, 0xB0, static_cast<void*>(&reviveSources));
        playerRpc = &rpcMarker;
        rpcObjectClass = &objectMarker;
        sanityFloatClass = &floatMarker;
        rpcStringNew = [](const char* name) -> void* {
            reviveRpc = std::strcmp(name, "Revive") == 0;
            assert(reviveRpc || std::strcmp(name, "NetworkedUpdatePlayerSanity") == 0);
            return sanityName.data();
        };
        rpcArrayNew = [](void* klass, uintptr_t count) -> void* {
            assert(klass == &objectMarker && count == (reviveRpc ? 0 : 1));
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
        rpcPin = [](void*, bool) -> uint32_t { return static_cast<uint32_t>(++sanityRoots); };
        rpcUnpin = [](uint32_t) { --sanityRoots; };
        rpcInvoke = [](void* method, void* view, void** arguments, void** exception) -> void* {
            assert(method == &rpcMarker && sanityRoots == (reviveRpc ? 2 : 3));
            assert(arguments[0] == sanityName.data() &&
                *static_cast<int*>(arguments[1]) == (reviveRpc ? 5 : 0) &&
                arguments[2] == sanityParameters.data());
            if (reviveRpc) {
                assert(view == expectedReviveView);
                ++revives;
                if (faultAction) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
                if (rejectRevive) *exception = &rpcMarker;
                return nullptr;
            }
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
        lastDeathType = -1;
        lastDeathPlayer = nullptr;
        nativeInputs = 0;
        faultCursorRestore = false;
        localAvatar.fill(0);
        remoteAvatar.fill(0);
        replacement.fill(0);
        ghostObject.fill(0);
        Put(localAvatar, 0xD8, static_cast<void*>(localCorpse.data()));
        Put(remoteAvatar, 0xD8, static_cast<void*>(remoteCorpse.data()));
        Put(localAvatar, 0x100, static_cast<void*>(localAnimations.data()));
        Put(remoteAvatar, 0x100, static_cast<void*>(remoteAnimations.data()));
        Put(localAnimations, 0x28, static_cast<void*>(localAvatar.data()));
        Put(remoteAnimations, 0x28, static_cast<void*>(remoteAvatar.data()));
        Put(localAnimations, 0x110, static_cast<void*>(localAnimator.data()));
        Put(remoteAnimations, 0x110, static_cast<void*>(remoteAnimator.data()));
        Put(localCorpse, 0x40, static_cast<void*>(localAnimator.data()));
        Put(remoteCorpse, 0x40, static_cast<void*>(remoteAnimator.data()));
        Put(localCorpse, 0xC0, static_cast<void*>(localAvatar.data()));
        Put(remoteCorpse, 0xC0, static_cast<void*>(remoteAvatar.data()));
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
            assert(player == remoteAvatar.data() || player == localAvatar.data());
            lastDeathType = deathType;
            lastDeathPlayer = player;
            ++kills;
            if (faultAction) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
        };
        playerView.call = [](void* player, void*) -> void* {
            assert(player == localAvatar.data() || player == remoteAvatar.data());
            return player == localAvatar.data() ? localOwner.data() : remoteOwner.data();
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
        flashlightController.fill(0);
        remoteFlashlight.fill(0);
        journalObject.fill(0);
        menuController.fill(0);
        movementController.fill(0);
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
        Put(localController, 0x28, static_cast<void*>(journalObject.data()));
        Put(localController, 0x130, static_cast<void*>(menuController.data()));
        Put(menuController, 0x28, static_cast<void*>(localController.data()));
        Put(localController, 0x128, static_cast<void*>(movementController.data()));
        Put(localController, 0x148, static_cast<void*>(grabController.data()));
        Put(localController, 0x158, static_cast<void*>(doorController.data()));
        Put(grabController, 0x20, static_cast<void*>(localController.data()));
        Put(doorController, 0x58, static_cast<void*>(localController.data()));
        Put(localController, 0x168, static_cast<void*>(flashlightController.data()));
        Put(flashlightController, 0x20, static_cast<void*>(grabController.data()));
        Put(flashlightController, 0x28, static_cast<void*>(localController.data()));
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
        assert(instance == grabController.data() || instance == doorController.data() ||
            instance == flashlightController.data());
        uint8_t actualDead = 0, localDead = 1, mode = 0, blocked = 0;
        assert(ReadField(localAvatar.data(), 0x28, actualDead) && actualDead == 1);
        assert(ReadField(localController.data(), 0x38, localDead) && localDead == 0);
        assert(ReadField(grabController.data(), 0xE0, mode) && mode == 1);
        assert(ReadField(grabController.data(), 0xE1, blocked) && blocked == 1);
    }

    bool InvokeFaultingInput(size_t index = 0, void* instance = grabController.data()) {
        __try {
            Hooks::worldInputHooks[index].hook(instance, nullptr, nullptr);
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
            const bool torch = std::strcmp(input.type, "PCFlashlight") == 0;
            input.hook(torch ? flashlightController.data() :
                door ? doorController.data() : grabController.data(), nullptr, nullptr);
            assert(DeadController(localAvatar.data(), localController.data()) && interactionDepth == 0);
            uint8_t mode = 1;
            assert(ReadField(grabController.data(), 0xE0, mode) && mode == 0);
        }
        Hooks::oPropGrabUpdate = [](void* instance, void*) {
            assert(instance == grabController.data());
            assert(DeadController(localAvatar.data(), localController.data()));
            uint8_t mode = 0;
            assert(ReadField(instance, 0xE0, mode) && mode == 1);
        };
        Hooks::oDoorFixedUpdate = AssertInteractionScope;
        Hooks::hkPropGrabUpdate(grabController.data(), nullptr);
        Hooks::hkDoorFixedUpdate(doorController.data(), nullptr);
        assert(DeadController(localAvatar.data(), localController.data()));
        assert(!BeginInteraction(remoteAvatar.data(), InteractionKind::Grab).changed);
        Features::bDeadInteraction.store(false);
        assert(!BeginInteraction(grabController.data(), InteractionKind::Grab).changed);
        Features::bDeadInteraction.store(true);
        isHost = false;
        assert(!BeginInteraction(grabController.data(), InteractionKind::Grab).changed);
        isHost = true;
        isPrivate = false;
        assert(!BeginInteraction(grabController.data(), InteractionKind::Grab).changed);
        isPrivate = true;
        Put(localAvatar, 0x28, uint8_t{0});
        assert(!BeginInteraction(grabController.data(), InteractionKind::Grab).changed);
        Put(localAvatar, 0x28, uint8_t{1});
        Put(localController, 0x38, uint8_t{0});
        assert(!BeginInteraction(grabController.data(), InteractionKind::Grab).changed);
        Put(localController, 0x38, uint8_t{1});
        localPCClass = nullptr;
        assert(!Sample().deadInteractionAvailable &&
            !BeginInteraction(grabController.data(), InteractionKind::Grab).changed);
        localPCClass = &pcClassMarker;
        cleanupRequested.store(true);
        assert(!BeginInteraction(grabController.data(), InteractionKind::Grab).changed);
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

    void TestDeadFlashlight() {
        ConfigureWorld();
        Put(localAvatar, 0x28, uint8_t{1});
        Put(localController, 0x38, uint8_t{1});
        Put(grabController, 0xE1, uint8_t{1});
        Features::bDeadInteraction.store(true);
        auto& torch = Hooks::worldInputHooks[13];
        assert(!BeginInteraction(remoteFlashlight.data(), InteractionKind::Flashlight).changed);
        Put(flashlightController, 0x20, static_cast<void*>(remoteAvatar.data()));
        assert(!BeginInteraction(flashlightController.data(), InteractionKind::Flashlight).changed);
        Put(flashlightController, 0x20, static_cast<void*>(grabController.data()));
        Put(flashlightController, 0x28, static_cast<void*>(remoteAvatar.data()));
        assert(!BeginInteraction(flashlightController.data(), InteractionKind::Flashlight).changed);
        Put(flashlightController, 0x28, static_cast<void*>(localController.data()));
        destroyedObject = flashlightController.data();
        assert(!BeginInteraction(flashlightController.data(), InteractionKind::Flashlight).changed);
        destroyedObject = nullptr;

        torch.original = [](void*, void*, void*) { assert(false); };
        blockedInputOwner = grabController.data();
        torch.hook(flashlightController.data(), nullptr, nullptr);
        assert(interactionDepth == 0 && DeadController(localAvatar.data(), localController.data()));
        blockedInputOwner = nullptr;

        torch.original = [](void* instance, void*, void*) {
            AssertInteractionScope(instance, nullptr);
            assert(interactionDepth == 1);
        };
        Hooks::worldInputHooks[0].original = [](void* instance, void*, void*) {
            AssertInteractionScope(instance, nullptr);
            Hooks::worldInputHooks[13].hook(flashlightController.data(), nullptr, nullptr);
            AssertInteractionScope(instance, nullptr);
            assert(interactionDepth == 1);
        };
        Hooks::worldInputHooks[0].hook(grabController.data(), nullptr, nullptr);
        assert(interactionDepth == 0 && DeadController(localAvatar.data(), localController.data()));

        torch.original = [](void* instance, void*, void*) {
            AssertInteractionScope(instance, nullptr);
            RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
        };
        assert(!InvokeFaultingInput(13, flashlightController.data()));
        assert(interactionDepth == 0 && DeadController(localAvatar.data(), localController.data()));
        uint8_t mode = 1;
        assert(ReadField(grabController.data(), 0xE0, mode) && mode == 0);

        Features::bDeadInteraction.store(false);
        torch.original = [](void*, void*, void*) {
            assert(DeadController(localAvatar.data(), localController.data()) && interactionDepth == 0);
        };
        torch.hook(flashlightController.data(), nullptr, nullptr);
    }

    void TestInteractionUIOwnership() {
        ConfigureWorld();
        Put(localAvatar, 0x28, uint8_t{1});
        Put(localController, 0x38, uint8_t{1});
        Features::bDeadInteraction.store(true);
        Put(journalObject, 0xB8, uint8_t{1});
        assert(!BeginInteraction(grabController.data(), InteractionKind::Grab).changed);
        assert(!BeginInteraction(doorController.data(), InteractionKind::Door).changed);
        assert(!BeginInteraction(flashlightController.data(), InteractionKind::Flashlight).changed);
        assert(!Sample().deadInteractionActive);
        Hooks::oPropGrabUpdate = [](void*, void*) {
            assert(DeadController(localAvatar.data(), localController.data()));
            uint8_t mode = 1, open = 0;
            assert(ReadField(grabController.data(), 0xE0, mode) && mode == 0);
            assert(ReadField(journalObject.data(), 0xB8, open) && open == 1);
        };
        Hooks::hkPropGrabUpdate(grabController.data(), nullptr);
        Put(journalObject, 0xB8, uint8_t{0});
        for (size_t offset : {0x20, 0x21}) {
            Put(menuController, offset, uint8_t{1});
            assert(!BeginInteraction(doorController.data(), InteractionKind::Door).changed);
            Put(menuController, offset, uint8_t{0});
        }
        GhostControl::inputCaptured.store(true);
        assert(!BeginInteraction(grabController.data(), InteractionKind::Grab).changed);
        GhostControl::inputCaptured.store(false);
        Features::bOverlayInput.store(true);
        assert(!BeginInteraction(grabController.data(), InteractionKind::Grab).changed);
        Hooks::worldInputHooks[12].original = [](void*, void*, void*) {
            ++nativeInputs;
            assert(DeadController(localAvatar.data(), localController.data()) && interactionDepth == 0);
        };
        inputPhase.call = [](void*, void*) -> int32_t { return 3; };
        Hooks::worldInputHooks[12].hook(doorController.data(), nullptr, nullptr);
        assert(nativeInputs == 0);
        inputPhase.call = [](void*, void*) -> int32_t { return 4; };
        Hooks::worldInputHooks[12].hook(doorController.data(), nullptr, nullptr);
        assert(nativeInputs == 1);
        Features::bOverlayInput.store(false);

        // The frame loop may refresh targeting without pretending the controller is alive.
        Hooks::worldInputHooks[12].original = [](void* instance, void*, void*) {
            uint8_t mode = 0, dead = 1;
            assert(instance == doorController.data());
            assert(ReadField(grabController.data(), 0xE0, mode) && mode == 1);
            assert(ReadField(localController.data(), 0x38, dead) && dead == 0);
            ++nativeInputs;
        };
        Hooks::oPropGrabUpdate = [](void*, void*) {
            assert(DeadController(localAvatar.data(), localController.data()));
            Hooks::worldInputHooks[12].hook(doorController.data(), nullptr, nullptr);
            assert(DeadController(localAvatar.data(), localController.data()));
            assert(interactionDepth == 1);
        };
        Hooks::hkPropGrabUpdate(grabController.data(), nullptr);
        assert(nativeInputs == 2 && interactionDepth == 0);
        assert(DeadController(localAvatar.data(), localController.data()));
    }

    bool InvokeFaultingMovement() {
        __try {
            Hooks::UpdatePlayerInput([](void*, void*) {
                RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
            }, movementController.data(), nullptr);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    void TestOverlayInput() {
        ConfigureWorld();
        overlayReady = true;
        cursorMode = 1;
        cursorShown = false;
        cursorLock.call = [](void*) -> int32_t { return cursorMode; };
        cursorVisible.call = [](void*) { return cursorShown; };
        Hooks::oCursorLock = [](int32_t mode, void*) {
            if (faultCursorRestore && mode != 0)
                RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
            cursorMode = mode;
        };
        Hooks::oCursorVisible = [](bool visible, void*) { cursorShown = visible; };
        Features::bOverlayInput.store(true);
        assert(ServiceOverlay() && cursorMode == 0 && cursorShown);
        Put(localAvatar, 0x28, uint8_t{1});
        Put(localController, 0x38, uint8_t{1});
        Put(movementController, 0x20, uint8_t{1});
        Put(movementController, 0x21, uint8_t{0});
        Hooks::UpdatePlayerInput([](void*, void*) {
            uint8_t movement = 1, look = 1;
            assert(ReadField(movementController.data(), 0x20, movement) && movement == 0);
            assert(ReadField(movementController.data(), 0x21, look) && look == 0);
            assert(DeadController(localAvatar.data(), localController.data()));
        }, movementController.data(), nullptr);
        uint8_t movement = 0, look = 1;
        assert(ReadField(movementController.data(), 0x20, movement) && movement == 1);
        assert(ReadField(movementController.data(), 0x21, look) && look == 0);
        Hooks::UpdatePlayerInput([](void*, void*) {
            assert(movementDepth == 1);
            cleanupRequested.store(true);
            ServiceWorldOptions();
            assert(!cleanupComplete.load());
            cleanupRequested.store(false);
        }, movementController.data(), nullptr);
        assert(movementDepth == 0);
        assert(!InvokeFaultingMovement());
        assert(ReadField(movementController.data(), 0x20, movement) && movement == 1);
        Hooks::UpdatePlayerInput([](void* instance, void*) {
            assert(instance == remoteAvatar.data());
            uint8_t movement = 0;
            assert(ReadField(movementController.data(), 0x20, movement) && movement == 1);
        }, remoteAvatar.data(), nullptr);

        Hooks::hkCursorLock(2, nullptr);
        Hooks::hkCursorVisible(false, nullptr);
        assert(cursorMode == 0 && cursorShown);
        Features::bOverlayInput.store(false);
        assert(ServiceOverlay() && cursorMode == 2 && !cursorShown && !cursorState.active);
        Features::bOverlayInput.store(true);
        assert(ServiceOverlay());
        Hooks::hkCursorLock(1, nullptr);
        faultCursorRestore = true;
        cleanupRequested.store(true);
        Hooks::UpdateRadar();
        assert(!cleanupComplete.load() && cursorState.active);
        faultCursorRestore = false;
        Hooks::UpdateRadar();
        assert(cleanupComplete.load() && cursorMode == 1 && !cursorShown && !cursorState.active);
        cleanupRequested.store(false);
        Features::bOverlayInput.store(false);
    }

    void TestHangedManControl() {
        for (bool self : {false, true}) {
            for (auto action : {Radar::Action::Kill, Radar::Action::KillHangedMan}) {
                ConfigureBackend();
                auto& avatar = self ? localAvatar : remoteAvatar;
                auto data = Sample();
                const int actor = self ? localActor : remoteActor;
                assert(Radar::Queue(actor, data.generation, action, GetTickCount64()));
                data = Sample();
                assert(kills == 1 && lastDeathPlayer == avatar.data());
                assert(lastDeathType == (action == Radar::Action::KillHangedMan ? 0 : 1));
                assert(pending.size() == 1);
                Sample();
                assert(pending.size() == 1 && kills == 1);
                Put(avatar, 0x28, uint8_t{1});
                data = Sample();
                assert(pending.empty() && data.message.find("confirmed") != std::string::npos);
                assert(!Radar::Queue(actor, data.generation, action, GetTickCount64()));
            }
        }
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

    void TestReviveControl() {
        for (int source : {1, 7}) for (int actor : {1, 2}) {
            ConfigureBackend();
            reviveSources.count = 2;
            reviveSources.entries[0] = 4;
            reviveSources.entries[1] = source;
            auto& avatar = actor == 1 ? localAvatar : remoteAvatar;
            expectedReviveView = actor == 1 ? localOwner.data() : remoteOwner.data();
            Put(avatar, 0x28, uint8_t{1});
            auto data = Sample();
            assert(Radar::Queue(actor, data.generation, Radar::Action::Revive, GetTickCount64()));
            data = Sample();
            assert(revives == 1 && sanityCalls == 0 && sanityRoots == 0);
            assert(data.players[actor - 1].pending);
            Put(avatar, 0x28, uint8_t{0});
            data = Sample();
            assert(!data.players[actor - 1].pending && data.message.find("confirmed") != std::string::npos);
        }

        for (int source : {0, 2, 3, 4, 5, 6}) {
            ConfigureBackend();
            reviveSources.entries[0] = source;
            Put(localAvatar, 0x28, uint8_t{1});
            auto data = Sample();
            assert(data.actionsAllowed && data.sanityAvailable && !data.reviveAvailable);
            assert(!Radar::Queue(1, data.generation, Radar::Action::Revive, GetTickCount64()));
            assert(Radar::Read().message.find("tarot cards") != std::string::npos);
            assert(Radar::Queue(2, data.generation, Radar::Action::Kill, GetTickCount64()));
            Sample();
            assert(!revives && kills == 1);
        }

        for (int invalid = 0; invalid < 7; ++invalid) {
            ConfigureBackend();
            Put(localAvatar, 0x28, uint8_t{1});
            if (invalid == 0) reviveReady = false;
            if (invalid == 1) levelValues.call = [](void*) -> void* { return nullptr; };
            if (invalid == 2) Put(levelValuesObject, 0x38, static_cast<void*>(nullptr));
            if (invalid == 3) destroyedObject = difficultyObject.data();
            if (invalid == 4) Put(difficultyObject, 0xB0, static_cast<void*>(nullptr));
            if (invalid == 5) reviveSources.count = 129;
            if (invalid == 6) levelValues.call = [](void*) -> void* {
                RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
                return nullptr;
            };
            auto data = Sample();
            assert(data.actionsAllowed && data.sanityAvailable &&
                !data.reviveAvailable && !data.reviveRestriction.empty());
            assert(!Radar::Queue(1, data.generation, Radar::Action::Revive, GetTickCount64()));
            assert(!revives && Radar::requests.empty());
        }

        for (int actor : {1, 2}) {
            ConfigureBackend();
            auto& avatar = actor == 1 ? localAvatar : remoteAvatar;
            Put(avatar, 0x28, uint8_t{1});
            auto data = Sample();
            assert(Radar::Queue(actor, data.generation, Radar::Action::Revive, GetTickCount64()));
            reviveSources.count = 0;
            data = Sample();
            assert(data.actionsAllowed && !data.reviveAvailable && !revives && pending.empty());
            assert(data.message.find("Revive rejected") != std::string::npos);
            Sample();
            assert(!revives);
        }

        for (int missing = 0; missing < 9; ++missing) {
            ConfigureBackend();
            Put(remoteAvatar, 0x28, uint8_t{1});
            auto data = Sample();
            assert(Radar::Queue(2, data.generation, Radar::Action::Revive, GetTickCount64()));
            if (missing == 0) Put(remoteAvatar, 0xD8, static_cast<void*>(nullptr));
            if (missing == 1) destroyedObject = remoteCorpse.data();
            if (missing == 2) Put(remoteAnimations, 0x28, static_cast<void*>(localAvatar.data()));
            if (missing == 3) Put(remoteCorpse, 0x40, static_cast<void*>(nullptr));
            if (missing == 4) Put(remoteAnimations, 0x110, static_cast<void*>(nullptr));
            if (missing == 5) Put(remoteAvatar, 0x2B, uint8_t{1});
            if (missing == 6) destroyedObject = remoteOwner.data();
            if (missing == 7) Put(remoteAvatar, 0x100, static_cast<void*>(nullptr));
            if (missing == 8) Put(remoteCorpse, 0xC0, static_cast<void*>(localAvatar.data()));
            data = Sample();
            assert(!revives && !sanityRoots && pending.empty() && data.actionsAllowed);
            assert(data.message.find("Revive rejected") != std::string::npos);
        }

        for (bool nativeFault : {false, true}) {
            ConfigureBackend();
            Put(remoteAvatar, 0x28, uint8_t{1});
            auto data = Sample();
            assert(Radar::Queue(2, data.generation, Radar::Action::Revive, GetTickCount64()));
            faultAction = nativeFault;
            rejectRevive = !nativeFault;
            data = Sample();
            assert(revives == 1 && !sanityRoots && pending.empty() && !data.actionsAllowed);
            assert(data.message.find("failed") != std::string::npos);
            Sample();
            assert(revives == 1);
        }

        ConfigureBackend();
        Put(remoteAvatar, 0x28, uint8_t{1});
        auto data = Sample();
        assert(Radar::Queue(2, data.generation, Radar::Action::Revive, GetTickCount64()));
        rpcArrayNew = [](void*, uintptr_t) -> void* { return nullptr; };
        data = Sample();
        assert(!revives && !sanityRoots && pending.empty() && !data.actionsAllowed);
        assert(data.message.find("failed") != std::string::npos);
    }
}

int main() {
    TestMathAndQueue();
    TestBackend();
    TestSanityControl();
    TestReviveControl();
    TestHangedManControl();
    TestCursedObjects();
    TestLobbyPickup();
    TestVisibility();
    TestDeadInteraction();
    TestDeadFlashlight();
    TestInteractionUIOwnership();
    TestOverlayInput();
}
