#include <cassert>
#include <cstring>
#include <algorithm>
#include "../Features/Reporting.h"

namespace {
    struct ManagedFailure {};
    int exceptionObject = 0;
    int coreObject = 0;
    int bindingsObject = 0;
    int classObject = 0;
    int gameImageObject = 0, stateClassObject = 0, builderClassObject = 0;
    int integerClassObject = 0, stringClassObject = 0, completedTaskObject = 0;
    unsigned factories = 0, raises = 0, forwards = 0;
    unsigned detectionForwards = 0;
    void* lastDetectionMethod = nullptr;
    bool allocationFails = false, missingAssembly = false, missingMethod = false;
    bool missingStateClass = false;
    Reporting::Report methodCode = nullptr;
    Reporting::Detection detectionCode = nullptr, completionCode = nullptr;
    struct Field {
        size_t offset;
        void* type;
    } stateField{0x10, &integerClassObject}, builderField{0x18, &builderClassObject},
        reasonField{0x30, &stringClassObject};
    struct Builder {
        void* task = nullptr;
        void* context = nullptr;
        unsigned completions = 0;
        unsigned padding = 0;
    };
    struct State {
        int32_t state = -1;
        uint32_t padding = 0;
        Builder builder;
        void* reason = nullptr;
        void* awaiter = nullptr;
    };
    struct ManagedReason {
        char header[0x10]{};
        int32_t length = 0;
        wchar_t characters[1024]{};
        explicit ManagedReason(const std::wstring& text) {
            assert(text.size() < 1024);
            length = static_cast<int32_t>(text.size());
            std::copy(text.begin(), text.end(), characters);
        }
    };
    static_assert(sizeof(State) == 0x30 && offsetof(State, builder) == 0x8);
    static_assert(offsetof(State, reason) == 0x20 && offsetof(ManagedReason, characters) == 0x14);

    __declspec(noinline) void* ForwardReport(void*, void*, void*) {
        ++forwards;
        return &classObject;
    }

    __declspec(noinline) void ForwardDetection(void*, void* method) {
        ++detectionForwards;
        lastDetectionMethod = method;
    }

    void CompleteDetection(void* builder, void* method) {
        assert(method == &completionCode);
        auto* state = reinterpret_cast<State*>(static_cast<char*>(builder) - 0x8);
        assert(state->state == -2);
        state->builder.task = &completedTaskObject;
        ++state->builder.completions;
    }
}

extern "C" __declspec(dllexport) size_t il2cpp_image_get_class_count(void* image) {
    assert(image == &gameImageObject);
    return missingStateClass ? 0 : 2;
}

extern "C" __declspec(dllexport) void* il2cpp_image_get_class(void* image, size_t index) {
    assert(image == &gameImageObject && index < 2);
    return index == 0 ? &classObject : &stateClassObject;
}

extern "C" __declspec(dllexport) bool il2cpp_class_is_valuetype(void* klass) {
    return klass == &stateClassObject || klass == &builderClassObject;
}

extern "C" __declspec(dllexport) void* il2cpp_exception_from_name_msg(
    void* image, const char* space, const char* type, const char* message) {
    assert(image == &coreObject && std::strcmp(space, "System") == 0);
    assert(std::strcmp(type, "InvalidOperationException") == 0);
    assert(std::strstr(message, "no report was dispatched"));
    ++factories;
    return allocationFails ? nullptr : &exceptionObject;
}

extern "C" __declspec(dllexport) void il2cpp_raise_exception(void* exception) noexcept(false) {
    assert(exception == &exceptionObject);
    ++raises;
    throw ManagedFailure{};
}

int main() {
    HMODULE module = GetModuleHandleW(nullptr);
    SDK::assembly_open = [](void*, const char* name) -> void* {
        if (std::strcmp(name, "mscorlib") == 0) return &coreObject;
        assert(std::strcmp(name, "Unity.Services.CloudCode.GeneratedBindings") == 0);
        return missingAssembly ? nullptr : &bindingsObject;
    };
    SDK::assembly_get_image = [](void* assembly) { return assembly; };
    SDK::class_from_name = [](void* image, const char* space, const char* name) -> void* {
        if (image == &coreObject) {
            if (std::strcmp(space, "System.Runtime.CompilerServices") == 0) {
                assert(std::strcmp(name, "AsyncTaskMethodBuilder") == 0);
                return &builderClassObject;
            }
            assert(std::strcmp(space, "System") == 0);
            if (std::strcmp(name, "Int32") == 0) return &integerClassObject;
            if (std::strcmp(name, "String") == 0) return &stringClassObject;
            assert(std::strcmp(name, "InvalidOperationException") == 0);
        } else {
            assert(image == &bindingsObject);
            assert(std::strcmp(space, "Unity.Services.CloudCode.GeneratedBindings") == 0);
            assert(std::strcmp(name, "ModerationModuleBindings") == 0);
        }
        return &classObject;
    };
    SDK::get_method = [](void* klass, const char* name, int args) -> void* {
        if (klass == &stateClassObject) {
            assert(std::strcmp(name, "MoveNext") == 0 && args == 0);
            return &detectionCode;
        }
        if (klass == &builderClassObject) {
            assert(std::strcmp(name, "SetResult") == 0 && args == 0);
            return &completionCode;
        }
        assert(klass == &classObject && std::strcmp(name, "SelfPlayerReport") == 0 && args == 1);
        return missingMethod ? nullptr : &methodCode;
    };
    SDK::get_field = [](void* klass, const char* name) -> void* {
        assert(klass == &stateClassObject);
        if (std::strcmp(name, "<>1__state") == 0) return &stateField;
        if (std::strcmp(name, "<>t__builder") == 0) return &builderField;
        assert(std::strcmp(name, "reason") == 0);
        return &reasonField;
    };
    SDK::field_get_offset = [](void* field) { return static_cast<Field*>(field)->offset; };
    SDK::field_get_type = [](void* field) -> const void* { return static_cast<Field*>(field)->type; };
    SDK::class_from_type = [](const void* type) -> void* {
        if (type == &integerClassObject) return &integerClassObject;
        if (type == &builderClassObject) return &builderClassObject;
        if (type == &stringClassObject) return &stringClassObject;
        return nullptr;
    };
    SDK::class_value_size = [](void* klass, uint32_t* alignment) -> int32_t {
        *alignment = 8;
        assert(klass == &stateClassObject || klass == &builderClassObject);
        return klass == &stateClassObject ? 0x30 : 0x18;
    };
    methodCode = reinterpret_cast<Reporting::Report>(reinterpret_cast<uintptr_t>(module) + 0x4E93EB0);
    detectionCode = reinterpret_cast<Reporting::Detection>(reinterpret_cast<uintptr_t>(module) + 0x24964E0);
    completionCode = reinterpret_cast<Reporting::Detection>(reinterpret_cast<uintptr_t>(module) + 0x40A5E20);
    assert(Reporting::Initialize(&classObject, &gameImageObject, module));
    Reporting::original = ForwardReport;
    Reporting::originalDetection = ForwardDetection;
    Reporting::completeDetection = CompleteDetection;
    assert(DetourTransactionBegin() == NO_ERROR);
    assert(DetourUpdateThread(GetCurrentThread()) == NO_ERROR);
    assert(Reporting::Install(true) == NO_ERROR);
    assert(DetourTransactionCommit() == NO_ERROR);
    Reporting::installed = true;
    volatile Reporting::Report entry = ForwardReport;
    for (int i = 0; i < 2; ++i) {
        bool failed = false;
        try {
            entry(&bindingsObject, nullptr, nullptr);
        } catch (const ManagedFailure&) {
            failed = true;
        }
        assert(failed && !forwards);
    }
    assert(Reporting::blocked.load() == 2 && factories == 2 && raises == 2);

    allocationFails = true;
    assert(entry(nullptr, nullptr, nullptr) == nullptr);
    assert(!forwards && factories == 3 && raises == 2);
    assert(Logger::gLogLines.back().find("[ERROR]") != std::string::npos);

    volatile Reporting::Detection detectionEntry = ForwardDetection;
    for (const auto* text : {L"HACKER: fixture", L"HACKER DETECTED: fixture"}) {
        for (int32_t initial : {-1, 0}) {
            ManagedReason reason(text);
            State state;
            state.state = initial;
            state.reason = &reason;
            state.awaiter = initial == 0 ? &classObject : nullptr;
            detectionEntry(&state, &classObject);
            assert(state.state == -2 && state.builder.completions == 1);
            assert(state.builder.task == &completedTaskObject && !detectionForwards);
            const unsigned completed = Reporting::terminationsBlocked.load();
            detectionEntry(&state, &classObject);
            assert(state.builder.completions == 1 && Reporting::terminationsBlocked.load() == completed);
        }
    }
    ManagedReason longReason(std::wstring(L"HACKER DETECTED: ") + std::wstring(700, L'x'));
    State longState;
    longState.reason = &longReason;
    detectionEntry(&longState, &classObject);
    assert(longState.builder.completions == 1 && !detectionForwards);
    assert(Reporting::terminationsBlocked.load() == 5);

    for (const auto* text : {L"Unrelated fatal error", L"prefix HACKER: fixture",
        L"HACKER", L"HACKERISH: fixture", L""}) {
        ManagedReason reason(text);
        State state;
        state.reason = &reason;
        detectionEntry(&state, &classObject);
        assert(state.state == -1 && !state.builder.completions);
        assert(lastDetectionMethod == &classObject);
    }
    assert(detectionForwards == 5);
    State invalid;
    invalid.state = 1;
    invalid.reason = &longReason;
    detectionEntry(&invalid, &classObject);
    detectionEntry(nullptr, &classObject);
    assert(detectionForwards == 7 && !invalid.builder.completions);

    assert(Reporting::Install(false) != NO_ERROR && Reporting::installed.load());
    assert(DetourTransactionBegin() == NO_ERROR);
    assert(DetourUpdateThread(GetCurrentThread()) == NO_ERROR);
    assert(Reporting::Install(false) == NO_ERROR);
    assert(DetourTransactionCommit() == NO_ERROR);
    Reporting::installed = false;
    assert(entry(nullptr, nullptr, nullptr) == &classObject && forwards == 1);
    detectionEntry(&longState, &classObject);
    assert(detectionForwards == 8 && longState.builder.completions == 1);
    assert(Reporting::Install(false) == NO_ERROR);

    missingAssembly = true;
    assert(!Reporting::Initialize(&classObject, &gameImageObject, module));
    assert(Reporting::Install(true) == ERROR_INVALID_FUNCTION);
    missingAssembly = false;
    missingMethod = true;
    assert(!Reporting::Initialize(&classObject, &gameImageObject, module));
    missingMethod = false;
    methodCode = ForwardReport;
    assert(!Reporting::Initialize(&classObject, &gameImageObject, module));
    assert(!Reporting::original && Reporting::Install(true) == ERROR_INVALID_FUNCTION);
    methodCode = reinterpret_cast<Reporting::Report>(reinterpret_cast<uintptr_t>(module) + 0x4E93EB0);
    missingStateClass = true;
    assert(!Reporting::Initialize(&classObject, &gameImageObject, module));
    missingStateClass = false;
    reasonField.offset += 4;
    assert(!Reporting::Initialize(&classObject, &gameImageObject, module));
    reasonField.offset -= 4;
    completionCode = CompleteDetection;
    assert(!Reporting::Initialize(&classObject, &gameImageObject, module));
    assert(Reporting::Install(true) == ERROR_INVALID_FUNCTION);
    assert(!Reporting::Initialize(&classObject, &gameImageObject, nullptr));
    puts("[OK] Report blocking, scoped HACKER completion, unrelated forwarding, binding guards and detach restoration passed.");
}
