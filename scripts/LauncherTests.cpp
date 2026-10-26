#include <windows.h>
#include <cassert>
#include <string>
#include <vector>

#if defined(LAUNCHER_FIXTURE_DLL)

BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }

#elif defined(LAUNCHER_FIXTURE_GAME)

int wmain()
{
    wchar_t path[MAX_PATH] = {}, stopName[128] = {}, unloadName[128] = {};
    if (!GetModuleFileNameW(nullptr, path, MAX_PATH) ||
        !GetEnvironmentVariableW(L"ZACKMOPHOBIA_TEST_STOP", stopName, 128) ||
        !GetEnvironmentVariableW(L"ZACKMOPHOBIA_TEST_UNLOAD", unloadName, 128)) return 1;
    const std::wstring directory = std::wstring(path).substr(0, std::wstring(path).find_last_of(L'\\') + 1);
    if (!LoadLibraryW((directory + L"GameAssembly.dll").c_str())) return 2;
    HANDLE events[]{OpenEventW(SYNCHRONIZE, FALSE, stopName), OpenEventW(SYNCHRONIZE, FALSE, unloadName)};
    if (!events[0] || !events[1]) return 3;
    for (;;) {
        const DWORD wait = WaitForMultipleObjects(2, events, FALSE, 60000);
        if (wait == WAIT_OBJECT_0) break;
        if (wait != WAIT_OBJECT_0 + 1) return 4;
        for (const wchar_t* name : {L"Zackmophobia_fixture.dll", L"Zackmophobia_second.dll"})
            if (HMODULE module = GetModuleHandleW(name)) FreeLibrary(module);
    }
    CloseHandle(events[0]);
    CloseHandle(events[1]);
    return 0;
}

#else

#define wmain LauncherEntry
#include "../Launcher/LaunchWithDll.cpp"
#undef wmain

namespace {
    HANDLE stopEvent = nullptr;
    HANDLE unloadEvent = nullptr;

    PROCESS_INFORMATION StartFixture(const std::wstring& game)
    {
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        std::wstring command = QuoteIfNeeded(game);
        std::vector<wchar_t> args(command.begin(), command.end());
        args.push_back(L'\0');
        assert(CreateProcessW(game.c_str(), args.data(), nullptr, nullptr, FALSE, 0,
            nullptr, nullptr, &startup, &process));
        assert(WaitForModule(process.hProcess, process.dwProcessId, L"GameAssembly.dll", 10000));
        return process;
    }

    void StopFixtures(std::initializer_list<PROCESS_INFORMATION> processes)
    {
        assert(SetEvent(stopEvent));
        for (auto process : processes) {
            assert(WaitForSingleObject(process.hProcess, 10000) == WAIT_OBJECT_0);
            if (process.hThread) CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
        }
        assert(ResetEvent(stopEvent));
    }

    int RunLauncher(const std::wstring& game, const std::wstring& dll)
    {
        wchar_t name[] = L"LaunchWithDll.exe", extra[] = L"--ignored-when-running";
        wchar_t* args[]{name, const_cast<wchar_t*>(game.c_str()), const_cast<wchar_t*>(dll.c_str()), extra};
        return LauncherEntry(4, args);
    }

    void AssertSameProcess(const std::wstring& game, DWORD expected)
    {
        DWORD found = 0;
        assert(FindRunningProcess(game, found) && found == expected);
    }
}

int wmain()
{
    wchar_t path[MAX_PATH] = {};
    assert(GetModuleFileNameW(nullptr, path, MAX_PATH));
    const std::wstring directory = std::wstring(path).substr(0, std::wstring(path).find_last_of(L'\\') + 1);
    const std::wstring game = directory + L"FixtureGame.exe";
    const std::wstring otherGame = directory + L"Other\\FixtureGame.exe";
    const std::wstring dll = directory + L"Zackmophobia_fixture.dll";
    const std::wstring secondDll = directory + L"Zackmophobia_second.dll";
    const std::wstring prefix = L"Local\\ZackmophobiaLauncherTests_" + std::to_wstring(GetCurrentProcessId());
    const std::wstring stopName = prefix + L"_stop", unloadName = prefix + L"_unload";
    stopEvent = CreateEventW(nullptr, TRUE, FALSE, stopName.c_str());
    unloadEvent = CreateEventW(nullptr, FALSE, FALSE, unloadName.c_str());
    assert(stopEvent && unloadEvent);
    assert(SetEnvironmentVariableW(L"ZACKMOPHOBIA_TEST_STOP", stopName.c_str()));
    assert(SetEnvironmentVariableW(L"ZACKMOPHOBIA_TEST_UNLOAD", unloadName.c_str()));

    DWORD found = 0;
    assert(FindRunningProcess(game, found) && found == 0);
    auto existing = StartFixture(game);
    assert(RunLauncher(game, dll) == 0);
    AssertSameProcess(game, existing.dwProcessId);
    assert(IsModuleLoaded(existing.dwProcessId, L"Zackmophobia_fixture.dll"));
    assert(RunLauncher(game, secondDll) == 8);
    AssertSameProcess(game, existing.dwProcessId);
    assert(!IsModuleLoaded(existing.dwProcessId, L"Zackmophobia_second.dll"));

    assert(SetEvent(unloadEvent));
    const ULONGLONG start = GetTickCount64();
    while (IsModuleLoaded(existing.dwProcessId, L"Zackmophobia_fixture.dll") &&
        GetTickCount64() - start < 5000) Sleep(10);
    assert(!IsModuleLoaded(existing.dwProcessId, L"Zackmophobia_fixture.dll"));
    assert(RunLauncher(game, secondDll) == 0);
    AssertSameProcess(game, existing.dwProcessId);
    assert(IsModuleLoaded(existing.dwProcessId, L"Zackmophobia_second.dll"));
    StopFixtures({existing});

    assert(RunLauncher(game, dll) == 0);
    assert(FindRunningProcess(game, found) && found != 0);
    PROCESS_INFORMATION launched{};
    launched.dwProcessId = found;
    launched.hProcess = OpenProcess(SYNCHRONIZE, FALSE, found);
    assert(launched.hProcess && IsModuleLoaded(found, L"Zackmophobia_fixture.dll"));
    StopFixtures({launched});

    auto first = StartFixture(game);
    auto second = StartFixture(game);
    assert(RunLauncher(game, dll) == 7);
    assert(!IsModuleLoaded(first.dwProcessId, L"Zackmophobia_fixture.dll"));
    assert(!IsModuleLoaded(second.dwProcessId, L"Zackmophobia_fixture.dll"));
    StopFixtures({first, second});

    auto unrelated = StartFixture(otherGame);
    assert(FindRunningProcess(game, found) && found == 0);
    assert(RunLauncher(game, dll) == 0);
    assert(FindRunningProcess(game, found) && found != 0 && found != unrelated.dwProcessId);
    launched = {};
    launched.dwProcessId = found;
    launched.hProcess = OpenProcess(SYNCHRONIZE, FALSE, found);
    assert(launched.hProcess);
    assert(IsModuleLoaded(found, L"Zackmophobia_fixture.dll"));
    assert(!IsModuleLoaded(unrelated.dwProcessId, L"Zackmophobia_fixture.dll"));
    StopFixtures({unrelated, launched});

    const std::wstring invalidDll = directory + L"InvalidFixture.dll";
    HANDLE invalid = CreateFileW(invalidDll.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    assert(invalid != INVALID_HANDLE_VALUE);
    DWORD written = 0;
    assert(WriteFile(invalid, "invalid", 7, &written, nullptr) && written == 7);
    CloseHandle(invalid);
    existing = StartFixture(game);
    assert(RunLauncher(game, invalidDll) == 6);
    AssertSameProcess(game, existing.dwProcessId);
    assert(RunLauncher(game, dll) == 0);
    StopFixtures({existing});
    assert(DeleteFileW(invalidDll.c_str()));
    CloseHandle(stopEvent);
    CloseHandle(unloadEvent);
    wprintf(L"[OK] Launcher runtime regressions passed using controlled fixtures.\n");
}

#endif
