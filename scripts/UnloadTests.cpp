#include <windows.h>
#include <atomic>
#include <cassert>
#include <string>

struct UnloadState {
    std::atomic<bool> requested{false};
    std::atomic<bool> cleanupAllowed{false};
    std::atomic<bool> detachAllowed{false};
    std::atomic<unsigned> cleanupCalls{0};
    std::atomic<unsigned> detachCalls{0};
    std::atomic<unsigned> menuCalls{0};
};

#if defined(UNLOAD_FIXTURE_ENGINE)

extern "C" __declspec(dllexport) void* il2cpp_domain_get() { return reinterpret_cast<void*>(1); }
extern "C" __declspec(dllexport) void* il2cpp_thread_attach(void* domain) { return domain; }
extern "C" __declspec(dllexport) void il2cpp_thread_detach(void*) {}

#elif defined(UNLOAD_FIXTURE_HOOK)

namespace {
    std::atomic<UnloadState*> state{nullptr};
    std::atomic<bool> initialized{false};

    SHORT WINAPI FixtureKeyState(int key) {
        auto* current = state.load();
        return key == VK_END && current && current->requested.exchange(false) ? 1 : 0;
    }
}

#define GetAsyncKeyState FixtureKeyState
#include "../dllmain.cpp"
#undef GetAsyncKeyState

void Hooks::Init() {}
bool Hooks::PrepareWorldOptionsForUnload() {
    auto* current = state.load();
    const bool allowed = current->cleanupAllowed.load();
    ++current->cleanupCalls;
    return allowed;
}
bool Hooks::Shutdown() {
    auto* current = state.load();
    const bool allowed = current->detachAllowed.load();
    ++current->detachCalls;
    return allowed;
}
void Menu::Start() { initialized.store(true); }
void Menu::Shutdown() { ++state.load()->menuCalls; }

extern "C" __declspec(dllexport) void ConfigureUnloadTest(UnloadState* current) { state.store(current); }
extern "C" __declspec(dllexport) bool UnloadTestReady() { return initialized.load(); }

#else

template <typename Predicate>
void WaitFor(Predicate predicate) {
    const ULONGLONG deadline = GetTickCount64() + 5000;
    while (!predicate() && GetTickCount64() < deadline) Sleep(10);
    assert(predicate());
}

int wmain() {
    wchar_t path[MAX_PATH]{};
    assert(GetModuleFileNameW(nullptr, path, MAX_PATH));
    const std::wstring directory = std::wstring(path).substr(0, std::wstring(path).find_last_of(L'\\') + 1);
    const std::wstring hookPath = directory + L"HookUnloadFixture.dll";
    HMODULE engine = LoadLibraryW((directory + L"GameAssembly.dll").c_str());
    assert(engine);

    for (int cycle = 0; cycle < 2; ++cycle) {
        UnloadState current;
        HMODULE hook = LoadLibraryW(hookPath.c_str());
        assert(hook);
        auto configure = reinterpret_cast<void (*)(UnloadState*)>(GetProcAddress(hook, "ConfigureUnloadTest"));
        auto ready = reinterpret_cast<bool (*)()>(GetProcAddress(hook, "UnloadTestReady"));
        assert(configure && ready);
        configure(&current);
        WaitFor([&] { return ready(); });

        current.requested.store(true);
        WaitFor([&] { return current.cleanupCalls.load() == 1; });
        assert(current.detachCalls.load() == 0 && current.menuCalls.load() == 0);
        assert(GetModuleHandleW(hookPath.c_str()) == hook);

        current.cleanupAllowed.store(true);
        current.requested.store(true);
        WaitFor([&] { return current.detachCalls.load() == 1; });
        assert(current.menuCalls.load() == 0 && GetModuleHandleW(hookPath.c_str()) == hook);

        current.detachAllowed.store(true);
        current.requested.store(true);
        WaitFor([&] { return GetModuleHandleW(hookPath.c_str()) == nullptr; });
        assert(current.cleanupCalls.load() == 3 && current.detachCalls.load() == 2);
        assert(current.menuCalls.load() == 1);
    }

    assert(FreeLibrary(engine));
    OutputDebugStringA("[OK] Actual hook worker unloaded and reloaded twice; failed cleanup/detachment remained retryable.\n");
}

#endif
