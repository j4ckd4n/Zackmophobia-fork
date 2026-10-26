#pragma once
#include <atomic>
#include <cstring>
#include "../SDK/IL2CPP.h"
#include "../Hooks/detours.h"
#include "../Utils/Logger.h"

namespace Reporting {
    using Report = void* (*)(void*, void*, void*);
    using ExceptionFactory = void* (*)(void*, const char*, const char*, const char*);
    using RaiseException = void (*)(void*);
    using Detection = void (*)(void*, void*);

    inline Report original = nullptr;
    inline Detection originalDetection = nullptr;
    inline Detection completeDetection = nullptr;
    inline void* completeDetectionInfo = nullptr;
    inline ExceptionFactory exceptionFactory = nullptr;
    inline RaiseException raiseException = nullptr;
    inline void* coreImage = nullptr;
    inline std::atomic<bool> installed{false};
    inline std::atomic<unsigned> blocked{0};
    inline std::atomic<unsigned> terminationsBlocked{0};

    inline bool HackerReason(void* reason) {
        int32_t length = 0;
        wchar_t shortPrefix[7]{};
        if (!reason || !SDK::ReadSafe(static_cast<char*>(reason) + 0x10, length) || length < 7 ||
            !SDK::ReadSafe(static_cast<char*>(reason) + 0x14, shortPrefix))
            return false;
        if (std::memcmp(shortPrefix, L"HACKER:", sizeof(shortPrefix)) == 0) return true;
        wchar_t longPrefix[16]{};
        return length >= 16 && SDK::ReadSafe(static_cast<char*>(reason) + 0x14, longPrefix) &&
            std::memcmp(longPrefix, L"HACKER DETECTED:", sizeof(longPrefix)) == 0;
    }

    inline void BlockDetectionTermination(void* stateMachine, void* method) {
        int32_t state = 0;
        void* reason = nullptr;
        if (!stateMachine || !SDK::ReadSafe(stateMachine, state) ||
            !SDK::ReadSafe(static_cast<char*>(stateMachine) + 0x20, reason)) {
            Logger::Log("[ERROR] Detection state unreadable; retaining original handler.");
            originalDetection(stateMachine, method);
            return;
        }
        if (!HackerReason(reason)) {
            originalDetection(stateMachine, method);
            return;
        }
        if (state == -2) return;
        if (state != -1 && state != 0) {
            Logger::Log("[ERROR] Unsupported detection coroutine state; retaining original handler.");
            originalDetection(stateMachine, method);
            return;
        }
        const int32_t completed = -2;
        if (!SDK::WriteSafe(stateMachine, completed)) {
            Logger::Log("[ERROR] Cannot complete detection state; retaining original handler.");
            originalDetection(stateMachine, method);
            return;
        }
        const unsigned count = terminationsBlocked.fetch_add(1) + 1;
        Logger::Log("[DETECTION] HACKER-specific termination suppressed (attempt %u); native action remains rejected.", count);
        // Finish the real async builder, including a previously suspended handler.
        completeDetection(static_cast<char*>(stateMachine) + 0x8, completeDetectionInfo);
    }

    inline bool FieldAt(void* klass, const char* name, size_t offset, void* type) {
        void* field = klass ? SDK::get_field(klass, name) : nullptr;
        return field && type && SDK::field_get_offset(field) == offset &&
            SDK::class_from_type(SDK::field_get_type(field)) == type;
    }

    inline bool InitializeDetection(void* image, HMODULE module) {
        const auto classCount = reinterpret_cast<size_t (*)(void*)>(
            GetProcAddress(module, "il2cpp_image_get_class_count"));
        const auto imageClass = reinterpret_cast<void* (*)(void*, size_t)>(
            GetProcAddress(module, "il2cpp_image_get_class"));
        const auto valueType = reinterpret_cast<bool (*)(void*)>(
            GetProcAddress(module, "il2cpp_class_is_valuetype"));
        if (!image || !classCount || !imageClass || !valueType || !SDK::get_field ||
            !SDK::field_get_offset || !SDK::field_get_type || !SDK::class_from_type ||
            !SDK::class_value_size) return false;
        void* builder = SDK::class_from_name(coreImage, "System.Runtime.CompilerServices", "AsyncTaskMethodBuilder");
        void* integer = SDK::class_from_name(coreImage, "System", "Int32");
        void* string = SDK::class_from_name(coreImage, "System", "String");
        void* complete = builder ? SDK::get_method(builder, "SetResult", 0) : nullptr;
        Detection setter = nullptr;
        uint32_t alignment = 0;
        const uintptr_t base = reinterpret_cast<uintptr_t>(module);
        if (!builder || !valueType(builder) || SDK::class_value_size(builder, &alignment) != 0x18 ||
            !SDK::ReadSafe(complete, setter) || reinterpret_cast<uintptr_t>(setter) != base + 0x40A5E20)
            return false;
        const size_t count = classCount(image);
        if (!count || count > 65536) return false;
        for (size_t index = 0; index < count; ++index) {
            void* klass = imageClass(image, index);
            if (!klass || !valueType(klass)) continue;
            void* method = SDK::get_method(klass, "MoveNext", 0);
            Detection candidate = nullptr;
            if (!SDK::ReadSafe(method, candidate) ||
                reinterpret_cast<uintptr_t>(candidate) != base + 0x24964E0) continue;
            // Reflection offsets include the object header; MoveNext receives the unboxed value.
            if (SDK::class_value_size(klass, &alignment) != 0x30 ||
                !FieldAt(klass, "<>1__state", 0x10, integer) ||
                !FieldAt(klass, "<>t__builder", 0x18, builder) ||
                !FieldAt(klass, "reason", 0x30, string)) return false;
            originalDetection = candidate;
            completeDetection = setter;
            completeDetectionInfo = complete;
            return true;
        }
        return false;
    }

    inline void* BlockSelfReport(void*, void*, void*) {
        const unsigned count = blocked.fetch_add(1) + 1;
        Logger::Log("[REPORTING] Automatic SelfPlayerReport blocked before dispatch (attempt %u).", count);
        void* exception = exceptionFactory(coreImage, "System", "InvalidOperationException",
            "Automatic SelfPlayerReport was blocked locally; no report was dispatched.");
        if (exception) {
            // Preserve failure semantics instead of fabricating a successful server response.
            raiseException(exception);
            Logger::Log("[ERROR] Managed exception API unexpectedly returned while blocking a report.");
        } else {
            Logger::Log("[ERROR] Could not allocate the blocked-report exception; returning no task.");
        }
        return nullptr;
    }

    inline bool Initialize(void* domain, void* image, HMODULE module) {
        original = nullptr;
        originalDetection = completeDetection = nullptr;
        completeDetectionInfo = nullptr;
        if (!domain || !module || !SDK::assembly_open || !SDK::assembly_get_image ||
            !SDK::class_from_name || !SDK::get_method) return false;
        exceptionFactory = reinterpret_cast<ExceptionFactory>(
            GetProcAddress(module, "il2cpp_exception_from_name_msg"));
        raiseException = reinterpret_cast<RaiseException>(
            GetProcAddress(module, "il2cpp_raise_exception"));
        void* core = SDK::assembly_open(domain, "mscorlib");
        coreImage = core ? SDK::assembly_get_image(core) : nullptr;
        void* assembly = SDK::assembly_open(domain, "Unity.Services.CloudCode.GeneratedBindings");
        void* bindingsImage = assembly ? SDK::assembly_get_image(assembly) : nullptr;
        void* klass = bindingsImage ? SDK::class_from_name(bindingsImage, "Unity.Services.CloudCode.GeneratedBindings",
            "ModerationModuleBindings") : nullptr;
        void* method = klass ? SDK::get_method(klass, "SelfPlayerReport", 1) : nullptr;
        Report candidate = nullptr;
        if (!exceptionFactory || !raiseException || !coreImage ||
            !SDK::class_from_name(coreImage, "System", "InvalidOperationException") ||
            !SDK::ReadSafe(method, candidate) ||
            reinterpret_cast<uintptr_t>(candidate) != reinterpret_cast<uintptr_t>(module) + 0x4E93EB0 ||
            !InitializeDetection(image, module))
            return false;
        original = candidate;
        return true;
    }

    inline LONG Install(bool attach) {
        if (!attach && !installed.load()) return NO_ERROR;
        if (!original || !originalDetection || !completeDetection || !completeDetectionInfo ||
            !exceptionFactory || !raiseException || !coreImage)
            return ERROR_INVALID_FUNCTION;
        const LONG reportError = attach ? DetourAttach(reinterpret_cast<PVOID*>(&original), reinterpret_cast<PVOID>(BlockSelfReport)) :
            DetourDetach(reinterpret_cast<PVOID*>(&original), reinterpret_cast<PVOID>(BlockSelfReport));
        if (reportError != NO_ERROR) return reportError;
        return attach ? DetourAttach(reinterpret_cast<PVOID*>(&originalDetection), reinterpret_cast<PVOID>(BlockDetectionTermination)) :
            DetourDetach(reinterpret_cast<PVOID*>(&originalDetection), reinterpret_cast<PVOID>(BlockDetectionTermination));
    }
}
