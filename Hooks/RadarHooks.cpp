#include "Hooks.h"
#include "../Features/Radar.h"
#include "../Features/Features.h"
#include "../Features/GhostControl.h"
#include "../Features/Pickup.h"
#include "../Features/Stamina.h"
#include "../Features/WorldActions.h"
#include "../Utils/Logger.h"
#include <cstring>

namespace {
    template<typename T>
    struct Method {
        T call = nullptr;
        void* info = nullptr;

        bool Bind(void* image, const char* space, const char* type, const char* name, int arguments) {
            call = nullptr;
            void* klass = image ? SDK::class_from_name(image, space, type) : nullptr;
            info = klass ? SDK::get_method(klass, name, arguments) : nullptr;
            if (info) SDK::ReadSafe(info, call);
            if (!call) Logger::Log("[ERROR] Radar: %s.%s unavailable.", type, name);
            return call != nullptr;
        }
    };

    using StaticObject = void* (*)(void*);
    using StaticBool = bool (*)(void*);
    using ObjectGetter = void* (*)(void*, void*);
    using BoolGetter = bool (*)(void*, void*);
    using IntGetter = int32_t (*)(void*, void*);
    using PlayerGetter = void* (*)(int32_t, void*);
    using DeathAction = void (*)(void*, int32_t, void*);
    using ReviveAction = void (*)(void*, bool, void*);
    using RotationGetter = void (*)(void*, Radar::Quaternion*, void*);

    Method<StaticObject> playerList, localPlayer, currentRoom;
    Method<StaticBool> inRoom, offlineMode, masterClient;
    Method<BoolGetter> roomVisible, objectAlive;
    Method<IntGetter> actorNumber;
    Method<ObjectGetter> nickName, findBone, findCursed, headTransform;
    Method<PlayerGetter> gamePlayer;
    Method<DeathAction> beginDeath;
    Method<ReviveAction> revivePlayer;
    void* sanityRpc = nullptr;
    void* sanityObjectClass = nullptr;
    void* sanityFloatClass = nullptr;
    void* (*sanityStringNew)(const char*) = nullptr;
    void* (*sanityArrayNew)(void*, uintptr_t) = nullptr;
    void* (*sanityBox)(void*, void*) = nullptr;
    void* (*sanityInvoke)(void*, void*, void**, void**) = nullptr;
    void (*sanityBarrier)(void*, void**, void*) = nullptr;
    uint32_t (*sanityPin)(void*, bool) = nullptr;
    void (*sanityUnpin)(uint32_t) = nullptr;
    bool sanityReady = false;
    Method<RotationGetter> rotation;
    Method<BoolGetter> rendererEnabled;
    Method<Hooks::Visibility_t> setRendererEnabled;
    bool visibilityReady = false;
    bool interactionReady = false;
    bool worldHealthy = true;
    unsigned ghostTransitions = 0;
    unsigned interactionDepth = 0;
    void* localPCClass = nullptr;
    std::atomic<bool> cleanupRequested{false};
    std::atomic<bool> cleanupComplete{false};
    std::atomic<bool> worldHooksInstalled{false};
    struct RendererState {
        void* renderer;
        bool enabled;
    };
    void* visibleModel = nullptr;
    std::vector<RendererState> originalRenderers;
    const void* boneType = nullptr;
    const void* cursedType = nullptr;
    bool cursedUnavailable = false;
    bool ready = false;
    bool actionsReady = false;
    bool actionsHealthy = true;
    bool boneLayoutValid = false;
    bool rosterUnavailable = false;
    uint64_t generation = 1;
    uint64_t lastSample = 0;
    void* lastRoom = nullptr;
    void* lastGhost = nullptr;

    struct Pending {
        Radar::Request request;
        uint64_t deadline;
    };
    std::vector<Pending> pending;

    template<typename T>
    bool ReadField(void* object, size_t offset, T& value) {
        return object && SDK::ReadSafe(static_cast<char*>(object) + offset, value);
    }

    bool FieldMatches(void* klass, size_t offset, void* type) {
        if (!klass || !type) return false;
        void* iterator = nullptr;
        while (void* field = SDK::class_get_fields(klass, &iterator)) {
            if (SDK::field_get_offset(field) == offset) {
                return SDK::class_from_type(SDK::field_get_type(field)) == type;
            }
        }
        return false;
    }

    bool InitSanity(void* image, void* photon, void* core) {
        HMODULE module = GetModuleHandleW(L"GameAssembly.dll");
        if (!module || !photon || !core || !SDK::method_get_param) return false;
        sanityStringNew = reinterpret_cast<decltype(sanityStringNew)>(GetProcAddress(module, "il2cpp_string_new"));
        sanityArrayNew = reinterpret_cast<decltype(sanityArrayNew)>(GetProcAddress(module, "il2cpp_array_new"));
        sanityBox = reinterpret_cast<decltype(sanityBox)>(GetProcAddress(module, "il2cpp_value_box"));
        sanityInvoke = reinterpret_cast<decltype(sanityInvoke)>(GetProcAddress(module, "il2cpp_runtime_invoke"));
        sanityBarrier = reinterpret_cast<decltype(sanityBarrier)>(GetProcAddress(module, "il2cpp_gc_wbarrier_set_field"));
        sanityPin = reinterpret_cast<decltype(sanityPin)>(GetProcAddress(module, "il2cpp_gchandle_new"));
        sanityUnpin = reinterpret_cast<decltype(sanityUnpin)>(GetProcAddress(module, "il2cpp_gchandle_free"));
        const auto methods = reinterpret_cast<void* (*)(void*, void**)>(GetProcAddress(module, "il2cpp_class_get_methods"));
        const auto name = reinterpret_cast<const char* (*)(void*)>(GetProcAddress(module, "il2cpp_method_get_name"));
        const auto count = reinterpret_cast<uint32_t (*)(void*)>(GetProcAddress(module, "il2cpp_method_get_param_count"));
        if (!sanityStringNew || !sanityArrayNew || !sanityBox || !sanityInvoke || !sanityBarrier ||
            !sanityPin || !sanityUnpin || !methods || !name || !count) return false;
        void* viewClass = SDK::class_from_name(photon, "Photon.Pun", "PhotonView");
        void* targetClass = SDK::class_from_name(photon, "Photon.Pun", "RpcTarget");
        void* sanityClass = SDK::class_from_name(image, "", "PlayerSanity");
        sanityObjectClass = SDK::class_from_name(core, "System", "Object");
        sanityFloatClass = SDK::class_from_name(core, "System", "Single");
        if (!viewClass || !targetClass || !sanityObjectClass || !sanityFloatClass ||
            !FieldMatches(sanityClass, 0x20, viewClass)) return false;
        void* iterator = nullptr;
        while (void* method = methods(viewClass, &iterator)) {
            const char* methodName = name(method);
            if (methodName && std::strcmp(methodName, "RPC") == 0 && count(method) == 3 &&
                SDK::class_from_type(SDK::method_get_param(method, 1)) == targetClass) {
                sanityRpc = method;
                break;
            }
        }
        void* receive = SDK::get_method(sanityClass, "NetworkedUpdatePlayerSanity", 2);
        uintptr_t rpcCode = 0, receiveCode = 0;
        const uintptr_t base = reinterpret_cast<uintptr_t>(module);
        return SDK::ReadSafe(sanityRpc, rpcCode) && rpcCode == base + 0x43638A0 &&
            SDK::ReadSafe(receive, receiveCode) && receiveCode == base + 0xDA5570;
    }

    bool ValidPosition(const Radar::Vector3& value) {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) &&
            std::fabs(value.x) < 100000 && std::fabs(value.y) < 100000 && std::fabs(value.z) < 100000;
    }

    bool Alive(void* object) {
        return SDK::IsReadable(object, 0x20) && objectAlive.call(object, objectAlive.info);
    }

    bool ReadHeading(void* transform, float& heading) {
        if (!rotation.call || !Alive(transform)) return false;
        Radar::Quaternion value;
        rotation.call(transform, &value, rotation.info);
        return Radar::Heading(value, heading);
    }

    Radar::Pose ReadPose(void* component) {
        Radar::Pose pose;
        if (!Alive(component)) return pose;
        void* transform = Hooks::oGetTransform(component, Hooks::getTransformMethodInfo);
        if (!Alive(transform)) return pose;
        Hooks::Vector3 value{};
        Hooks::oGetPositionInjected(transform, &value, Hooks::getPositionInjectedMethodInfo);
        pose.position = {value.x, value.y, value.z};
        pose.positionValid = ValidPosition(pose.position);
        pose.headingValid = ReadHeading(transform, pose.heading);
        return pose;
    }

    const char* CursedName(void* object) {
        void* klass = SDK::object_get_class(object);
        const char* name = klass ? SDK::class_get_name(klass) : nullptr;
        if (!name) return "Cursed object";
        if (std::strcmp(name, "OuijaBoard") == 0) return "Ouija board";
        if (std::strcmp(name, "MusicBox") == 0) return "Music box";
        if (std::strcmp(name, "TarotCards") == 0) return "Tarot cards";
        if (std::strcmp(name, "SummoningCircle") == 0) return "Summoning circle";
        if (std::strcmp(name, "HauntedMirror") == 0) return "Haunted mirror";
        if (std::strcmp(name, "VoodooDoll") == 0) return "Voodoo doll";
        if (std::strcmp(name, "MonkeyPaw") == 0) return "Monkey paw";
        return name;
    }

    bool ReadCursedObjects(Radar::Snapshot& next) {
        if (!findCursed.call || !cursedType || !SDK::object_get_class || !SDK::class_get_name) return true;
        void* type = SDK::type_get_object(cursedType);
        void* array = type ? findCursed.call(type, findCursed.info) : nullptr;
        uintptr_t count = 0;
        if (!ReadField(array, 0x18, count) || count > 128) return false;
        for (uintptr_t index = 0; index < count; ++index) {
            void* object = nullptr;
            if (!ReadField(array, 0x20 + index * sizeof(void*), object)) return false;
            if (!Alive(object)) continue;
            Radar::Pose pose = ReadPose(object);
            if (pose.positionValid) next.cursedObjects.push_back({CursedName(object), pose});
        }
        return true;
    }

    bool ReadLife(void* player, Radar::Player& display) {
        uint8_t dead = 0, dying = 0;
        display.lifeValid = ReadField(player, 0x28, dead) && dead <= 1 &&
            ReadField(player, 0x29, dying) && dying <= 1;
        display.dead = dead != 0;
        display.dying = dying != 0 && !display.dead;
        return display.lifeValid;
    }

    bool Authority(void* room) {
        return actionsReady && actionsHealthy && Radar::MayAct(
            inRoom.call(inRoom.info), offlineMode.call(offlineMode.info),
            masterClient.call(masterClient.info),
            room && !roomVisible.call(room, roomVisible.info), Hooks::gCurrentGhostAI != nullptr);
    }

    void Message(const std::string& text) {
        Logger::Log("[RADAR] %s", text.c_str());
        std::lock_guard<std::mutex> lock(Radar::mutex);
        Radar::snapshot.message = text;
    }

    bool RestoreVisibility() {
        for (auto& state : originalRenderers) {
            if (Alive(state.renderer))
                setRendererEnabled.call(state.renderer, state.enabled, setRendererEnabled.info);
        }
        originalRenderers.clear();
        visibleModel = nullptr;
        return true;
    }

    bool SafeRestoreVisibility() {
        __try {
            return RestoreVisibility();
        }
        __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION
            ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
            worldHealthy = false;
            Logger::Log("[ERROR] World options: renderer restoration failed; unload blocked until restoration succeeds.");
            return false;
        }
    }

    bool WorldAuthority() {
        return ready && worldHooksInstalled.load() && worldHealthy && !cleanupRequested.load() &&
            Alive(Hooks::gCurrentGhostAI) && Authority(currentRoom.call(currentRoom.info));
    }

    bool ForceVisibility() {
        __try {
            int32_t state = -1;
            return Features::bGhostVisible.load() && WorldAuthority() && !ghostTransitions &&
                ReadField(Hooks::gCurrentGhostAI, 0x30, state) && state != 2;
        }
        __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION
            ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
            worldHealthy = false;
            Logger::Log("[ERROR] World options: visibility lookup faulted; options disabled.");
            return false;
        }
    }

    void* LocalPC(void*& player, void*& grab, void*& door, bool requireAuthority = true) {
        player = grab = door = nullptr;
        if (requireAuthority && !WorldAuthority()) return nullptr;
        void* owner = localPlayer.call(localPlayer.info);
        if (!SDK::IsReadable(owner, 0x20)) return nullptr;
        player = gamePlayer.call(actorNumber.call(owner, actorNumber.info), gamePlayer.info);
        void* local = nullptr;
        void* back = nullptr;
        if (!Alive(player) || !ReadField(player, 0x110, local) || !Alive(local) ||
            SDK::object_get_class(local) != localPCClass ||
            !ReadField(local, 0x20, back) || back != player ||
            !ReadField(local, 0x148, grab) || !Alive(grab) ||
            !ReadField(grab, 0x20, back) || back != local ||
            !ReadField(local, 0x158, door) || !Alive(door) ||
            !ReadField(door, 0x58, back) || back != local) return nullptr;
        return local;
    }

    bool DeadController(void* player, void* local) {
        uint8_t dead = 0, complete = 0;
        return ReadField(player, 0x28, dead) && dead == 1 &&
            ReadField(local, 0x38, complete) && complete == 1;
    }

    struct InteractionScope {
        void* player = nullptr;
        void* local = nullptr;
        void* grab = nullptr;
        uint8_t grabMode = 0;
        bool changed = false;
    };
    InteractionScope pendingInteractionRestore;

    bool ApplyInteraction(const InteractionScope& scope) {
        __try {
            return SDK::WriteSafe(static_cast<char*>(scope.grab) + 0xE0, uint8_t{1}) &&
                SDK::WriteSafe(static_cast<char*>(scope.local) + 0x38, uint8_t{0});
        }
        __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION
            ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
            return false;
        }
    }

    bool RestoreInteraction(const InteractionScope& scope) {
        __try {
            uint8_t dead = 0;
            if (!Alive(scope.player)) return true;
            if (!ReadField(scope.player, 0x28, dead) || dead > 1) return false;
            // A real revival inside the callback owns the new living-controller state.
            if (dead == 0) return true;
            const bool restoredLocal = !Alive(scope.local) ||
                SDK::WriteSafe(static_cast<char*>(scope.local) + 0x38, uint8_t{1});
            const bool restoredGrab = !Alive(scope.grab) ||
                SDK::WriteSafe(static_cast<char*>(scope.grab) + 0xE0, scope.grabMode);
            return restoredLocal && restoredGrab;
        }
        __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION
            ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
            return false;
        }
    }

    void EndInteraction(const InteractionScope& scope) {
        if (!scope.changed) return;
        if (!RestoreInteraction(scope)) {
            pendingInteractionRestore = scope;
            worldHealthy = false;
            Message("Dead interaction restoration failed; options disabled and unload blocked until restored.");
        }
        --interactionDepth;
    }

    InteractionScope BeginInteraction(void* instance, bool isDoor) {
        InteractionScope scope;
        if (!interactionReady || !Features::bDeadInteraction.load()) return scope;
        void* player = nullptr;
        void* grab = nullptr;
        void* door = nullptr;
        void* local = LocalPC(player, grab, door);
        if (!local || instance != (isDoor ? door : grab) || !DeadController(player, local) ||
            !ReadField(grab, 0xE0, scope.grabMode) || scope.grabMode > 1) return scope;
        scope.player = player;
        scope.local = local;
        scope.grab = grab;
        // Only the local input callbacks see living interaction mode. Player.dead is untouched.
        scope.changed = true;
        ++interactionDepth;
        if (!ApplyInteraction(scope)) {
            EndInteraction(scope);
            scope.changed = false;
            worldHealthy = false;
            Message("Dead interaction could not enter the local controller scope; options disabled.");
        }
        return scope;
    }

    InteractionScope SafeBeginInteraction(void* instance, bool door) {
        __try {
            return BeginInteraction(instance, door);
        }
        __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION
            ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
            worldHealthy = false;
            Logger::Log("[ERROR] World options: local interaction lookup faulted; options disabled.");
            return {};
        }
    }

    void InvokeInput(Hooks::InputAction_t original, void* instance, void* context, void* info, bool door) {
        const InteractionScope scope = SafeBeginInteraction(instance, door);
        __try {
            original(instance, context, info);
        }
        __finally {
            EndInteraction(scope);
        }
    }

    bool UpdatePickup(void* grab) {
        void* local = nullptr;
        void* camera = nullptr;
        void* avatar = nullptr;
        bool allowed = false;
        __try {
            void* back = nullptr;
            void* controller = nullptr;
            if (interactionReady && worldHealthy && worldHooksInstalled.load() && !cleanupRequested.load() &&
                Alive(grab) && ReadField(grab, 0x20, local) && Alive(local) &&
                SDK::object_get_class(local) == localPCClass &&
                ReadField(local, 0x148, back) && back == grab &&
                ReadField(local, 0x30, camera) && Alive(camera) &&
                ReadField(local, 0x128, controller) && ReadField(local, 0x20, avatar)) {
                Stamina::Observe(local);
                allowed = !GhostControl::ControlsPlayer(controller);
            }
        }
        __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION
            ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
            worldHealthy = false;
            Logger::Log("[ERROR] Pickup: local controller lookup faulted; pickup disabled.");
        }
        Pickup::Update(camera, local, allowed, GhostControl::inputCaptured.load(), generation, avatar);
        return allowed;
    }

    template<size_t Index>
    void InputHook(void* instance, void* context, void* info) {
        if (GhostControl::BlocksInput(instance, Index == 12)) return;
        if constexpr (Index == 1) {
            if (UpdatePickup(instance) && Pickup::HandleInput(context)) return;
        }
        InvokeInput(Hooks::worldInputHooks[Index].original, instance, context, info, Index == 12);
    }

    void InvokeUpdate(Hooks::Update_t original, void* instance, void* info, bool door) {
        const InteractionScope scope = SafeBeginInteraction(instance, door);
        __try {
            original(instance, info);
        }
        __finally {
            EndInteraction(scope);
        }
    }

    void ServiceWorldOptions() {
        if (pendingInteractionRestore.changed) {
            if (!RestoreInteraction(pendingInteractionRestore)) return;
            pendingInteractionRestore = {};
        }
        if (cleanupRequested.load()) {
            WorldActions::Reset();
            const bool controlRestored = GhostControl::Restore();
            const bool pickupRestored = Pickup::Restore();
            if (interactionDepth == 0 && controlRestored && pickupRestored && SafeRestoreVisibility())
                cleanupComplete.store(true);
            return;
        }
        const auto host = Hooks::ReadHostContext();
        if (GhostControl::desired.load() && !GhostControl::ControlsPlayer(host.controller) && !Pickup::Restore()) {
            GhostControl::desired.store(false);
            Logger::Log("[ERROR] Ghost control rejected: expanded-pickup restoration is still pending.");
        }
        GhostControl::Update(host);
        const auto control = GhostControl::Read();
        WorldActions::Update(host, control.active || control.restoring);
        if (!visibilityReady || !Features::bGhostVisible.load() || !WorldAuthority() || ghostTransitions) {
            SafeRestoreVisibility();
            return;
        }
        int32_t state = -1;
        void* model = nullptr;
        void* owner = nullptr;
        if (!ReadField(Hooks::gCurrentGhostAI, 0x30, state) || state == 2 ||
            !ReadField(Hooks::gCurrentGhostAI, 0x60, model) || !Alive(model) ||
            !ReadField(model, 0x28, owner) || owner != Hooks::gCurrentGhostAI) {
            SafeRestoreVisibility();
            return;
        }
        if (model != visibleModel && !SafeRestoreVisibility()) return;
        void* array = nullptr;
        uintptr_t count = 0;
        if (!ReadField(model, 0x30, array) || !ReadField(array, 0x18, count) || count > 128) {
            Features::bGhostVisible.store(false);
            SafeRestoreVisibility();
            Message("Ghost renderer list unsupported; visibility option disabled.");
            return;
        }
        for (uintptr_t index = 0; index < count; ++index) {
            void* renderer = nullptr;
            if (!ReadField(array, 0x20 + index * sizeof(void*), renderer)) {
                Features::bGhostVisible.store(false);
                SafeRestoreVisibility();
                Message("Ghost renderer list unreadable; visibility option disabled.");
                return;
            }
            if (!Alive(renderer)) continue;
            const auto found = std::find_if(originalRenderers.begin(), originalRenderers.end(),
                [renderer](const RendererState& item) { return item.renderer == renderer; });
            if (found == originalRenderers.end())
                originalRenderers.push_back({renderer, rendererEnabled.call(renderer, rendererEnabled.info)});
            visibleModel = model;
            setRendererEnabled.call(renderer, true, setRendererEnabled.info);
        }
    }

    void WorldStatus(Radar::Snapshot& next) {
        next.ghostVisibilityAvailable = visibilityReady && worldHooksInstalled.load() &&
            worldHealthy && !cleanupRequested.load();
        next.ghostVisibilityActive = visibleModel && !originalRenderers.empty();
        void* player = nullptr;
        void* grab = nullptr;
        void* door = nullptr;
        void* local = interactionReady ? LocalPC(player, grab, door) : nullptr;
        next.deadInteractionAvailable = interactionReady && worldHealthy && !cleanupRequested.load() && local;
        next.deadInteractionActive = next.deadInteractionAvailable && Features::bDeadInteraction.load() &&
            DeadController(player, local);
        next.worldOptionsMessage = !worldHealthy ? "World options disabled after a native failure; see logs." :
            "PC local input only; normal ownership/item restrictions remain. Visibility yields during hunts.";
    }

    void RejectRoster(Radar::Snapshot& next) {
        if (!rosterUnavailable) Message("Player list unavailable or unsupported; stale radar data cleared.");
        rosterUnavailable = true;
        next.players.clear();
        next.center = {};
        next.actionsAllowed = false;
        next.actionRestriction = "Player roster unavailable; actions disabled.";
    }

    bool BroadcastSanity(void* view, int sanity) {
        uint32_t roots[3]{};
        __try {
            void* name = sanityStringNew("NetworkedUpdatePlayerSanity");
            if (!name || !(roots[0] = sanityPin(name, false))) return false;
            void* parameters = sanityArrayNew(sanityObjectClass, 1);
            if (!parameters || !(roots[1] = sanityPin(parameters, false)) ||
                !SDK::IsReadable(parameters, 0x28)) return false;
            float insanity = 100.0f - static_cast<float>(sanity);
            void* value = sanityBox(sanityFloatClass, &insanity);
            if (!value || !(roots[2] = sanityPin(value, false))) return false;
            sanityBarrier(parameters, reinterpret_cast<void**>(static_cast<char*>(parameters) + 0x20), value);
            int target = 0; // RpcTarget.All: the real sender is validated on every receiver, including this host.
            void* arguments[]{name, &target, parameters};
            void* exception = nullptr;
            sanityInvoke(sanityRpc, view, arguments, &exception);
            if (exception) {
                Logger::Log("[ERROR] Sanity RPC raised a managed exception (%p).", exception);
                return false;
            }
            return true;
        }
        __finally {
            for (uint32_t root : roots) if (root) sanityUnpin(root);
        }
    }

    enum class ActionResult { Applied, Unavailable, Failed };

    ActionResult InvokeAction(void* player, const Radar::Request& request) {
        __try {
            if (request.action == Radar::Action::Kill) {
                // The game's SnappedNeck enum value; the wrapper routes the sequence to the owner.
                beginDeath.call(player, 1, beginDeath.info);
            } else if (request.action == Radar::Action::Revive) {
                revivePlayer.call(player, false, revivePlayer.info);
            } else {
                void* sanity = nullptr;
                void* owner = nullptr;
                void* view = nullptr;
                if (!sanityReady || !ReadField(player, 0x90, sanity) || !Alive(sanity) ||
                    !ReadField(sanity, 0x28, owner) || owner != player ||
                    !ReadField(sanity, 0x20, view) || !Alive(view)) return ActionResult::Unavailable;
                return BroadcastSanity(view, request.sanity) ? ActionResult::Applied : ActionResult::Failed;
            }
            return ActionResult::Applied;
        }
        __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION
            ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
            Logger::Log("[ERROR] Native player action raised an access violation.");
            return ActionResult::Failed;
        }
    }

    void ProcessRequests(Radar::Snapshot& next, uint64_t now) {
        std::vector<Radar::Request> queued;
        {
            std::lock_guard<std::mutex> lock(Radar::mutex);
            queued.swap(Radar::requests);
        }
        for (const auto& request : queued) {
            const auto target = std::find_if(next.players.begin(), next.players.end(),
                [&](const Radar::Player& player) { return player.actorId == request.actorId; });
            if ((request.action != Radar::Action::Kill && request.action != Radar::Action::Revive &&
                request.action != Radar::Action::SetSanity) ||
                !next.actionsAllowed || request.generation != generation || now < request.requestedAt ||
                now - request.requestedAt > 1500 || !Authority(currentRoom.call(currentRoom.info)) ||
                target == next.players.end() || target->instance != request.instance ||
                target->pending || !target->lifeValid || target->dying ||
                (request.action == Radar::Action::Revive) != target->dead ||
                (request.action == Radar::Action::SetSanity &&
                    (!next.sanityAvailable || !target->sanityValid ||
                     (!target->local && !next.remoteSanityAllowed) || request.sanity < 0 || request.sanity > 100))) {
                Message("Action rejected: session, authority, or player state changed.");
                continue;
            }
            void* player = gamePlayer.call(request.actorId, gamePlayer.info);
            Radar::Player live;
            if (reinterpret_cast<uintptr_t>(player) != request.instance ||
                !Alive(player) || !ReadLife(player, live) || live.dying ||
                (request.action == Radar::Action::Revive) != live.dead) {
                Message("Action rejected: target is no longer in the requested state.");
                continue;
            }
            const ActionResult result = InvokeAction(player, request);
            if (result == ActionResult::Unavailable) {
                Message("Action rejected: the native sanity component or its network view is unavailable.");
                continue;
            }
            if (result == ActionResult::Failed) {
                actionsHealthy = false;
                Message("Native player action failed; further player actions disabled. See logs.");
                continue;
            }
            if (request.generation != generation) break;
            pending.push_back({request, now + 10000});
            target->pending = true;
            Message(std::string(request.action == Radar::Action::Kill ? "Kill" :
                request.action == Radar::Action::Revive ? "Revive" : "Sanity change") +
                " requested for " + target->name + "; awaiting game confirmation.");
        }
    }

    void UpdatePending(Radar::Snapshot& next, uint64_t now) {
        for (auto item = pending.begin(); item != pending.end();) {
            auto target = std::find_if(next.players.begin(), next.players.end(),
                [&](const Radar::Player& player) { return player.actorId == item->request.actorId; });
            if (target == next.players.end() || target->instance != item->request.instance) {
                Message("Pending action cancelled: player left or was replaced.");
            } else if (item->request.action == Radar::Action::SetSanity && target->sanityValid &&
                std::fabs(target->sanity - item->request.sanity) <= 0.5f) {
                Message("Requested sanity observed for " + target->name + "; remote replication is not independently verified.");
            } else if (item->request.action != Radar::Action::SetSanity && target->lifeValid &&
                target->dead == (item->request.action == Radar::Action::Kill) && !target->dying) {
                Message("Game confirmed the requested life state for " + target->name + ".");
            } else if (now >= item->deadline) {
                Message("No requested-state confirmation received for " + target->name + "; request not retried.");
            } else {
                target->pending = true;
                ++item;
                continue;
            }
            item = pending.erase(item);
        }
    }
}

Hooks::WorldInputHook Hooks::worldInputHooks[13] = {
    {"PCPropGrab", "OnInteract", nullptr, InputHook<0>},
    {"PCPropGrab", "OnPickup", nullptr, InputHook<1>},
    {"PCPropGrab", "OnPrimaryUse", nullptr, InputHook<2>},
    {"PCPropGrab", "OnPrimaryUseHold", nullptr, InputHook<3>},
    {"PCPropGrab", "OnSecondaryUse", nullptr, InputHook<4>},
    {"PCPropGrab", "OnDrop", nullptr, InputHook<5>},
    {"PCPropGrab", "OnInventorySwapScroll", nullptr, InputHook<6>},
    {"PCPropGrab", "OnSelectInventorySlot1", nullptr, InputHook<7>},
    {"PCPropGrab", "OnSelectInventorySlot2", nullptr, InputHook<8>},
    {"PCPropGrab", "OnSelectInventorySlot3", nullptr, InputHook<9>},
    {"PCPropGrab", "OnSelectInventorySlot4", nullptr, InputHook<10>},
    {"PCPropGrab", "OnInventorySwap", nullptr, InputHook<11>},
    {"DragRigidbodyUse", "OnInteract", nullptr, InputHook<12>}
};

void Hooks::InitRadar(void* domain, void* image, void* unityImage) {
    if (!SDK::class_get_fields || !SDK::field_get_type || !SDK::class_get_type ||
        !SDK::type_get_object || !SDK::class_from_type || !SDK::field_get_offset) {
        Message("Required IL2CPP metadata exports unavailable; radar disabled.");
        return;
    }
    const auto imageFor = [domain](const char* name) {
        void* assembly = SDK::assembly_open(domain, name);
        return assembly ? SDK::assembly_get_image(assembly) : nullptr;
    };
    void* photon = imageFor("PhotonUnityNetworking");
    void* realtime = imageFor("PhotonRealtime");
    void* core = imageFor("mscorlib");
    void* physics = imageFor("UnityEngine.PhysicsModule");
    void* navigation = imageFor("UnityEngine.AIModule");
    void* input = imageFor("Unity.InputSystem");
    bool methods = true;
    methods &= objectAlive.Bind(unityImage, "UnityEngine", "Object", "op_Implicit", 1);
    methods &= playerList.Bind(photon, "Photon.Pun", "PhotonNetwork", "get_PlayerList", 0);
    methods &= localPlayer.Bind(photon, "Photon.Pun", "PhotonNetwork", "get_LocalPlayer", 0);
    methods &= currentRoom.Bind(photon, "Photon.Pun", "PhotonNetwork", "get_CurrentRoom", 0);
    methods &= inRoom.Bind(photon, "Photon.Pun", "PhotonNetwork", "get_InRoom", 0);
    methods &= actorNumber.Bind(realtime, "Photon.Realtime", "Player", "get_ActorNumber", 0);
    methods &= nickName.Bind(realtime, "Photon.Realtime", "Player", "get_NickName", 0);
    methods &= gamePlayer.Bind(image, "", "Player", "GetPlayerFromActorID", 1);
    actionsReady = offlineMode.Bind(photon, "Photon.Pun", "PhotonNetwork", "get_OfflineMode", 0);
    actionsReady &= masterClient.Bind(photon, "Photon.Pun", "PhotonNetwork", "get_IsMasterClient", 0);
    actionsReady &= roomVisible.Bind(realtime, "Photon.Realtime", "Room", "get_IsVisible", 0);
    actionsReady &= beginDeath.Bind(image, "", "Player", "BeginDeathSequence", 1);
    actionsReady &= revivePlayer.Bind(image, "", "Player", "RevivePlayer", 1);
    rotation.Bind(unityImage, "UnityEngine", "Transform", "get_rotation_Injected", 1);
    headTransform.Bind(unityImage, "UnityEngine", "GameObject", "get_transform", 0);
    findBone.Bind(unityImage, "UnityEngine", "Object", "FindObjectOfType", 1);
    findCursed.Bind(unityImage, "UnityEngine", "Object", "FindObjectsOfType", 1);

    void* playerClass = SDK::class_from_name(image, "", "Player");
    void* sanityClass = SDK::class_from_name(image, "", "PlayerSanity");
    void* booleanClass = core ? SDK::class_from_name(core, "System", "Boolean") : nullptr;
    void* floatClass = core ? SDK::class_from_name(core, "System", "Single") : nullptr;
    void* objectClass = SDK::class_from_name(unityImage, "UnityEngine", "GameObject");
    const bool layout = FieldMatches(playerClass, 0x28, booleanClass) &&
        FieldMatches(playerClass, 0x29, booleanClass) &&
        FieldMatches(playerClass, 0x40, objectClass) &&
        FieldMatches(playerClass, 0x90, sanityClass) &&
        FieldMatches(sanityClass, 0x28, playerClass) &&
        FieldMatches(sanityClass, 0x30, floatClass);
    sanityReady = layout && InitSanity(image, photon, core);
    Logger::Log("[RADAR] Native sanity controls %s.", sanityReady ? "ready" : "unavailable");
    void* boneClass = SDK::class_from_name(image, "", "DNAEvidence");
    void* collectableClass = SDK::class_from_name(image, "", "Collectable");
    boneLayoutValid = FieldMatches(boneClass, 0x60, collectableClass) &&
        FieldMatches(collectableClass, 0x9C, booleanClass);
    boneType = boneClass ? SDK::class_get_type(boneClass) : nullptr;
    void* cursedClass = SDK::class_from_name(image, "", "CursedItem");
    cursedType = cursedClass ? SDK::class_get_type(cursedClass) : nullptr;
    if (!cursedType) Logger::Log("[ERROR] Radar: CursedItem type unavailable; cursed-object tracking disabled.");
    ready = methods && layout && oGetTransform && oGetPositionInjected;
    void* ghostClass = SDK::class_from_name(image, "", "GhostAI");
    void* modelClass = SDK::class_from_name(image, "", "GhostModel");
    void* localClass = SDK::class_from_name(image, "", "LocalPlayer");
    localPCClass = SDK::class_from_name(image, "", "LocalPCPlayer");
    void* grabClass = SDK::class_from_name(image, "", "PCPropGrab");
    void* doorClass = SDK::class_from_name(image, "", "DragRigidbodyUse");
    void* renderers = modelClass && SDK::get_field ? SDK::get_field(modelClass, "myRends") : nullptr;
    Method<Visibility_t> visibility;
    visibilityReady = FieldMatches(ghostClass, 0x60, modelClass) &&
        FieldMatches(modelClass, 0x28, ghostClass) && renderers &&
        SDK::field_get_offset(renderers) == 0x30;
    visibilityReady &= visibility.Bind(image, "", "GhostModel", "SetVisibility", 1);
    visibilityReady &= rendererEnabled.Bind(unityImage, "UnityEngine", "Renderer", "get_enabled", 0);
    visibilityReady &= setRendererEnabled.Bind(unityImage, "UnityEngine", "Renderer", "set_enabled", 1);
    oGhostVisibility = visibilityReady ? visibility.call : nullptr;
    Method<Update_t> grabUpdate, doorUpdate;
    interactionReady = SDK::object_get_class &&
        FieldMatches(playerClass, 0x110, localClass) &&
        FieldMatches(localClass, 0x20, playerClass) && FieldMatches(localClass, 0x38, booleanClass) &&
        FieldMatches(localPCClass, 0x148, grabClass) && FieldMatches(localPCClass, 0x158, doorClass) &&
        FieldMatches(grabClass, 0x20, localPCClass) && FieldMatches(grabClass, 0xE0, booleanClass) &&
        FieldMatches(doorClass, 0x58, localClass);
    interactionReady &= grabUpdate.Bind(image, "", "PCPropGrab", "Update", 0);
    interactionReady &= doorUpdate.Bind(image, "", "DragRigidbodyUse", "FixedUpdate", 0);
    for (auto& input : worldInputHooks) {
        Method<InputAction_t> binding;
        interactionReady &= binding.Bind(image, "", input.type, input.name, 1);
        input.original = binding.call;
    }
    oPropGrabUpdate = interactionReady ? grabUpdate.call : nullptr;
    oDoorFixedUpdate = interactionReady ? doorUpdate.call : nullptr;
    if (!interactionReady)
        for (auto& input : worldInputHooks) input.original = nullptr;
    GhostControl::Initialize(image, unityImage, navigation, physics);
    Pickup::Initialize(image, unityImage, physics, input);
    WorldActions::Initialize(domain, image, unityImage);
    Logger::Log("[WORLD] Renderer override %s; scoped PC interactions %s.",
        visibilityReady ? "ready" : "unavailable", interactionReady ? "ready" : "unavailable");
    Logger::Log("[RADAR] Player layout %s; observation %s; lifecycle APIs %s.",
        layout ? "matched" : "unsupported", ready ? "ready" : "disabled",
        actionsReady ? "resolved" : "unavailable");
    if (!ready) Message("Radar unavailable: missing methods or unsupported player layout. See logs.");
    if (!boneLayoutValid) Logger::Log("[ERROR] Radar: bone collection layout unsupported; bone marker disabled.");
}

void Hooks::ResetRadar() {
    WorldActions::Reset();
    GhostControl::Restore();
    Pickup::Restore();
    SafeRestoreVisibility();
    cursedUnavailable = false;
    rosterUnavailable = false;
    ++generation;
    lastSample = 0;
    lastRoom = nullptr;
    lastGhost = nullptr;
    pending.clear();
    std::lock_guard<std::mutex> lock(Radar::mutex);
    Radar::snapshot = {};
    Radar::snapshot.generation = generation;
    Radar::requests.clear();
}

namespace {
void UpdateRadarCore() {
    using namespace Hooks;
    if (!ready || cleanupRequested.load()) {
        ServiceWorldOptions();
        return;
    }
    const uint64_t now = GetTickCount64();
    void* room = currentRoom.call(currentRoom.info);
    if (room != lastRoom || gCurrentGhostAI != lastGhost) {
        ResetRadar();
        lastRoom = room;
        lastGhost = gCurrentGhostAI;
    }
    ServiceWorldOptions();
    if (lastSample && now - lastSample < 200) return;
    lastSample = now;
    Radar::Snapshot next;
    next.generation = generation;
    next.sampledAt = now;
    next.actionsAllowed = Authority(room);
    next.sanityAvailable = sanityReady && actionsHealthy;
    next.actionRestriction = next.actionsAllowed ? "Private/offline host controls enabled." :
        "Actions require an active match and private/offline host authority.";
    if (!actionsReady || !actionsHealthy)
        next.actionRestriction = "Player actions unavailable; see logs.";

    const bool joined = inRoom.call(inRoom.info);
    if (joined) {
        void* local = localPlayer.call(localPlayer.info);
        const bool localValid = SDK::IsReadable(local, 0x20);
        const int localId = localValid ? actorNumber.call(local, actorNumber.info) : 0;
        void* array = playerList.call(playerList.info);
        uintptr_t count = 0;
        if (!localValid || !ReadField(array, 0x18, count) || count == 0 || count > 16) {
            RejectRoster(next);
        } else {
            bool validRoster = true;
            for (uintptr_t index = 0; index < count; ++index) {
                void* owner = nullptr;
                if (!ReadField(array, 0x20 + index * sizeof(void*), owner) || !SDK::IsReadable(owner, 0x20)) {
                    validRoster = false;
                    break;
                }
                Radar::Player entry;
                entry.actorId = actorNumber.call(owner, actorNumber.info);
                entry.local = entry.actorId == localId;
                entry.name = SDK::IL2CPP_To_String(nickName.call(owner, nickName.info));
                if (entry.name.empty()) entry.name = "Player #" + std::to_string(entry.actorId);
                // Lookup dereferences the game's Network singleton; do not call it during lobby setup.
                void* player = gCurrentGhostAI ? gamePlayer.call(entry.actorId, gamePlayer.info) : nullptr;
                entry.instance = reinterpret_cast<uintptr_t>(player);
                if (Alive(player)) {
                    ReadLife(player, entry);
                    entry.pose = ReadPose(player);
                    if (headTransform.call) {
                        void* head = nullptr;
                        if (ReadField(player, 0x40, head) && Alive(head)) {
                            // SetHeadTransform writes this GameObject's transform, not the body root.
                            void* transform = headTransform.call(head, headTransform.info);
                            entry.pose.headingValid = ReadHeading(transform, entry.pose.heading);
                        }
                    }
                    void* sanity = nullptr;
                    void* sanityOwner = nullptr;
                    float insanity = 0;
                    entry.sanityValid = ReadField(player, 0x90, sanity) &&
                        ReadField(sanity, 0x28, sanityOwner) && sanityOwner == player &&
                        ReadField(sanity, 0x30, insanity) && std::isfinite(insanity) &&
                        insanity >= 0 && insanity <= 100;
                    if (entry.sanityValid) entry.sanity = 100.0f - insanity;
                }
                if (entry.local) next.center = entry.pose;
                next.players.push_back(std::move(entry));
            }
            const bool hasLocal = std::any_of(next.players.begin(), next.players.end(),
                [](const Radar::Player& player) { return player.local; });
            if (!validRoster || !hasLocal) RejectRoster(next);
            else {
                rosterUnavailable = false;
                // This build accepts remote sanity from the lowest actor ID, not every promoted master.
                next.remoteSanityAllowed = offlineMode.call(offlineMode.info) ||
                    std::all_of(next.players.begin(), next.players.end(),
                        [localId](const Radar::Player& player) { return player.actorId >= localId; });
            }
        }
    }
    if (joined && gCurrentGhostAI) {
        next.ghost = ReadPose(gCurrentGhostAI);
        if (!ReadCursedObjects(next)) {
            next.cursedObjects.clear();
            if (!cursedUnavailable) Message("Cursed-object list unavailable or unsupported; markers cleared.");
            cursedUnavailable = true;
        } else {
            cursedUnavailable = false;
        }
        if (findBone.call && boneType && boneLayoutValid) {
            void* type = SDK::type_get_object(boneType);
            void* bone = type ? findBone.call(type, findBone.info) : nullptr;
            void* collectable = nullptr;
            uint8_t collected = 0;
            if (ReadField(bone, 0x60, collectable) &&
                ReadField(collectable, 0x9C, collected) && collected == 0)
                next.bone = ReadPose(bone);
        }
    }
    UpdatePending(next, now);
    ProcessRequests(next, now);
    if (next.generation != generation) return;
    next.actionsAllowed = next.actionsAllowed && actionsHealthy;
    WorldStatus(next);
    if (!actionsHealthy) next.actionRestriction = "Player actions disabled after a native access violation; see logs.";
    std::lock_guard<std::mutex> lock(Radar::mutex);
    // A render-thread request can arrive after ProcessRequests drained the queue.
    for (auto& player : next.players) {
        player.pending = player.pending || std::any_of(Radar::requests.begin(), Radar::requests.end(),
            [&](const Radar::Request& request) {
                return request.generation == generation && request.actorId == player.actorId;
            });
    }
    next.message = Radar::snapshot.message;
    Radar::snapshot = std::move(next);
}

void DisableRadarOnFault() {
    ready = false;
    worldHealthy = false;
    Features::bGhostVisible.store(false);
    Features::bDeadInteraction.store(false);
    actionsHealthy = false;
    Hooks::ResetRadar();
    Message("Radar observation raised an access violation; radar and player actions disabled. See logs.");
}
}

void Hooks::hkGhostVisibility(void* instance, bool visible, void* methodInfo) {
    if (instance == visibleModel) {
        for (auto& state : originalRenderers) state.enabled = visible;
        if (ForceVisibility()) visible = true;
    }
    oGhostVisibility(instance, visible, methodInfo);
}

void Hooks::hkPropGrabUpdate(void* instance, void* methodInfo) {
    UpdatePickup(instance);
    UpdateRadar();
    InvokeUpdate(oPropGrabUpdate, instance, methodInfo, false);
}

void Hooks::hkDoorFixedUpdate(void* instance, void* methodInfo) {
    InvokeUpdate(oDoorFixedUpdate, instance, methodInfo, true);
}

bool Hooks::BeginGhostTransition(void* instance) {
    if (instance != gCurrentGhostAI) return false;
    ++ghostTransitions;
    SafeRestoreVisibility();
    return true;
}

void Hooks::EndGhostTransition() {
    if (ghostTransitions) --ghostTransitions;
}

bool Hooks::PrepareWorldOptionsForUnload() {
    Features::bGhostVisible.store(false);
    Features::bDeadInteraction.store(false);
    GhostControl::desired.store(false);
    Pickup::desired.store(false);
    cleanupComplete.store(false);
    cleanupRequested.store(true);
    const uint64_t deadline = GetTickCount64() + 2000;
    while (!cleanupComplete.load() && GetTickCount64() < deadline) Sleep(10);
    if (cleanupComplete.load()) return true;
    cleanupRequested.store(false);
    Logger::Log("[ERROR] Unload cancelled: game-thread world-option cleanup was not acknowledged. Resume the game and retry END.");
    return false;
}

void Hooks::SetWorldHooksInstalled(bool installed) {
    worldHooksInstalled.store(installed);
    GhostControl::SetHooksInstalled(installed);
}

GhostControl::Context Hooks::ReadHostContext() {
    GhostControl::Context context;
    __try {
        context.ghost = gCurrentGhostAI;
        context.generation = generation;
        // Cleanup still needs current host authority after new override requests stop.
        context.authorized = ready && interactionReady && worldHealthy && worldHooksInstalled.load() &&
            Alive(gCurrentGhostAI) && Authority(currentRoom.call(currentRoom.info));
        if (!context.authorized) return context;
        void* grab = nullptr;
        void* door = nullptr;
        context.local = LocalPC(context.player, grab, door, false);
        if (!context.local || !ReadField(context.local, 0x128, context.controller) ||
            !ReadField(context.local, 0x30, context.camera)) {
            context.controller = context.camera = nullptr;
        }
        return context;
    }
    __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        worldHealthy = false;
        Logger::Log("[ERROR] World options: host context faulted; control and pickup authorization denied.");
        return {};
    }
}

void Hooks::UpdateRadar() {
    __try {
        UpdateRadarCore();
    }
    __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION
        ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        DisableRadarOnFault();
    }
}
