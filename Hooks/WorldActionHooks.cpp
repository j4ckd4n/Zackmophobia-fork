#include "../Features/WorldActions.h"
#include "../SDK/IL2CPP.h"
#include "../Utils/Logger.h"
#include <cstring>

// Build 25811160, PE 6AC7D141/06EAC000, metadata 31. Static REA:
// ev_e2db21cde922eab028e42fd231263e709ebd6702715129ea8f4d039b15ebe6a1
// ev_252ee4d6ab72191af77d18bc758730999bb37dfb3f446abb96ca704387efaa99
// ev_ea3c84e584094df08dd2313a24e7510561cc376b6b2fc797026020923e3c474c
// ev_83232da41a8fd8bf3b5fde089d21964dc33cc1aa7c384e96b03575ec36a66151
// ev_0913965aed0ac61a01b3fb2c93bb32aac62c5cd5f6ffe369877eeba2567a1e9e
// ev_5fa7f8e38dc943a319e88a5762869db72c702cf6d24b6fb4f43e1215813ea6d5
// ev_57ef44ae9f462141de85823ed75caaac93bd2ae64f8a1b51b89fa49b6127b932
// ev_05e24620811fb1ca52cfbee0893cc882ea969537599abe8a4a3880c10ac890c3
// ev_1f5a0c0b222730e0e04257c37ef6deee8eeac69d29349b96e11c6fb2b66b0504
// ev_36afa99f5d626e734fe3acba504931a096780d8050ad5a4ca0011595bb23962e
// ev_d3d9f44ce22100a3d24aaf25996d9bf23f50e36d86033fcd936cf374e5296148
// ToggleDoorLock(false,false) restores native interaction/collider state and
// invokes UnlockDoor(false), including SyncHasBeenUnlocked/SyncLockState RPCs.
// GhostActivity.GhostAbility is NOT this feature: it randomly picks ordinary
// interaction/event states. State 15 selects the current type's native ability.
// RVAs identify runtime MethodInfo entries; never invoke an unchecked base+RVA.
namespace {
    using Kind = WorldActions::Kind;
    struct Method { void* info = nullptr; };
    using Invoke = void* (*)(void*, void*, void**, void**);
    Invoke invoke = nullptr;
    void* (*unbox)(void*) = nullptr;
    void* (*methods)(void*, void**) = nullptr;
    uint32_t (*parameterCount)(void*) = nullptr;
    void (*staticValue)(void*, void*) = nullptr;
    int (*fieldFlags)(void*) = nullptr;
    Method aliveMethod, normalMethod, whisperMethod, lightMethod, getLightMethod;
    Method unlockMethod, stateMethod, rendererEnabledMethod;
    void* levelField = nullptr;
    void* networkField = nullptr;
    void* evidenceField = nullptr;
    void* valuesField = nullptr;
    bool ready = false, healthy = true, failed = false;
    std::mutex mutex;
    WorldActions::Snapshot status;
    struct Targets {
        GhostControl::Context context;
        void* activity = nullptr;
        void* interaction = nullptr;
        void* info = nullptr;
        void* level = nullptr;
        void* doors = nullptr;
        int type = -1;
        const char* abilityReason = "Current native ability prerequisites unavailable.";
    } current;
    const char* AbilityReason(const Targets& target);
    struct Request {
        bool pending = false;
        Kind kind{};
        Targets targets;
        uint64_t at = 0;
    } request;
    constexpr uint64_t MaxAge = 750;

    template<class T> bool Field(void* object, size_t offset, T& result) {
        return object && SDK::ReadSafe(static_cast<char*>(object) + offset, result);
    }
    template<class T> bool Export(HMODULE module, const char* name, T& result) {
        result = reinterpret_cast<T>(GetProcAddress(module, name));
        return result != nullptr;
    }
    bool Build(void* module) {
        IMAGE_DOS_HEADER dos{};
        IMAGE_NT_HEADERS64 nt{};
        return SDK::ReadSafe(module, dos) && dos.e_magic == IMAGE_DOS_SIGNATURE &&
            dos.e_lfanew > 0 && dos.e_lfanew < 0x10000 &&
            Field(module, dos.e_lfanew, nt) && nt.Signature == IMAGE_NT_SIGNATURE &&
            nt.FileHeader.TimeDateStamp == 0x6AC7D141 && nt.OptionalHeader.SizeOfImage == 0x06EAC000;
    }
    bool Bind(Method& method, void* klass, uintptr_t base, uintptr_t rva, uint32_t args) {
        method = {};
        void* iterator = nullptr;
        if (klass) while (void* entry = methods(klass, &iterator)) {
            uintptr_t pointer = 0;
            if (SDK::ReadSafe(entry, pointer) && pointer == base + rva && parameterCount(entry) == args)
                method.info = entry;
        }
        if (!method.info)
            Logger::Log("[WORLD] Missing method metadata: RVA 0x%llX, %u arguments.",
                static_cast<unsigned long long>(rva), args);
        return method.info != nullptr;
    }
    void* Call(const Method& method, void* self = nullptr, void** args = nullptr) {
        if (failed) return nullptr;
        if (!method.info) { failed = true; return nullptr; }
        void* exception = nullptr;
        void* result = invoke(method.info, self, args, &exception);
        if (exception) failed = true;
        return result;
    }
    bool Boolean(const Method& method, void* self, bool& result, void** args = nullptr) {
        void* boxed = Call(method, self, args);
        if (failed || !boxed || !SDK::ReadSafe(unbox(boxed), result)) { failed = true; return false; }
        return true;
    }
    bool Alive(void* object) {
        if (!object) return false;
        bool result = false;
        void* args[]{object};
        return Boolean(aliveMethod, nullptr, result, args) && result;
    }
    bool SameContext(const GhostControl::Context& a, const GhostControl::Context& b) {
        return a.generation == b.generation && a.ghost == b.ghost && a.player == b.player &&
            a.local == b.local && a.controller == b.controller && a.camera == b.camera;
    }
    bool SameTargets(const Targets& a, const Targets& b) {
        return SameContext(a.context, b.context) && a.activity == b.activity &&
            a.interaction == b.interaction && a.info == b.info && a.type == b.type &&
            a.level == b.level && a.doors == b.doors;
    }
    void Message(const char* text) {
        if (status.message != text) {
            status.message = text;
            Logger::Log("[WORLD] %s", text);
        }
    }
    bool Capture(const GhostControl::Context& context, Targets& target) {
        target = {};
        target.context = context;
        if (!context.authorized || !context.generation || !Alive(context.ghost)) return false;
        if (!Field(context.ghost, 0x58, target.activity) || !Alive(target.activity) ||
            !Field(context.ghost, 0x50, target.interaction) || !Alive(target.interaction) ||
            !Field(context.ghost, 0x38, target.info) || !Alive(target.info) ||
            !Field(target.info, 0x2C, target.type) || target.type < 0 || target.type > 30 ||
            target.type == 24) return false;
        void* owner = nullptr;
        if (!Field(target.activity, 0x28, owner) || owner != context.ghost ||
            !Field(target.interaction, 0x20, owner) || owner != context.ghost) return false;
        staticValue(levelField, &target.level);
        if (!Alive(target.level) || !Field(target.level, 0x40, owner) || owner != context.ghost ||
            !Field(target.level, 0x48, target.doors) || !target.doors) return false;
        target.abilityReason = AbilityReason(target);
        return !failed;
    }
    bool DoorAt(const Targets& target, uintptr_t index, void*& door) {
        return Field(target.doors, 0x20 + index * sizeof(void*), door);
    }
    bool MainLocked(void* door) {
        int key = -1;
        uint8_t locked = 0;
        return Alive(door) && Field(door, 0x108, key) && key == 1 &&
            Field(door, 0x20, locked) && locked == 1;
    }
    bool DoorCount(const Targets& target, unsigned& result) {
        result = 0;
        uintptr_t count = 0;
        if (!Field(target.doors, 0x18, count) || count > 512) return false;
        for (uintptr_t i = 0; i < count; ++i) {
            void* door = nullptr;
            if (!DoorAt(target, i, door)) return false;
            if (MainLocked(door)) ++result;
        }
        return !failed;
    }
    bool Idle(void* ghost) {
        int state = -1;
        // Only quiescent native states, never hunting or a running interaction/event.
        return Field(ghost, 0x30, state) && (state == 0 || state == 1 || state == 3);
    }
    bool ListContents(void* list, void*& items, int& count, int maximum) {
        uintptr_t capacity = 0;
        return Field(list, 0x18, count) && count > 0 && count <= maximum &&
            Field(list, 0x10, items) && Field(items, 0x18, capacity) &&
            capacity >= static_cast<uintptr_t>(count) && capacity <= 4096;
    }
    const char* PlayerAbilityReason() {
        if (!networkField) return "Player ability metadata unavailable; see Logs.";
        void* network = nullptr;
        staticValue(networkField, &network);
        void* list = nullptr;
        void* items = nullptr;
        int count = 0;
        if (!Alive(network) || !Field(network, 0x38, list) || !ListContents(list, items, count, 16))
            return "Native player list unavailable/empty.";
        bool eligible = false;
        for (int i = 0; i < count; ++i) {
            void* spot = nullptr;
            void* player = nullptr;
            void* room = nullptr;
            uint8_t dead = 1;
            int roomType = 1;
            if (!Field(items, 0x20 + i * sizeof(void*), spot) ||
                !Field(spot, 0x38, player) || !Alive(player) ||
                !Field(player, 0x28, dead) || !Field(player, 0x50, room) ||
                !Alive(room) || !Field(room, 0x58, roomType))
                return "Native player slots/rooms are not ready.";
            eligible |= !dead && roomType != 1;
        }
        return eligible ? "" : "No living player in a native eligible room.";
    }
    const char* AbilityReason(const Targets& target) {
        switch (target.type) {
        case 1: case 2: return PlayerAbilityReason();
        case 3: return "";
        case 5: {
            void* fuseBox = nullptr;
            uint8_t powered = 0;
            if (!Field(target.level, 0x80, fuseBox) || !Alive(fuseBox) ||
                !Field(fuseBox, 0x90, powered) || powered != 1)
                return "Jinn ability requires the native fuse box to be powered.";
            return PlayerAbilityReason();
        }
        case 9: {
            if (!valuesField) return "Demon ability metadata unavailable; see Logs.";
            void* values = nullptr;
            staticValue(valuesField, &values);
            uint8_t blocked = 1;
            return Alive(values) && Field(values, 0x23, blocked) && !blocked ? "" :
                "Native level guard prevents the Demon hunt ability.";
        }
        case 10: {
            void* room = nullptr;
            void* doors = nullptr;
            uintptr_t count = 0;
            if (!Field(target.level, 0x38, room) || !Alive(room) ||
                !Field(room, 0x28, doors) || !Field(doors, 0x18, count) || count > 512)
                return "Native favourite-room doors unavailable.";
            for (uintptr_t i = 0; i < count; ++i) {
                void* door = nullptr;
                uint8_t locked = 1, alternateLock = 1, blocked = 1;
                if (Field(doors, 0x20 + i * sizeof(void*), door) && Alive(door) &&
                    Field(door, 0x20, locked) && Field(door, 0x21, alternateLock) &&
                    Field(door, 0x159, blocked) && !locked && !alternateLock && !blocked) return "";
            }
            return "No native eligible unlocked favourite-room door for Yurei.";
        }
        case 19: {
            if (!evidenceField || !rendererEnabledMethod.info)
                return "Fingerprint ability metadata unavailable; see Logs.";
            void* evidence = nullptr;
            staticValue(evidenceField, &evidence);
            void* list = nullptr;
            void* items = nullptr;
            int count = 0;
            if (!Alive(evidence) || !Field(evidence, 0x60, list) ||
                !ListContents(list, items, count, 512))
                return "No native fingerprints available for Obake.";
            for (int i = 0; i < count; ++i) {
                void* print = nullptr;
                void* renderer = nullptr;
                bool enabled = false;
                if (!Field(items, 0x20 + i * sizeof(void*), print) || !Alive(print) ||
                    !Field(print, 0x40, renderer) || !Alive(renderer))
                    return "Native fingerprint components are not ready.";
                if (!Boolean(rendererEnabledMethod, renderer, enabled)) return "Native fingerprint check failed.";
                if (enabled) return "";
            }
            return "No active native fingerprint to accelerate; this is not hunt shapeshifting.";
        }
        default: return "No supported activatable native special ability for the current ghost type.";
        }
    }
    const char* AbilityLabel(int type) {
        switch (type) {
        case 1: return "Wraith teleport";
        case 2: return "Phantom visit";
        case 3: return "Poltergeist multi-throw";
        case 5: return "Jinn sanity drain";
        case 9: return "Demon ability - CAN START A HUNT";
        case 10: return "Yurei door slam";
        case 19: return "Obake fingerprint fade";
        default: return "Ghost ability";
        }
    }
    bool ActionMetadata(Kind kind) {
        switch (kind) {
        case Kind::UnlockEntrance: return unlockMethod.info != nullptr;
        case Kind::NormalInteraction: return normalMethod.info != nullptr;
        case Kind::Whisper: return whisperMethod.info != nullptr;
        case Kind::NearbyLight: return getLightMethod.info && lightMethod.info;
        case Kind::SpecialAbility: return stateMethod.info != nullptr;
        default: return false;
        }
    }
    void Options(const GhostControl::Context& context, bool possessed) {
        const char* labels[]{"Unlock front door", "Doll-style interaction",
            "Whisper", "Light / flicker", "Ghost ability"};
        for (size_t i = 0; i < status.options.size(); ++i) {
            auto& option = status.options[i];
            option = {static_cast<Kind>(i), labels[i], false, ""};
            if (option.kind == Kind::SpecialAbility) option.label = AbilityLabel(current.type);
            if (!healthy) option.reason = "Native action backend fault-disabled; reload required.";
            else if (!ready || !ActionMetadata(option.kind)) option.reason = "Action metadata unavailable; see Logs.";
            else if (!context.authorized) option.reason = "Requires private/offline host authority.";
            else if (!status.available) option.reason = "Current ghost/level native components unavailable.";
            else if (i == 0) {
                option.available = status.lockedEntrances > 0;
                if (!option.available) option.reason = "No locked main entrance found.";
            } else if (possessed) option.reason = "Disabled during possession/restoration; native AI outputs are suppressed.";
            else if (!Idle(context.ghost)) option.reason = "Wait until the ghost is idle/wandering; disabled during hunts/events/interactions.";
            else {
                option.available = true;
                if (option.kind == Kind::SpecialAbility) {
                    option.reason = current.abilityReason;
                    option.available = option.reason.empty();
                }
            }
        }
    }
    void Dispatch(const Request& selected) {
        const Targets& target = selected.targets;
        bool ordinary = false;
        void* args[]{&ordinary};
        switch (selected.kind) {
        case Kind::UnlockEntrance: {
            uintptr_t count = 0;
            if (!Field(target.doors, 0x18, count) || count > 512) { failed = true; return; }
            unsigned changed = 0;
            for (uintptr_t i = 0; i < count && !failed; ++i) {
                void* door = nullptr;
                if (!DoorAt(target, i, door)) { failed = true; return; }
                if (!MainLocked(door)) continue;
                bool locked = false, delayed = false;
                void* unlockArgs[]{&locked, &delayed};
                Call(unlockMethod, door, unlockArgs);
                uint8_t remainsLocked = 1;
                if (failed || !Field(door, 0x20, remainsLocked) || remainsLocked) { failed = true; return; }
                ++changed;
            }
            Message(changed ? "Entrance unlocked; later hunts can relock it." : "No locked entrance remains.");
            break;
        }
        case Kind::NormalInteraction:
            // The doll's non-heart callbacks use NormalInteraction(true).
            ordinary = true;
            Call(normalMethod, target.activity, args);
            Message("Doll-style interaction requested; needs a nearby target.");
            break;
        case Kind::Whisper:
            Call(whisperMethod, target.interaction);
            Message("Whisper requested.");
            break;
        case Kind::NearbyLight: {
            void* light = Call(getLightMethod, target.activity);
            if (failed) break;
            if (!Alive(light)) { Message("No eligible nearby light switch; nothing dispatched."); break; }
            void* lightArgs[]{light, &ordinary};
            bool dispatched = false;
            if (Boolean(lightMethod, target.activity, dispatched, lightArgs))
                Message(dispatched ? "Light interaction requested." : "Light target rejected.");
            break;
        }
        case Kind::SpecialAbility: {
            if (*target.abilityReason) { Message(target.abilityReason); break; }
            int abilityState = 15;
            void* noTarget = nullptr;
            void* stateArgs[]{&abilityState, noTarget, &ordinary};
            Call(stateMethod, target.context.ghost, stateArgs);
            Message(target.type == 9 ?
                "Demon ability requested; it can start a hunt." : "Ability requested; native conditions apply.");
            break;
        }
        default: break;
        }
    }
    bool SafeCapture(const GhostControl::Context& context, Targets& target) {
        __try { return Capture(context, target); }
        __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
            failed = true; return false;
        }
    }
    bool SafeDoors(const Targets& target, unsigned& count) {
        __try { return DoorCount(target, count); }
        __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
            failed = true; return false;
        }
    }
    void SafeDispatch(const Request& selected) {
        __try { Dispatch(selected); }
        __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
            failed = true;
        }
    }
    void Fault() {
        healthy = false;
        request = {};
        status.available = false;
        status.faulted = true;
        Message("Native world action failed; backend disabled without retry (effects may be partial).");
    }
    void Service(const GhostControl::Context& context, bool possessed, uint64_t now) {
        failed = false;
        Targets next;
        bool captured = ready && healthy && SafeCapture(context, next);
        if (request.pending && (!captured || !SameTargets(request.targets, next))) {
            request = {};
            Message("Queued action cancelled: authority/session/ghost/native target changed.");
        }
        current = next;
        status.generation = context.generation;
        status.sampledAt = now;
        status.lockedEntrances = 0;
        status.available = captured && SafeDoors(current, status.lockedEntrances);
        if (failed) Fault();
        else if (healthy && ready && !status.available)
            Message(context.authorized ? "Current ghost/level targets unavailable; actions disabled." :
                "World actions require private/offline host authority.");
        Options(context, possessed);
        if (request.pending) {
            Request selected = request;
            request = {}; // Consume before dispatch; never retry partially completed native work.
            const auto index = static_cast<size_t>(selected.kind);
            if (now < selected.at || now - selected.at > MaxAge)
                Message("Queued action expired; nothing dispatched.");
            else if (!status.options[index].available)
                Message("Queued action cancelled: native state or possession policy changed.");
            else {
                Targets rechecked;
                if (!SafeCapture(context, rechecked) || !SameTargets(selected.targets, rechecked))
                    Message("Queued action cancelled: native target changed before dispatch.");
                else if (selected.kind != Kind::UnlockEntrance &&
                    (!Idle(context.ghost) || (selected.kind == Kind::SpecialAbility && *rechecked.abilityReason)))
                    Message("Queued action cancelled: native ability eligibility/state changed before dispatch.");
                else SafeDispatch(selected);
            }
            if (failed) Fault();
            if (status.available && !SafeDoors(current, status.lockedEntrances)) status.available = false;
            if (failed) Fault();
            Options(context, possessed);
        }
        status.pending = request.pending;
    }
}

namespace {
    void* SingletonField(void* klass) {
        void* found = nullptr;
        void* iterator = nullptr;
        if (klass) while (void* field = SDK::class_get_fields(klass, &iterator)) {
            const int flags = fieldFlags(field);
            if ((flags & (0x10 | 0x40)) == 0x10 && SDK::field_get_offset(field) == 0 &&
                SDK::class_from_type(SDK::field_get_type(field)) == klass) {
                if (found) {
                    Logger::Log("[WORLD] Ambiguous singleton metadata for class %p.", klass);
                    return nullptr;
                }
                found = field;
            }
        }
        if (!found) Logger::Log("[WORLD] Missing typed singleton metadata for class %p.", klass);
        return found;
    }
    void BindMetadata(void* image, void* unityImage, uintptr_t base) {
        const auto klass = [image](const char* name) { return SDK::class_from_name(image, "", name); };
        void* activity = klass("GhostActivity");
        void* interaction = klass("GhostInteraction");
        void* object = SDK::class_from_name(unityImage, "UnityEngine", "Object");
        aliveMethod.info = object ? SDK::get_method(object, "op_Implicit", 1) : nullptr;
        levelField = SingletonField(klass("LevelController"));
        networkField = SingletonField(klass("Network"));
        evidenceField = SingletonField(klass("EvidenceController"));
        valuesField = SingletonField(klass("LevelValues"));
        void* renderer = SDK::class_from_name(unityImage, "UnityEngine", "Renderer");
        rendererEnabledMethod.info = renderer ? SDK::get_method(renderer, "get_enabled", 0) : nullptr;
        Bind(normalMethod, activity, base, 0x2007000, 1);
        Bind(whisperMethod, interaction, base, 0x207F280, 0);
        Bind(getLightMethod, activity, base, 0x2006120, 0);
        Bind(lightMethod, activity, base, 0x2007340, 2);
        Bind(unlockMethod, klass("Door"), base, 0x264E530, 2);
        Bind(stateMethod, klass("GhostAI"), base, 0x2021290, 3);
        ready = aliveMethod.info && levelField;
        if (!aliveMethod.info) Logger::Log("[WORLD] Unity Object.op_Implicit metadata unavailable.");
        Logger::Log("[WORLD] Action metadata: level=%d, network=%d, evidence=%d, values=%d; door=%d, interaction=%d, whisper=%d, light=%d, ability=%d.",
            levelField != nullptr, networkField != nullptr, evidenceField != nullptr, valuesField != nullptr,
            ActionMetadata(Kind::UnlockEntrance), ActionMetadata(Kind::NormalInteraction),
            ActionMetadata(Kind::Whisper), ActionMetadata(Kind::NearbyLight), ActionMetadata(Kind::SpecialAbility));
    }
}

void WorldActions::Initialize(void* domain, void* image, void* unityImage) {
    std::lock_guard<std::mutex> lock(mutex);
    ready = false;
    healthy = true;
    request = {};
    current = {};
    status = {};
    HMODULE module = GetModuleHandleA("GameAssembly.dll");
    if (!domain || !image || !unityImage || !Build(module) ||
        !SDK::class_from_name || !SDK::get_method || !SDK::class_get_fields || !SDK::field_get_offset ||
        !SDK::field_get_type || !SDK::class_from_type ||
        !Export(module, "il2cpp_runtime_invoke", invoke) || !Export(module, "il2cpp_object_unbox", unbox) ||
        !Export(module, "il2cpp_class_get_methods", methods) ||
        !Export(module, "il2cpp_method_get_param_count", parameterCount) ||
        !Export(module, "il2cpp_field_static_get_value", staticValue) ||
        !Export(module, "il2cpp_field_get_flags", fieldFlags)) {
        Message("World actions unavailable: unsupported build or missing IL2CPP exports.");
        return;
    }
    BindMetadata(image, unityImage, reinterpret_cast<uintptr_t>(module));
    Message(ready ? "Action metadata loaded." : "Action metadata unavailable; see Logs.");
}
void WorldActions::Update(const GhostControl::Context& context, bool possessed) {
    std::lock_guard<std::mutex> lock(mutex);
    Service(context, possessed || GhostControl::desired.load(), GetTickCount64());
}
WorldActions::Snapshot WorldActions::Read() {
    std::lock_guard<std::mutex> lock(mutex);
    return status;
}
bool WorldActions::Queue(Kind kind, uint64_t generation, uint64_t now) {
    std::lock_guard<std::mutex> lock(mutex);
    const auto index = static_cast<size_t>(kind);
    if (index >= status.options.size() || !healthy || !ready || !status.available ||
        request.pending || generation != status.generation || now < status.sampledAt ||
        now - status.sampledAt > MaxAge || !status.options[index].available) return false;
    request = {true, kind, current, now};
    status.pending = true;
    Message("World action queued for game-thread validation.");
    return true;
}
void WorldActions::Reset() {
    std::lock_guard<std::mutex> lock(mutex);
    request = {};
    current = {};
    status = {};
    status.faulted = !healthy;
    Message("World action session reset; pending requests cancelled.");
}
