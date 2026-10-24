#include <cassert>
#include <cstring>
#include <cwchar>
#include <limits>
#include "../Hooks/Hooks.h"
#include "../Features/Features.h"
#include "../Features/GhostControl.h"

void Hooks::UpdateRadar() {}
void Hooks::ResetRadar() {}
bool Hooks::BeginGhostTransition(void*) { return true; }
void Hooks::EndGhostTransition() {}
bool GhostControl::AllowTransition(void*, int) { return true; }

namespace {
    struct ManagedName {
        void* klass = nullptr;
        void* monitor = nullptr;
        int32_t length = 0;
        wchar_t characters[64] = {};
    };

    Hooks::GhostInfo_o info{};
    Hooks::GhostAI_o ghost{};
    ManagedName name;
    uintptr_t transform[4] = {};
    Hooks::Vector3 position{12.0f, 3.0f, -7.0f};
    bool transformAvailable = true;
    int updateCalls = 0;
    int initCalls = 0;
    int destroyCalls = 0;
    int transformCalls = 0;
    int transformMethod = 0;
    int positionMethod = 0;

    void SetName(const wchar_t* value) {
        wcscpy_s(name.characters, value);
        name.length = static_cast<int32_t>(wcslen(value));
    }

    void Update(void* instance, void*) {
        assert(instance == &info);
        ++updateCalls;
        info.fields.data.fields.string = &name;
    }

    void Init(void* instance, Hooks::GhostData_o* data, void*) {
        assert(instance == &ghost && data == nullptr);
        ++initCalls;
    }

    void Destroy(void* instance, void*) {
        assert(instance == &ghost);
        ++destroyCalls;
    }

    void ChangeState(void* instance, int32_t state, void*, bool, void*) {
        assert(instance == &ghost);
        ghost.fields.state = state;
    }

    void* GetTransform(void* instance, void* method) {
        assert(instance == &ghost && method == &transformMethod);
        ++transformCalls;
        return transformAvailable ? transform : nullptr;
    }

    void GetPosition(void* instance, Hooks::Vector3* result, void* method) {
        assert(instance == transform && method == &positionMethod);
        *result = position;
    }

    Features::GhostDisplay Snapshot() {
        std::lock_guard<std::mutex> lock(Features::ghostDisplayMutex);
        return Features::ghostDisplay;
    }

    void SampleType(int32_t id, const char* expected) {
        info.fields.data.fields.ghostTypeId2 = id;
        Hooks::hkGhostUpdate(&info, nullptr);
        const auto display = Snapshot();
        assert(display.typeId == id);
        assert(display.type && std::strcmp(display.type, expected) == 0);
    }
}

int main() {
    Hooks::gGhostInfoLayoutValid = true;
    Hooks::oGhostUpdate = Update;
    Hooks::oGhostAI_Init = Init;
    Hooks::oGhostAI_Destroy = Destroy;
    Hooks::oGhostAI_ChangeState = ChangeState;
    Hooks::oGetTransform = GetTransform;
    Hooks::oGetPositionInjected = GetPosition;
    Hooks::getTransformMethodInfo = &transformMethod;
    Hooks::getPositionInjectedMethodInfo = &positionMethod;
    info.fields.ghost = &ghost;
    ghost.fields.ghostInfo = &info;
    SetName(L"Test Ghost");

    // The original update supplies the name; unrelated integers must not choose the type.
    info.fields.data.fields.ghostTypeId1 = 9;
    SampleType(0, "Spirit");
    assert(updateCalls == 1);
    auto display = Snapshot();
    assert(display.name == "Test Ghost");
    assert(display.positionValid && display.position[0] == 12.0f &&
        display.position[1] == 3.0f && display.position[2] == -7.0f);

    SampleType(28, "Kormos");
    SampleType(29, "Aswang");
    SampleType(30, "Deildegast");
    for (int id : {-1, 31, 1000000}) {
        info.fields.data.fields.ghostTypeId2 = id;
        Hooks::hkGhostUpdate(&info, nullptr);
        assert(Snapshot().type == nullptr);
        assert(Snapshot().typeId == id);
    }

    // Both the published name and a renderer's snapshot own their string storage.
    SetName(L"Another Ghost");
    assert(Snapshot().name == "Test Ghost");
    Hooks::hkGhostUpdate(&info, nullptr);
    assert(Snapshot().name == "Another Ghost" && display.name == "Test Ghost");

    SetName(L"Ren\u00e9 \U0001f47b");
    Hooks::hkGhostUpdate(&info, nullptr);
    assert(Snapshot().name == "Ren\xc3\xa9 \xf0\x9f\x91\xbb");

    transformAvailable = false;
    Hooks::hkGhostUpdate(&info, nullptr);
    assert(!Snapshot().positionValid && Snapshot().positionSource == nullptr);
    transformAvailable = true;
    position.x = std::numeric_limits<float>::quiet_NaN();
    Hooks::hkGhostUpdate(&info, nullptr);
    assert(!Snapshot().positionValid);
    position.x = 42.0f;
    Hooks::hkGhostUpdate(&info, nullptr);
    assert(Snapshot().positionValid && Snapshot().position[0] == 42.0f);

    Hooks::hkGhostAI_ChangeState(&ghost, 2, nullptr, false, nullptr);
    assert(std::strcmp(Snapshot().historicalStates[0], "Hunting") == 0);
    Hooks::hkGhostAI_Destroy(&ghost, nullptr);
    assert(destroyCalls == 1 && Hooks::gCurrentGhostAI == nullptr);
    assert(Snapshot().name.empty() && Snapshot().typeId == -1 && !Snapshot().positionValid);

    // Late injection can recover without observing Init.
    SampleType(30, "Deildegast");
    assert(Hooks::gCurrentGhostAI == &ghost);
    Hooks::hkGhostAI_Init(&ghost, nullptr, nullptr);
    assert(initCalls == 1 && Snapshot().name.empty());
    SampleType(0, "Spirit");
    assert(Snapshot().historicalStates[0] == nullptr);
    ghost.fields.ghostInfo = nullptr;
    Hooks::hkGhostUpdate(&info, nullptr);
    assert(Snapshot().name.empty() && Hooks::gCurrentGhostAI == nullptr);

    const int previousUpdates = updateCalls;
    const int previousTransforms = transformCalls;
    Hooks::gGhostInfoLayoutValid = false;
    Hooks::hkGhostUpdate(&info, nullptr);
    assert(updateCalls == previousUpdates + 1 && transformCalls == previousTransforms);
}
