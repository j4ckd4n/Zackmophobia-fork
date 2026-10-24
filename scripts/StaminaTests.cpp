#include "../Features/Stamina.h"
#include "../SDK/IL2CPP.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <thread>
#include <vector>

namespace {
    struct Class;
    struct FieldInfo {
        size_t offset;
        Class* type;
    };
    struct Class {
        const char* name;
        std::vector<FieldInfo> fields;
    };
    Class boolClass{"Boolean"}, single{"Single"}, stamina{"PlayerStamina"};
    Class localClass{"LocalPlayer"}, playerClass{"Player"}, viewClass{"PhotonView"};
    Class pcClass{"PCStamina"}, vrClass{"VRStamina"}, objectClass{"Object"};
    Class* classes[]{&boolClass, &single, &stamina, &localClass, &playerClass,
        &viewClass, &pcClass, &vrClass, &objectClass};

    struct Object {
        alignas(void*) unsigned char bytes[0x180]{};

        template<class T>
        void Set(size_t offset, T value) {
            std::memcpy(bytes + offset, &value, sizeof value);
        }

        template<class T>
        T Get(size_t offset) const {
            T value{};
            std::memcpy(&value, bytes + offset, sizeof value);
            return value;
        }

        void Init(Class* klass) {
            std::memset(bytes, 0, sizeof bytes);
            Set(0, klass);
            Set<uintptr_t>(0x10, 1);
        }
    };
    Object component, local, player, view;
    int image, domain, updateInfo;
    int calls = 0, exhaustionEvents = 0;
    float dt = 0.5f;
    bool sprintRequested = true, ownership = true, simulate = true;
    bool observedDrain = true, destroyInUpdate = false, changeOwnerInUpdate = false;
    bool nativeEnablesDrain = false;
    int sprintMode = 1; // Native mode 0 disabled, 1 finite, 2 unlimited.

    bool Alive(void* self, void*) {
        return self && static_cast<Object*>(self)->Get<uintptr_t>(0x10) != 0;
    }

    bool Mine(void* self, void*) {
        assert(self == &view);
        return ownership;
    }

    struct MethodInfo { void* pointer; };
    MethodInfo aliveMethod{reinterpret_cast<void*>(Alive)};
    MethodInfo mineMethod{reinterpret_cast<void*>(Mine)};

    void* Domain() { return &domain; }
    void* Assembly(void*, const char*) { return &image; }
    void* Image(void* assembly) { return assembly; }
    void* FromName(void*, const char*, const char* name) {
        for (Class* klass : classes)
            if (std::strcmp(klass->name, name) == 0) return klass;
        return nullptr;
    }
    void* GetMethod(void* klass, const char* name, int arguments) {
        if (klass == &objectClass && std::strcmp(name, "op_Implicit") == 0 && arguments == 1)
            return &aliveMethod;
        if (klass == &viewClass && std::strcmp(name, "get_IsMine") == 0 && arguments == 0)
            return &mineMethod;
        return nullptr;
    }
    void* GetClass(void* self) { return static_cast<Object*>(self)->Get<Class*>(0); }
    void* Fields(void* klass, void** iterator) {
        auto& fields = static_cast<Class*>(klass)->fields;
        size_t index = reinterpret_cast<size_t>(*iterator);
        if (index == fields.size()) return nullptr;
        *iterator = reinterpret_cast<void*>(index + 1);
        return &fields[index];
    }
    size_t Offset(void* field) { return static_cast<FieldInfo*>(field)->offset; }
    const void* FieldType(void* field) { return static_cast<FieldInfo*>(field)->type; }
    void* FromType(const void* type) { return const_cast<void*>(type); }

    // Mirrors the recovered order: native concrete state calculation, base
    // regeneration, then gated/clamped drain. It is not a replacement game loop.
    void Original(void* self, void* info) {
        assert(info == &updateInfo);
        ++calls;
        if (!simulate) return;
        assert(self == &component);
        observedDrain = component.Get<bool>(0x35);
        if (destroyInUpdate) {
            component.Set<uintptr_t>(0x10, 0);
            return;
        }
        if (changeOwnerInUpdate) ownership = false;
        if (nativeEnablesDrain) component.Set(0x35, true);
        const bool dead = local.Get<bool>(0x38);
        bool exhausted = component.Get<bool>(0x38);
        const bool sprinting = sprintRequested && !dead && !exhausted && sprintMode != 0;
        component.Set(0x32, sprinting);
        component.Set(0x34, sprinting);
        float remaining = component.Get<float>(0x40);
        if (exhausted) {
            float recovery = component.Get<float>(0x3C);
            if (recovery >= 5) {
                remaining = 3;
                component.Set(0x3C, 0.0f);
                component.Set(0x38, false);
            } else {
                component.Set(0x3C, recovery + dt);
                remaining += dt * 0.6f;
            }
        } else if (!sprinting) {
            remaining = remaining >= 3 ? 3 : remaining + dt;
        }
        if (sprinting && sprintMode != 2 && component.Get<bool>(0x35)) {
            remaining = std::clamp(remaining - dt, 0.0f, 3.0f);
            if (remaining == 0) {
                component.Set(0x38, true);
                ++exhaustionEvents;
            }
        }
        component.Set(0x40, remaining);
    }

    void Tick() { Stamina::Update(Original, &component, &updateInfo); }

    void Setup() {
        component.Init(&pcClass);
        local.Init(&localClass);
        player.Init(&playerClass);
        view.Init(&viewClass);
        component.Set(0x20, &local);
        component.Set(0x31, true);
        component.Set(0x35, true);
        component.Set(0x40, 3.0f);
        component.Set<uintptr_t>(0x50, 0x12345678);
        local.Set(0x20, &player);
        local.Set(0x78, &view);
        local.Set(0xA0, &component);
        player.Set(0x110, &local);
        dt = 0.5f;
        calls = exhaustionEvents = 0;
        sprintRequested = ownership = simulate = true;
        destroyInUpdate = changeOwnerInUpdate = nativeEnablesDrain = false;
        sprintMode = 1;
        assert(Stamina::Initialize(&image));
    }

    void ExpectUnavailable() {
        const int previous = calls;
        Tick();
        assert(calls == previous + 1);
        assert(!Stamina::Read().available);
    }
}

int main() {
    SDK::domain_get = Domain;
    SDK::assembly_open = Assembly;
    SDK::assembly_get_image = Image;
    SDK::class_from_name = FromName;
    SDK::get_method = GetMethod;
    SDK::object_get_class = GetClass;
    SDK::class_get_fields = Fields;
    SDK::field_get_offset = Offset;
    SDK::field_get_type = FieldType;
    SDK::class_from_type = FromType;
    stamina.fields = {{0x20, &localClass}, {0x31, &boolClass}, {0x35, &boolClass},
        {0x38, &boolClass}, {0x40, &single}};
    localClass.fields = {{0x20, &playerClass}, {0x38, &boolClass}, {0x78, &viewClass},
        {0xA0, &stamina}};
    playerClass.fields = {{0x110, &localClass}};

    Setup();
    Tick();
    auto sample = Stamina::Read();
    assert(sample.available && sample.current == 2.5f);
    assert(std::fabs(sample.normalized - 2.5f / 3.0f) < 0.00001f);
    for (int i = 0; i < 5; ++i) Tick();
    sample = Stamina::Read();
    assert(sample.current == 0 && sample.exhausted && exhaustionEvents == 1);
    assert(sample.sampledAt <= GetTickCount64());
    assert(component.Get<uintptr_t>(0x50) == 0x12345678);

    Setup();
    component.Set(0x40, 0.25f);
    Stamina::infinite.store(true);
    // A long frame would immediately exhaust even a guessed "refill to 3".
    dt = 4;
    for (int i = 0; i < 20; ++i) {
        Tick();
        assert(!observedDrain && component.Get<bool>(0x35));
        assert(component.Get<float>(0x40) == 0.25f);
        assert(component.Get<bool>(0x32) && component.Get<bool>(0x34));
        assert(component.Get<uintptr_t>(0x50) == 0x12345678);
    }
    assert(Stamina::Read().infiniteActive && exhaustionEvents == 0);
    Stamina::infinite.store(false);
    dt = 0.125f;
    Tick();
    assert(observedDrain && component.Get<float>(0x40) == 0.125f);
    assert(!Stamina::Read().infiniteActive);
    Tick();
    assert(Stamina::Read().exhausted && exhaustionEvents == 1);

    Setup();
    Stamina::infinite.store(true);
    component.Set(0x35, false); // Already protected by the native timed buff.
    Tick();
    assert(!component.Get<bool>(0x35) && component.Get<float>(0x40) == 3);
    Stamina::infinite.store(false);
    Tick();
    assert(!component.Get<bool>(0x35) && component.Get<float>(0x40) == 3);
    nativeEnablesDrain = true; // Do not undo a native change we did not make.
    Tick();
    assert(component.Get<bool>(0x35) && component.Get<float>(0x40) == 2.5f);

    Setup();
    component.Set(0x40, 0.0f);
    component.Set(0x38, true);
    Stamina::infinite.store(true);
    Tick();
    assert(Stamina::Read().exhausted);
    assert(component.Get<float>(0x3C) == dt);
    for (int i = 0; i < 10; ++i) Tick();
    assert(!Stamina::Read().exhausted && Stamina::Read().current == 3);

    Setup();
    Stamina::infinite.store(true);
    sprintMode = 0;
    component.Set(0x31, false);
    Tick();
    assert(observedDrain && !Stamina::Read().sprintEnabled &&
        !Stamina::Read().infiniteActive && !component.Get<bool>(0x32));
    sprintMode = 2; // Native unlimited mode remains untouched with feature off.
    component.Set(0x31, true);
    Stamina::infinite.store(false);
    Tick();
    assert(component.Get<float>(0x40) == 3);

    Setup();
    component.Set(0, &vrClass);
    Tick();
    assert(Stamina::Read().current == 2.5f);
    Stamina::infinite.store(true);
    Tick();
    assert(Stamina::Read().current == 2.5f && component.Get<bool>(0x35));
    assert(component.Get<bool>(0x32) && component.Get<bool>(0x34));

    Setup();
    sprintRequested = false;
    component.Set(0x40, 2.9f);
    Tick(); // Native can overshoot; display is bounded without changing memory.
    assert(component.Get<float>(0x40) > 3 && Stamina::Read().current == 3);

    Setup();
    Stamina::infinite.store(true);
    local.Set(0x38, true);
    Tick();
    assert(Stamina::Read().available && !Stamina::Read().sprintEnabled && !Stamina::Read().infiniteActive);
    assert(observedDrain && component.Get<bool>(0x35));
    Setup();
    Stamina::infinite.store(true);
    ownership = false;
    Tick();
    assert(Stamina::Read().available && Stamina::Read().infiniteActive && !observedDrain);

    Setup();
    simulate = false;
    Stamina::infinite.store(true);
    view.Set<uintptr_t>(0x10, 0);
    Tick();
    assert(Stamina::Read().available);
    view.Set<uintptr_t>(0x10, 1);
    player.Set<uintptr_t>(0x10, 0);
    Tick();
    assert(Stamina::Read().available);
    player.Set<uintptr_t>(0x10, 1);
    local.Set<uintptr_t>(0x10, 0);
    ExpectUnavailable();
    local.Set<uintptr_t>(0x10, 1);
    local.Set<Object*>(0xA0, nullptr);
    ExpectUnavailable();
    local.Set(0xA0, &component);
    player.Set<Object*>(0x110, nullptr);
    Tick();
    assert(Stamina::Read().available);
    player.Set(0x110, &local);
    component.Set(0, &playerClass);
    ExpectUnavailable();
    component.Set(0, &pcClass);
    component.Set<uintptr_t>(0x10, 0);
    ExpectUnavailable();
    component.Set<uintptr_t>(0x10, 1);
    for (float invalid : {std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(), -1.0f}) {
        component.Set(0x40, invalid);
        ExpectUnavailable();
        assert(component.Get<bool>(0x35));
    }
    Stamina::Update(Original, nullptr, &updateInfo);
    assert(!Stamina::Read().available);
    Stamina::Update(Original, reinterpret_cast<void*>(1), &updateInfo);
    assert(!Stamina::Read().available);
    Stamina::Update(nullptr, &component, &updateInfo);
    assert(!Stamina::Read().available);

    Setup();
    Stamina::infinite.store(true);
    destroyInUpdate = true;
    ExpectUnavailable();
    assert(!component.Get<bool>(0x35)); // No write to a destroyed component.
    Setup();
    Stamina::infinite.store(true);
    changeOwnerInUpdate = true;
    Tick();
    assert(Stamina::Read().available);
    assert(component.Get<bool>(0x35)); // Our scoped change still gets restored.

    Setup();
    stamina.fields.back().offset = 0x50; // Old, incorrect field must fail closed.
    assert(!Stamina::Initialize(&image));
    Stamina::infinite.store(true);
    Tick();
    assert(observedDrain && !Stamina::Read().available);
    stamina.fields.back().offset = 0x40;
    mineMethod.pointer = nullptr;
    assert(Stamina::Initialize(&image)); // Local stamina does not depend on Photon.
    mineMethod.pointer = reinterpret_cast<void*>(Mine);
    assert(!Stamina::Initialize(nullptr));
    SDK::class_get_fields = nullptr;
    assert(!Stamina::Initialize(&image));
    SDK::class_get_fields = Fields;

    Setup();
    local.Set<Object*>(0x20, nullptr);
    local.Set<Object*>(0x78, nullptr);
    player.Set<Object*>(0x110, nullptr);
    Stamina::infinite.store(true);
    Stamina::Observe(&local); // Lobby sampling works without a networked avatar/update.
    assert(Stamina::Read().available && Stamina::Read().current == 3);
    assert(!Stamina::Read().infiniteActive && component.Get<bool>(0x35) && calls == 0);
    Tick();
    Stamina::Observe(&local);
    assert(Stamina::Read().infiniteActive);
    Stamina::infinite.store(false);
    Stamina::Observe(&local);
    assert(!Stamina::Read().infiniteActive);
    Object unrelated;
    unrelated.Init(&playerClass);
    simulate = false;
    Stamina::Update(Original, &unrelated, &updateInfo);
    assert(Stamina::Read().available); // Unrelated invalid callbacks must not blank the bar.
    local.Set<Object*>(0xA0, nullptr);
    Stamina::Observe(&local);
    assert(!Stamina::Read().available);

    Setup();
    std::atomic_bool stop{false};
    std::thread reader([&] {
        while (!stop.load()) {
            const auto value = Stamina::Read();
            assert(!value.available || (value.current >= 0 && value.current <= value.maximum &&
                value.normalized >= 0 && value.normalized <= 1));
            Stamina::infinite.store(true);
        }
    });
    for (int i = 0; i < 1000; ++i) Tick();
    stop.store(true);
    reader.join();
    Sleep(1510);
    assert(!Stamina::Read().available);
    Stamina::Reset();
    assert(!Stamina::Read().available && !Stamina::infinite.load());
    std::cout << "StaminaTests passed\n";
}
