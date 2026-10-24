#include <array>
#include <cassert>
#include <limits>
#include "../Hooks/GhostControlHooks.cpp"

namespace Test {
    std::array<char, 0x1B0> ghost{};
    std::array<char, 0x180> player{}, local{};
    std::array<char, 0xA0> controller{};
    std::array<char, 0xC0> mouse{};
    std::array<char, 0xE0> hunt{};
    std::array<char, 0x30> machine{}, agent{}, source{}, camera{}, cameraObject{};
    struct Transform { char header[0x20]{}; Vec3 position{}; Quaternion rotation{}; } ghostTransform, cameraTransform;
    struct FieldInfo { size_t offset; void* type; };
    int ghostType, agentType, floatType, huntType;
    FieldInfo fields[]{{0x10, &ghostType}, {0x18, &agentType}, {0x88, &floatType}, {0x8C, &floatType}};
    GhostControl::Context context;
    Vec3 navPosition{};
    int frame = 0, ticks = 0, entries = 0, exits = 0, pins = 0, copies = 0;
    bool sourceEnabled = true, cameraEnabled = false, nativeKill = false, failEnable = false, endOnKill = true;
    bool rejectHunt = false;

    template<typename T> T Read(void* object, size_t offset) {
        T value{};
        assert(Field(object, offset, value));
        return value;
    }
    bool Near(float a, float b) { return std::fabs(a - b) < 0.0001f; }
    bool Exists(void* object, void*) { return object != nullptr; }
    void* Fields(void* klass, void** iterator) {
        if (klass != &huntType) return nullptr;
        const uintptr_t index = reinterpret_cast<uintptr_t>(*iterator);
        if (index >= std::size(fields)) return nullptr;
        *iterator = reinterpret_cast<void*>(index + 1);
        return &fields[index];
    }
    void* TransformOf(void* object, void*) {
        return object == ghost.data() ? static_cast<void*>(&ghostTransform) : &cameraTransform;
    }
    void Position(void* object, Vec3* out, void*) { *out = static_cast<Transform*>(object)->position; }
    void SetPosition(void* object, Vec3* value, void*) { static_cast<Transform*>(object)->position = *value; }
    void Rotation(void* object, Quaternion* out, void*) { *out = static_cast<Transform*>(object)->rotation; }
    void SetRotation(void* object, Quaternion* value, void*) { static_cast<Transform*>(object)->rotation = *value; }
    void Enabled(void* object, bool value, void*) {
        if (object == source.data()) {
            if (failEnable && value) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
            sourceEnabled = value;
        } else cameraEnabled = value;
    }
    void State(void* object, int state) {
        if (!GhostControl::AllowTransition(object, state)) return;
        if (state == 2 && rejectHunt) return;
        assert(Write(object, 0x30, state));
        if (state == 2) ++entries;
        else ++exits;
    }
    void NativeTick(void*, void*) {
        ++ticks;
        SetPublicDestination(agent.data(), {20, 0, 20}, nullptr);
        SetPublicDestinationProperty(agent.data(), {30, 0, 30}, nullptr);
        PublicWarp(agent.data(), {40, 0, 40}, nullptr);
        ghostTransform.rotation = GhostControl::ViewRotation(1, 0);
        const float duration = Read<float>(hunt.data(), 0x88);
        assert(Write(hunt.data(), 0x88, duration - 0.02f));
        if (duration < 0) State(ghost.data(), 3);
        if (nativeKill) {
            nativeKill = false;
            if (endOnKill) State(ghost.data(), 3);
        }
    }
    void Reset() {
        session = {};
        pendingPlayerRestore = {};
        playerDepth = 0;
        ghost = {}; player = {}; controller = {}; mouse = {}; hunt = {}; machine = {};
        navPosition = {};
        ghostTransform = {}; cameraTransform = {};
        sourceEnabled = true; cameraEnabled = nativeKill = failEnable = false;
        endOnKill = true;
        rejectHunt = false;
        frame = ticks = entries = exits = pins = copies = 0;
        ready = healthy = installed = true;
        GhostControl::desired.store(false);
        GhostControl::huntRequested.store(false);
        GhostControl::inputCaptured.store(false);
        GhostControl::speed.store(2);
        GhostControl::firstPerson.store(false);
        GhostControl::cameraDistance.store(3);
        context = {ghost.data(), player.data(), local.data(), controller.data(), source.data(), 7, true};
        assert(Write(ghost.data(), 0x28, static_cast<void*>(machine.data())));
        assert(Write(ghost.data(), 0x40, static_cast<void*>(agent.data())));
        assert(Write(machine.data(), 0x10, static_cast<void*>(hunt.data())));
        assert(Write(hunt.data(), 0x10, static_cast<void*>(ghost.data())));
        assert(Write(hunt.data(), 0x18, static_cast<void*>(agent.data())));
        assert(Write(hunt.data(), 0x88, 60.0f));
        assert(Write(hunt.data(), 0x8C, -1.0f));
        assert(Write(controller.data(), 0x38, static_cast<void*>(mouse.data())));
        assert(Write(controller.data(), 0x20, uint8_t{1}));
        assert(Write(controller.data(), 0x21, uint8_t{1}));
        assert(Write(mouse.data(), 0x10, 1.0f));
        assert(Write(mouse.data(), 0x14, 1.0f));
        ghostClass = &ghostType; agentClass = &agentType; floatClass = &floatType;
        SDK::class_get_fields = Fields;
        SDK::field_get_offset = [](void* field) { return static_cast<FieldInfo*>(field)->offset; };
        SDK::field_get_type = [](void* field) -> const void* { return static_cast<FieldInfo*>(field)->type; };
        SDK::class_from_type = [](const void* type) -> void* { return const_cast<void*>(type); };
        SDK::object_get_class = [](void*) -> void* { return &huntType; };
        alive.call = Exists;
        enabled.call = [](void*, void*) { return sourceEnabled; };
        originalEnabled = Enabled;
        getTransform.call = TransformOf;
        position.call = Position; setPosition.call = SetPosition;
        rotation.call = Rotation; setRotation.call = SetRotation;
        deltaTime.call = [](void*) { return 0.02f; };
        frameCount.call = [](void*) { return frame; };
        nextPosition.call = [](void*, Vec3* value, void*) { *value = navPosition; };
        setNextPosition.call = [](void*, Vec3* value, void*) { navPosition = *value; };
        samplePosition.call = [](Vec3* value, NavHit* hit, float, int, void*) { hit->position = *value; return true; };
        navigationRay.call = [](Vec3*, Vec3*, NavHit*, int, void*) { return false; };
        cameraRay.call = [](Vec3, Vec3, PhysicsHit*, int, int, void*) { return false; };
        objectNew = [](void*) -> void* { return cameraObject.data(); };
        pin = [](void*, bool) -> uint32_t { ++pins; return static_cast<uint32_t>(pins); };
        unpin = [](uint32_t) { --pins; };
        gameObjectConstructor.call = [](void*, void*) {};
        addComponent.call = [](void*, void*, void*) -> void* { return camera.data(); };
        SDK::type_get_object = [](const void*) -> void* { return &huntType; };
        copyCamera.call = [](void*, void*, void*) { ++copies; };
        destroy.call = [](void*, void*) {};
        resetPath.call = [](void*, void*) {};
        originalTick = NativeTick;
        originalDestination = [](void*, Vec3*, void*) { return true; };
        originalWarp = originalDestination;
        originalDestinationProperty = [](void*, Vec3*, void*) {};
        originalPublicDestination = [](void*, Vec3 target, void*) { navPosition = target; return true; };
        originalPublicWarp = originalPublicDestination;
        originalPublicDestinationProperty = [](void*, Vec3 target, void*) { navPosition = target; };
    }
    void Start(bool hunting = true) {
        GhostControl::desired.store(true);
        GhostControl::huntRequested.store(hunting);
        GhostControl::Update(context);
        assert(session.active && sourceEnabled == false && cameraEnabled && copies == 1 && pins == 4);
    }
    void Lifecycle() {
        Reset(); Start();
        for (int i = 0; i < 100; ++i) Tick(machine.data(), nullptr);
        assert(ticks == 100 && Near(Read<float>(hunt.data(), 0x88), 60));
        assert(Near(navPosition.x, 0) && Near(navPosition.z, 0));
        float heading = 0;
        assert(Radar::Heading(ghostTransform.rotation, heading) && Near(heading, 0));
        Vec3 target{1, 0, 1};
        assert(!SetDestination(agent.data(), &target, nullptr));
        assert(SetDestination(camera.data(), &target, nullptr));
        assert(!Warp(agent.data(), &target, nullptr));
        assert(!SetPublicDestination(agent.data(), target, nullptr));
        assert(!PublicWarp(agent.data(), target, nullptr));
        assert(SetPublicDestination(camera.data(), target, nullptr) && Near(navPosition.x, 1));
        endOnKill = false;
        nativeKill = true;
        Tick(machine.data(), nullptr);
        assert(Read<int>(ghost.data(), 0x30) == 2 && Near(Read<float>(hunt.data(), 0x88), 60));
        endOnKill = true;
        nativeKill = true;
        Tick(machine.data(), nullptr);
        assert(Read<int>(ghost.data(), 0x30) == 3);
        Tick(machine.data(), nullptr);
        assert(ticks == 102 && session.active && session.huntEnded);
        State(ghost.data(), 2);
        assert(Read<int>(ghost.data(), 0x30) == 3 && entries == 1);
        GhostControl::desired.store(false);
        GhostControl::Update(context);
        assert(!session.active && sourceEnabled && !cameraEnabled && pins == 0);
        Tick(machine.data(), nullptr);
        assert(ticks == 103);
    }
    void MovementAndBody() {
        Reset();
        assert(Write(hunt.data(), 0x8C, 1.0f));
        assert(Write(controller.data(), 0x74, 1.0f));
        Start();
        assert(Near(navPosition.z, 0));
        assert(Write(hunt.data(), 0x8C, -1.0f));
        ++frame; GhostControl::Update(context);
        assert(Near(navPosition.z, 0.04f) && Near(cameraTransform.position.z, -2.96f));
        GhostControl::Update(context);
        assert(Near(navPosition.z, 0.04f));
        assert(Write(controller.data(), 0x20, uint8_t{0}));
        assert(Write(controller.data(), 0x21, uint8_t{0}));
        assert(Write(mouse.data(), 0xB0, 90.0f));
        ++frame; GhostControl::Update(context);
        assert(Near(navPosition.z, 0.04f) && Near(session.yaw, 0));
        assert(Write(controller.data(), 0x20, uint8_t{1}));
        assert(Write(controller.data(), 0x21, uint8_t{1}));
        GhostControl::inputCaptured.store(true);
        ++frame; GhostControl::Update(context);
        assert(Near(navPosition.z, 0.04f));
        GhostControl::PlayerUpdate([](void* self, void*) {
            assert(!Read<uint8_t>(self, 0x20) && !Read<uint8_t>(self, 0x21));
            assert(!Read<uint8_t>(player.data(), 0x28));
        }, controller.data(), nullptr);
        assert(Read<uint8_t>(controller.data(), 0x20) == 1 && Read<uint8_t>(controller.data(), 0x21) == 1);
        assert(GhostControl::Restore() && exits == 1 && sourceEnabled && pins == 0);
        Reset(); Start();
        GhostControl::PlayerUpdate([](void*, void*) {
            assert(Write(player.data(), 0x28, uint8_t{1}));
        }, controller.data(), nullptr);
        assert(!Read<uint8_t>(controller.data(), 0x20));
        GhostControl::Update(context);
        assert(!session.active && Read<uint8_t>(player.data(), 0x28) == 1 && sourceEnabled);
        Reset(); Start();
        cameraRay.call = [](Vec3, Vec3, PhysicsHit* hit, int, int, void*) {
            hit->distance = 1;
            return true;
        };
        ++frame; GhostControl::Update(context);
        assert(Near(cameraTransform.position.z, -0.85f));
        GhostControl::firstPerson.store(true);
        ++frame; GhostControl::Update(context);
        assert(Near(cameraTransform.position.z, 0.25f));
        assert(GhostControl::Restore());
    }
    void AuthorityAndRestoration() {
        Reset(); context.authorized = false;
        GhostControl::desired.store(true); GhostControl::Update(context);
        assert(!session.active && !GhostControl::desired.load() && !entries && !copies);
        Reset(); Start();
        context.authorized = false;
        GhostControl::Update(context);
        assert(!session.active && !exits && sourceEnabled && pins == 0);
        Reset(); Start();
        ++context.generation;
        GhostControl::Update(context);
        assert(!session.active && !exits && pins == 0);
        Reset(); Start();
        failEnable = true;
        assert(!GhostControl::Restore() && session.stopping && pins == 4);
        failEnable = false;
        assert(GhostControl::Restore() && pins == 0 && sourceEnabled && !session.active);
        Reset(); Start();
        assert(Write(hunt.data(), 0x88, -1.0f));
        Tick(machine.data(), nullptr);
        assert(Read<int>(ghost.data(), 0x30) == 3);
        assert(GhostControl::Restore());
        Reset(); Start();
        SetEnabled(source.data(), true, nullptr);
        assert(!sourceEnabled);
        SetEnabled(source.data(), false, nullptr);
        assert(GhostControl::Restore() && !sourceEnabled);
        Reset();
        objectNew = [](void*) -> void* { return nullptr; };
        GhostControl::desired.store(true); GhostControl::Update(context);
        assert(!session.active && !GhostControl::desired.load() && !entries && !exits && !pins && sourceEnabled);
        Reset();
        void* protectedController = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        assert(protectedController);
        memcpy(protectedController, controller.data(), controller.size());
        context.controller = protectedController;
        Start();
        DWORD oldProtection = 0;
        assert(VirtualProtect(protectedController, 4096, PAGE_READONLY, &oldProtection));
        GhostControl::PlayerUpdate([](void*, void*) { assert(false); }, protectedController, nullptr);
        assert(pendingPlayerRestore.changed && !GhostControl::Restore());
        assert(VirtualProtect(protectedController, 4096, PAGE_READWRITE, &oldProtection));
        assert(GhostControl::Restore() && !pendingPlayerRestore.changed && sourceEnabled && !pins);
        assert(VirtualFree(protectedController, 0, MEM_RELEASE));
    }
    void PassiveControl() {
        Reset();
        rejectHunt = true;
        assert(Write(controller.data(), 0x74, 1.0f));
        Start(false);
        assert(!entries && Read<int>(ghost.data(), 0x30) == 0 && Near(navPosition.z, 0.04f));
        Tick(machine.data(), nullptr);
        State(ghost.data(), 1);
        State(ghost.data(), 2);
        assert(!ticks && Read<int>(ghost.data(), 0x30) == 0);
        GhostControl::huntRequested.store(true);
        ++frame; GhostControl::Update(context);
        assert(session.active && !entries && !GhostControl::huntRequested.load());
        rejectHunt = false;
        GhostControl::huntRequested.store(true);
        ++frame; GhostControl::Update(context);
        assert(entries == 1 && Read<int>(ghost.data(), 0x30) == 2);
        nativeKill = true;
        Tick(machine.data(), nullptr);
        Tick(machine.data(), nullptr);
        assert(session.active && session.huntEnded && Read<int>(ghost.data(), 0x30) == 3);
        GhostControl::huntRequested.store(true);
        ++frame; GhostControl::Update(context);
        assert(entries == 2 && !session.huntEnded);
        assert(GhostControl::Restore() && Read<int>(ghost.data(), 0x30) == 0);
        assert(SetPublicDestination(agent.data(), {5, 0, 5}, nullptr) && Near(navPosition.z, 5));
        Reset();
        assert(Write(ghost.data(), 0x30, 2));
        Start(false);
        assert(!entries && Read<int>(ghost.data(), 0x30) == 2);
        assert(GhostControl::Restore());
    }
    void Math() {
        auto straight = GhostControl::Step(0, 1, 0, 2, 0.05f);
        auto diagonal = GhostControl::Step(1, 1, 0, 2, 0.05f);
        assert(Near(straight.z, 0.1f) && Near(std::hypot(diagonal.x, diagonal.z), 0.1f));
        auto east = GhostControl::Step(0, 1, 1.57079632679f, 2, 0.05f);
        assert(Near(east.x, 0.1f) && Near(east.z, 0));
        assert(Near(GhostControl::Step(0, 1, 0, 2, 10).z, 0.2f));
        assert(Near(GhostControl::Step(0, 1, 0, 2, -1).z, 0));
        assert(Near(GhostControl::Step(std::numeric_limits<float>::quiet_NaN(), 1, 0, 2, 1).z, 0));
    }
}

GhostControl::Context Hooks::ReadHostContext() { return Test::context; }
void Hooks::hkGhostAI_ChangeState(void* ghost, int state, void*, bool force, void*) {
    if (state == 2) assert(force);
    Test::State(ghost, state);
}

int main() {
    Test::Math();
    Test::Lifecycle();
    Test::PassiveControl();
    Test::MovementAndBody();
    Test::AuthorityAndRestoration();
    puts("Ghost-control regressions passed.");
}
