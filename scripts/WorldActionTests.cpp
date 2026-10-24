#include <cassert>
#include <iostream>
#include <vector>
#include "../Hooks/WorldActionHooks.cpp"

namespace Test {
    struct Object {
        alignas(void*) char bytes[0x1C0]{};
        template<class T> void Set(size_t offset, T value) {
            std::memcpy(bytes + offset, &value, sizeof(value));
        }
        template<class T> T Get(size_t offset) const {
            T result{};
            std::memcpy(&result, bytes + offset, sizeof(result));
            return result;
        }
    };
    Object ghost, replacement, activity, interaction, info, level, doors, alternateDoors;
    Object mainDoor, secondary, permanent, interior, openMain, light, alternateActivity;
    Object network, players, playerItems, spot, player, room, roomDoors, fuseBox, values;
    Object evidence, fingerprints, printItems, fingerprint, renderer;
    GhostControl::Context context;
    int aliveId, normalId, whisperId, unlockId, lightId, getLightId, stateId;
    int levelId, networkId, evidenceId, valuesId, rendererId, lastAbilityType;
    int normalCalls, whisperCalls, unlockCalls, lightCalls, abilityCalls, huntCalls;
    bool returnValue, eligibleLight, rejectLight, managedFailure, accessViolation, refuseUnlock;
    bool destroyed;
    bool activeFingerprint;
    int captureCount;
    uint64_t now = 10000;

    void* Invoke(void* method, void* self, void** args, void** exception) {
        if (method == &aliveId) {
            returnValue = args && args[0] && !(destroyed && args[0] == &ghost);
            return &returnValue;
        }
        if (managedFailure) { *exception = &aliveId; return nullptr; }
        if (accessViolation) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
        if (method == &normalId) {
            assert(self == &activity && args && *static_cast<bool*>(args[0]));
            ++normalCalls;
        } else if (method == &whisperId) {
            assert(self == &interaction && !args);
            ++whisperCalls;
        } else if (method == &unlockId) {
            assert(self == &mainDoor && !*static_cast<bool*>(args[0]) && !*static_cast<bool*>(args[1]));
            ++unlockCalls;
            if (!refuseUnlock) static_cast<Object*>(self)->Set<uint8_t>(0x20, 0);
        } else if (method == &getLightId) {
            assert(self == &activity);
            return eligibleLight ? &light : nullptr;
        } else if (method == &lightId) {
            assert(self == &activity && args[0] == &light && !*static_cast<bool*>(args[1]));
            ++lightCalls;
            returnValue = !rejectLight;
            return &returnValue;
        } else if (method == &stateId) {
            assert(self == &ghost && args[1] == nullptr && !*static_cast<bool*>(args[2]));
            const int state = *static_cast<int*>(args[0]);
            if (state == 2) ++huntCalls;
            lastAbilityType = info.Get<int>(0x2C);
            assert(state == 15 && (lastAbilityType == 1 || lastAbilityType == 2 ||
                lastAbilityType == 3 || lastAbilityType == 5 || lastAbilityType == 9 ||
                lastAbilityType == 10 || lastAbilityType == 19));
            ++abilityCalls;
        } else if (method == &rendererId) {
            assert(self == &renderer && !args);
            returnValue = activeFingerprint;
            return &returnValue;
        } else assert(false);
        return nullptr;
    }
    void Door(Object& door, int key, bool locked) {
        door.Set(0x108, key);
        door.Set<uint8_t>(0x20, locked ? 1 : 0);
    }
    void Reset() {
        ready = healthy = true;
        failed = false;
        WorldActions::Reset();
        ghost = {}; replacement = {}; activity = {}; interaction = {}; info = {};
        level = {}; doors = {}; alternateDoors = {}; mainDoor = {}; secondary = {};
        permanent = {}; interior = {}; openMain = {}; light = {}; alternateActivity = {};
        network = {}; players = {}; playerItems = {}; spot = {}; player = {}; room = {};
        roomDoors = {}; fuseBox = {}; values = {}; evidence = {}; fingerprints = {};
        printItems = {}; fingerprint = {}; renderer = {};
        normalCalls = whisperCalls = unlockCalls = lightCalls = abilityCalls = huntCalls = 0;
        managedFailure = accessViolation = refuseUnlock = rejectLight = destroyed = false;
        captureCount = 0;
        eligibleLight = true;
        activeFingerprint = true;
        lastAbilityType = -1;
        GhostControl::desired.store(false);
        context = {&ghost, &ghost, &ghost, &ghost, &ghost, 7, true};
        ghost.Set(0x58, static_cast<void*>(&activity));
        ghost.Set(0x50, static_cast<void*>(&interaction));
        ghost.Set(0x38, static_cast<void*>(&info));
        ghost.Set(0x30, 0);
        activity.Set(0x28, static_cast<void*>(&ghost));
        interaction.Set(0x20, static_cast<void*>(&ghost));
        info.Set(0x2C, 3);
        level.Set(0x40, static_cast<void*>(&ghost));
        level.Set(0x48, static_cast<void*>(&doors));
        doors.Set<uintptr_t>(0x18, 5);
        Object* entries[]{&mainDoor, &secondary, &permanent, &interior, &openMain};
        for (size_t i = 0; i < std::size(entries); ++i)
            doors.Set(0x20 + i * sizeof(void*), static_cast<void*>(entries[i]));
        Door(mainDoor, 1, true);
        Door(secondary, 2, true);
        Door(permanent, 3, true);
        Door(interior, 0, true);
        Door(openMain, 1, false);
        network.Set(0x38, static_cast<void*>(&players));
        players.Set(0x10, static_cast<void*>(&playerItems));
        players.Set(0x18, 1);
        playerItems.Set<uintptr_t>(0x18, 1);
        playerItems.Set(0x20, static_cast<void*>(&spot));
        spot.Set(0x38, static_cast<void*>(&player));
        player.Set(0x50, static_cast<void*>(&room));
        room.Set(0x58, 0);
        room.Set(0x28, static_cast<void*>(&roomDoors));
        roomDoors.Set<uintptr_t>(0x18, 1);
        roomDoors.Set(0x20, static_cast<void*>(&openMain));
        level.Set(0x38, static_cast<void*>(&room));
        level.Set(0x80, static_cast<void*>(&fuseBox));
        fuseBox.Set<uint8_t>(0x90, 1);
        evidence.Set(0x60, static_cast<void*>(&fingerprints));
        fingerprints.Set(0x10, static_cast<void*>(&printItems));
        fingerprints.Set(0x18, 1);
        printItems.Set<uintptr_t>(0x18, 1);
        printItems.Set(0x20, static_cast<void*>(&fingerprint));
        fingerprint.Set(0x40, static_cast<void*>(&renderer));
        invoke = Invoke;
        unbox = [](void* boxed) -> void* { return boxed; };
        levelField = &levelId; networkField = &networkId;
        evidenceField = &evidenceId; valuesField = &valuesId;
        staticValue = [](void* field, void* out) {
            *static_cast<void**>(out) = field == levelField ? &level :
                field == networkField ? &network : field == evidenceField ? &evidence : &values;
        };
        aliveMethod.info = &aliveId; normalMethod.info = &normalId;
        whisperMethod.info = &whisperId; unlockMethod.info = &unlockId;
        lightMethod.info = &lightId; getLightMethod.info = &getLightId; stateMethod.info = &stateId;
        rendererEnabledMethod.info = &rendererId;
        Service(context, false, now);
        assert(status.available && status.lockedEntrances == 1);
    }
    bool Queue(Kind kind) { return WorldActions::Queue(kind, context.generation, now); }
    void Tick(bool possessed = false, uint64_t delay = 1) {
        now += delay;
        Service(context, possessed, now);
    }
    void DispatchSelection() {
        Reset();
        assert(Queue(Kind::NormalInteraction));
        assert(!Queue(Kind::Whisper));
        Tick(); Tick();
        assert(normalCalls == 1 && whisperCalls == 0);
        assert(Queue(Kind::Whisper)); Tick();
        assert(whisperCalls == 1);
        assert(Queue(Kind::NearbyLight)); Tick();
        assert(lightCalls == 1);
        assert(Queue(Kind::SpecialAbility)); Tick(); Tick();
        assert(abilityCalls == 1 && huntCalls == 0);
        eligibleLight = false;
        assert(Queue(Kind::NearbyLight)); Tick();
        assert(lightCalls == 1 && !status.faulted);
        assert(status.message.find("No eligible nearby") != std::string::npos);
        eligibleLight = true; rejectLight = true;
        assert(Queue(Kind::NearbyLight)); Tick();
        assert(lightCalls == 2 && status.message.find("rejected") != std::string::npos);
    }
    void DoorFiltering() {
        Reset();
        assert(Queue(Kind::UnlockEntrance)); Tick(); Tick();
        assert(unlockCalls == 1 && !mainDoor.Get<uint8_t>(0x20));
        assert(secondary.Get<uint8_t>(0x20) && permanent.Get<uint8_t>(0x20) &&
            interior.Get<uint8_t>(0x20) && !openMain.Get<uint8_t>(0x20));
        assert(!status.lockedEntrances && !Queue(Kind::UnlockEntrance));
        mainDoor.Set<uint8_t>(0x20, 1);
        Tick();
        assert(mainDoor.Get<uint8_t>(0x20) && unlockCalls == 1);
        assert(Queue(Kind::UnlockEntrance)); Tick(true);
        assert(unlockCalls == 2); // Door action never competes for possessed AI outputs.
    }
    void AuthorityAndSession() {
        Reset();
        assert(Queue(Kind::Whisper));
        context.authorized = false; Tick();
        assert(!whisperCalls && !request.pending && !status.available && !Queue(Kind::Whisper));
        context.authorized = true; Tick();
        assert(!whisperCalls);
        assert(Queue(Kind::Whisper));
        ++context.generation; Tick();
        assert(!whisperCalls && !request.pending);
        assert(!WorldActions::Queue(Kind::Whisper, context.generation - 1, now + 1));
        assert(Queue(Kind::Whisper));
        context.ghost = &replacement; Tick();
        assert(!whisperCalls && !request.pending);
        Reset(); assert(Queue(Kind::Whisper));
        context.local = &replacement; Tick();
        assert(!whisperCalls && !request.pending);
        Reset(); assert(Queue(Kind::Whisper));
        destroyed = true; Tick();
        assert(!whisperCalls && !status.available);
    }
    void NativeTargetReplacement() {
        Reset(); assert(Queue(Kind::NormalInteraction));
        alternateActivity.Set(0x28, static_cast<void*>(&ghost));
        ghost.Set(0x58, static_cast<void*>(&alternateActivity)); Tick();
        assert(!normalCalls && !request.pending);
        Reset(); assert(Queue(Kind::UnlockEntrance));
        alternateDoors = doors;
        level.Set(0x48, static_cast<void*>(&alternateDoors)); Tick();
        assert(!unlockCalls && !request.pending);
        Reset(); assert(Queue(Kind::SpecialAbility));
        info.Set(0x2C, 0); Tick();
        assert(!abilityCalls && !request.pending && !status.options[4].available);
        Reset(); assert(Queue(Kind::Whisper));
        alternateDoors = doors;
        staticValue = [](void*, void* out) {
            if (++captureCount == 2) level.Set(0x48, static_cast<void*>(&alternateDoors));
            *static_cast<void**>(out) = &level;
        };
        Tick();
        assert(!whisperCalls && !request.pending);
    }
    void StaleAndReset() {
        Reset();
        assert(!WorldActions::Queue(Kind::Whisper, 7, now - 1));
        assert(!WorldActions::Queue(Kind::Whisper, 7, now + MaxAge + 1));
        assert(!Queue(static_cast<Kind>(100)));
        assert(Queue(Kind::Whisper)); Tick(false, MaxAge + 1);
        assert(!whisperCalls && !request.pending);
        Reset(); assert(Queue(Kind::Whisper));
        WorldActions::Reset(); Tick();
        assert(!whisperCalls && !request.pending);
    }
    void AbilityAndPossessionPolicy() {
        Reset();
        for (int type : {0, 4, 20, 24, 30}) {
            info.Set(0x2C, type); Tick();
            assert(!status.options[4].available && !status.options[4].reason.empty());
            assert(!Queue(Kind::SpecialAbility));
        }
        info.Set(0x2C, 3); Tick();
        assert(Queue(Kind::SpecialAbility)); Tick(true);
        assert(!abilityCalls && status.options[4].reason.find("possession") != std::string::npos);
        Tick();
        assert(Queue(Kind::SpecialAbility));
        ghost.Set(0x30, 2); Tick();
        assert(!abilityCalls && !Queue(Kind::Whisper));
        for (int state : {4, 6, 14, 15}) {
            ghost.Set(0x30, state); Tick();
            assert(!Queue(Kind::SpecialAbility));
        }
        ghost.Set(0x30, 3); Tick();
        assert(Queue(Kind::SpecialAbility)); Tick();
        assert(abilityCalls == 1 && !huntCalls);
    }
    void CurrentTypeAbilityDispatch() {
        for (int type : {1, 2, 3, 5, 9, 10, 19}) {
            Reset();
            info.Set(0x2C, type); Tick();
            assert(status.options[4].available);
            if (type == 9) assert(status.options[4].label.find("CAN START A HUNT") != std::string::npos);
            assert(Queue(Kind::SpecialAbility)); Tick(); Tick();
            assert(abilityCalls == 1 && lastAbilityType == type && !huntCalls);
            assert(status.message.find("requested") != std::string::npos);
            assert(Queue(Kind::SpecialAbility)); Tick(true);
            assert(abilityCalls == 1 && !status.options[4].available);
            Tick();
            assert(Queue(Kind::SpecialAbility));
            ghost.Set(0x30, 2); Tick();
            assert(abilityCalls == 1 && !status.options[4].available);
            ghost.Set(0x30, 6); Tick();
            assert(!status.options[4].available);
        }
        Reset(); info.Set(0x2C, 1); Tick();
        assert(Queue(Kind::SpecialAbility));
        info.Set(0x2C, 2); Tick();
        assert(!abilityCalls && !request.pending && status.options[4].available);
    }
    void ConditionalAbilityGuards() {
        for (int type : {1, 2}) {
            Reset(); info.Set(0x2C, type); Tick();
            assert(Queue(Kind::SpecialAbility));
            player.Set<uint8_t>(0x28, 1); Tick();
            assert(!abilityCalls && !Queue(Kind::SpecialAbility));
            assert(status.options[4].reason.find("living player") != std::string::npos);
            player.Set<uint8_t>(0x28, 0); room.Set(0x58, 1); Tick();
            assert(!Queue(Kind::SpecialAbility));
            players.Set(0x18, 0); Tick();
            assert(status.options[4].reason.find("empty") != std::string::npos);
        }
        Reset(); info.Set(0x2C, 5); Tick();
        assert(Queue(Kind::SpecialAbility));
        fuseBox.Set<uint8_t>(0x90, 0); Tick();
        assert(!abilityCalls && status.options[4].reason.find("fuse box") != std::string::npos);
        fuseBox.Set<uint8_t>(0x90, 1); Tick();
        assert(Queue(Kind::SpecialAbility));
        captureCount = 0;
        staticValue = [](void* field, void* out) {
            if (field == levelField && ++captureCount == 2) fuseBox.Set<uint8_t>(0x90, 0);
            *static_cast<void**>(out) = field == levelField ? &level : &network;
        };
        Tick();
        assert(!abilityCalls && status.message.find("before dispatch") != std::string::npos);
        Reset(); info.Set(0x2C, 9); Tick();
        assert(Queue(Kind::SpecialAbility));
        values.Set<uint8_t>(0x23, 1); Tick();
        assert(!abilityCalls && status.options[4].reason.find("level guard") != std::string::npos);
        Reset(); info.Set(0x2C, 10); Tick();
        assert(Queue(Kind::SpecialAbility));
        openMain.Set<uint8_t>(0x159, 1); Tick();
        assert(!abilityCalls && status.options[4].reason.find("eligible") != std::string::npos);
        openMain.Set<uint8_t>(0x159, 0);
        openMain.Set<uint8_t>(0x20, 1); Tick();
        assert(!Queue(Kind::SpecialAbility));
        Reset(); info.Set(0x2C, 19); Tick();
        assert(Queue(Kind::SpecialAbility));
        activeFingerprint = false; Tick();
        assert(!abilityCalls && status.options[4].reason.find("No active") != std::string::npos);
        fingerprints.Set(0x18, 0); Tick();
        assert(status.options[4].reason.find("No native fingerprints") != std::string::npos);
        fingerprints.Set(0x18, 1); activeFingerprint = true; managedFailure = true; Tick();
        assert(status.faulted && !abilityCalls && !Queue(Kind::SpecialAbility));
    }
    void FailuresAreTerminal() {
        for (int failure = 0; failure < 3; ++failure) {
            Reset();
            assert(Queue(failure == 2 ? Kind::UnlockEntrance : Kind::Whisper));
            managedFailure = failure == 0;
            accessViolation = failure == 1;
            refuseUnlock = failure == 2;
            Tick(); Tick();
            assert(status.faulted && !healthy && !request.pending && !Queue(Kind::Whisper));
            assert(whisperCalls == 0 && unlockCalls == (failure == 2 ? 1 : 0));
            managedFailure = accessViolation = refuseUnlock = false;
            WorldActions::Reset(); Tick();
            assert(!healthy && !Queue(Kind::Whisper));
        }
    }
    void MissingOptionalMetadata() {
        Reset();
        networkField = evidenceField = valuesField = nullptr;
        rendererEnabledMethod = {};
        whisperMethod = {};
        Tick();
        assert(status.available && status.options[0].available && status.options[1].available);
        assert(!status.options[2].available && status.options[2].reason.find("metadata") != std::string::npos);
        assert(Queue(Kind::NormalInteraction)); Tick();
        assert(normalCalls == 1 && !status.faulted);
        assert(Queue(Kind::UnlockEntrance)); Tick();
        assert(unlockCalls == 1);
        for (int type : {1, 2, 5, 9, 19}) {
            info.Set(0x2C, type); Tick();
            assert(status.available && !status.options[4].available && !status.faulted);
            assert(status.options[1].available);
        }
    }
}
namespace MetadataTest {
    enum Class { Level, Network, Evidence, Values, Activity, Interaction, Door, Ghost, Object, Renderer, Other, Count };
    struct Field { size_t offset; int flags; void* type; };
    struct Method { uintptr_t address; uint32_t arguments; };
    struct Klass {
        const char* name;
        std::vector<Field> fields;
        std::vector<Method> entries;
    };
    Klass classes[Count];
    constexpr uintptr_t Base = 0x180000000;
    void Reset() {
        const char* names[]{"LevelController", "Network", "EvidenceController", "LevelValues",
            "GhostActivity", "GhostInteraction", "Door", "GhostAI", "Object", "Renderer", "Other"};
        for (size_t i = 0; i < Count; ++i) classes[i] = {names[i], {}, {}};
        for (int i : {Level, Network, Evidence, Values}) {
            classes[i].fields = {{0, 0x16, &classes[i]}, {0, 0x50, &classes[Other]},
                {0, 0x10, &classes[Other]}, {0x20, 0x6, &classes[Other]}};
        }
        classes[Activity].entries = {{Base + 0x2007000, 1}, {Base + 0x2006120, 0}, {Base + 0x2007340, 2}};
        classes[Interaction].entries = {{Base + 0x207F280, 0}};
        classes[Door].entries = {{Base + 0x264E530, 2}};
        classes[Ghost].entries = {{Base + 0x2021290, 3}};
        SDK::class_from_name = [](void*, const char*, const char* name) -> void* {
            for (auto& klass : classes) if (!std::strcmp(klass.name, name)) return &klass;
            return nullptr;
        };
        SDK::get_method = [](void* klass, const char* name, int args) -> void* {
            if (klass == &classes[Object] && !std::strcmp(name, "op_Implicit") && args == 1) return &Test::aliveId;
            if (klass == &classes[Renderer] && !std::strcmp(name, "get_enabled") && args == 0) return &Test::rendererId;
            return nullptr;
        };
        SDK::class_get_fields = [](void* klass, void** iterator) -> void* {
            auto& fields = static_cast<Klass*>(klass)->fields;
            const auto index = reinterpret_cast<uintptr_t>(*iterator);
            if (index >= fields.size()) return nullptr;
            *iterator = reinterpret_cast<void*>(index + 1);
            return &fields[index];
        };
        SDK::field_get_offset = [](void* field) { return static_cast<Field*>(field)->offset; };
        SDK::field_get_type = [](void* field) -> const void* { return static_cast<Field*>(field)->type; };
        SDK::class_from_type = [](const void* type) { return const_cast<void*>(type); };
        fieldFlags = [](void* field) { return static_cast<Field*>(field)->flags; };
        methods = [](void* klass, void** iterator) -> void* {
            auto& entries = static_cast<Klass*>(klass)->entries;
            const auto index = reinterpret_cast<uintptr_t>(*iterator);
            if (index >= entries.size()) return nullptr;
            *iterator = reinterpret_cast<void*>(index + 1);
            return &entries[index];
        };
        parameterCount = [](void* method) { return static_cast<Method*>(method)->arguments; };
    }
    void TypedSingletonAndPartialBindings() {
        Reset();
        BindMetadata(&classes, &classes, Base);
        assert(ready && levelField == &classes[Level].fields[0]);
        for (Kind kind : {Kind::UnlockEntrance, Kind::NormalInteraction, Kind::Whisper, Kind::NearbyLight, Kind::SpecialAbility})
            assert(ActionMetadata(kind));
        // Optional ability metadata and one missing action must not disable the others.
        for (int i : {Network, Evidence, Values}) classes[i].fields.clear();
        classes[Renderer].name = "UnavailableRenderer";
        classes[Interaction].entries.clear();
        BindMetadata(&classes, &classes, Base);
        assert(ready && !networkField && !evidenceField && !valuesField);
        assert(!rendererEnabledMethod.info && !ActionMetadata(Kind::Whisper));
        assert(ActionMetadata(Kind::UnlockEntrance) && ActionMetadata(Kind::NormalInteraction));
        classes[Level].fields.push_back({0, 0x16, &classes[Level]});
        assert(!SingletonField(&classes[Level]));
        classes[Level].fields.clear();
        BindMetadata(&classes, &classes, Base);
        assert(!ready);
    }
}
int main() {
    MetadataTest::TypedSingletonAndPartialBindings();
    Test::DispatchSelection();
    Test::DoorFiltering();
    Test::AuthorityAndSession();
    Test::NativeTargetReplacement();
    Test::StaleAndReset();
    Test::AbilityAndPossessionPolicy();
    Test::CurrentTypeAbilityDispatch();
    Test::ConditionalAbilityGuards();
    Test::FailuresAreTerminal();
    Test::MissingOptionalMetadata();
    std::cout << "WorldActionTests: passed\n";
}
