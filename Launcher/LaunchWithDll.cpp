#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <iterator>
#include <string>
#include <vector>

static std::wstring QuoteIfNeeded(const std::wstring& value)
{
     if (value.find(L' ') == std::wstring::npos) {
          return value;
     }

     return L"\"" + value + L"\"";
}

static std::wstring ToAbsolutePath(const std::wstring& path)
{
     wchar_t buffer[MAX_PATH] = {};
     DWORD len = GetFullPathNameW(path.c_str(), MAX_PATH, buffer, nullptr);
     if (len == 0 || len >= MAX_PATH) {
          return path;
     }
     return std::wstring(buffer);
}

static bool FindRunningProcess(const std::wstring& exePath, DWORD& processId)
{
     processId = 0;
     const size_t slash = exePath.find_last_of(L"\\/");
     const std::wstring exeName = exePath.substr(slash == std::wstring::npos ? 0 : slash + 1);
     HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
     if (snapshot == INVALID_HANDLE_VALUE) {
          wprintf(L"[ERROR] Could not enumerate running processes. GetLastError=%lu\n", GetLastError());
          return false;
     }

     PROCESSENTRY32W entry{};
     entry.dwSize = sizeof(entry);
     if (!Process32FirstW(snapshot, &entry)) {
          wprintf(L"[ERROR] Could not read the process list. GetLastError=%lu\n", GetLastError());
          CloseHandle(snapshot);
          return false;
     }
     bool success = true;
     do {
          if (_wcsicmp(entry.szExeFile, exeName.c_str()) != 0) continue;
          HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
          wchar_t path[32768] = {};
          DWORD length = static_cast<DWORD>(std::size(path));
          const bool queried = process && QueryFullProcessImageNameW(process, 0, path, &length);
          const DWORD error = queried ? ERROR_SUCCESS : GetLastError();
          if (process) CloseHandle(process);
          if (!queried) {
               // Do not launch a duplicate when a same-name process cannot be identified.
               if (error == ERROR_INVALID_PARAMETER) continue; // Process exited during enumeration.
               wprintf(L"[ERROR] Could not identify candidate PID=%lu. GetLastError=%lu\n",
                    entry.th32ProcessID, error);
               success = false;
               break;
          }
          if (_wcsicmp(path, exePath.c_str()) != 0) continue;
          if (processId != 0) {
               wprintf(L"[ERROR] Multiple matching game processes are running; close extra instances first.\n");
               success = false;
               break;
          }
          processId = entry.th32ProcessID;
     } while (Process32NextW(snapshot, &entry));
     if (success && GetLastError() != ERROR_NO_MORE_FILES) {
          wprintf(L"[ERROR] Process enumeration failed. GetLastError=%lu\n", GetLastError());
          success = false;
     }
     CloseHandle(snapshot);
     return success;
}

static bool CheckNoInjectedHook(DWORD processId, const std::wstring& dllPath)
{
     HANDLE snapshot = INVALID_HANDLE_VALUE;
     for (int attempt = 0; attempt < 10; ++attempt) {
          snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, processId);
          if (snapshot != INVALID_HANDLE_VALUE || GetLastError() != ERROR_BAD_LENGTH) break;
          Sleep(10);
     }
     if (snapshot == INVALID_HANDLE_VALUE) {
          wprintf(L"[ERROR] Could not inspect target DLLs. GetLastError=%lu\n", GetLastError());
          return false;
     }
     const size_t slash = dllPath.find_last_of(L"\\/");
     const std::wstring dllName = dllPath.substr(slash == std::wstring::npos ? 0 : slash + 1);
     const std::wstring shadowPrefix = L"Zackmophobia_";
     MODULEENTRY32W entry{};
     entry.dwSize = sizeof(entry);
     bool success = Module32FirstW(snapshot, &entry) != FALSE;
     if (!success) wprintf(L"[ERROR] Could not read target DLLs. GetLastError=%lu\n", GetLastError());
     while (success) {
          const std::wstring name = entry.szModule;
          const bool shadow = name.size() > shadowPrefix.size() + 4 &&
               _wcsnicmp(name.c_str(), shadowPrefix.c_str(), shadowPrefix.size()) == 0 &&
               _wcsicmp(name.c_str() + name.size() - 4, L".dll") == 0;
          if (_wcsicmp(name.c_str(), dllName.c_str()) == 0 ||
               _wcsicmp(name.c_str(), L"Zackmophobia.dll") == 0 || shadow) {
               wprintf(L"[ERROR] A hook DLL is still loaded (%s). Press END in-game and wait for unload before reinjecting.\n",
                    name.c_str());
               success = false;
               break;
          }
          if (!Module32NextW(snapshot, &entry)) {
               if (GetLastError() != ERROR_NO_MORE_FILES) {
                    wprintf(L"[ERROR] Target DLL enumeration failed. GetLastError=%lu\n", GetLastError());
                    success = false;
               }
               break;
          }
     }
     CloseHandle(snapshot);
     return success;
}

static bool IsModuleLoaded(DWORD processId, const wchar_t* moduleName)
{
     HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, processId);
     if (snapshot == INVALID_HANDLE_VALUE) {
          return false;
     }

     MODULEENTRY32W me{};
     me.dwSize = sizeof(me);
     bool found = false;

     if (Module32FirstW(snapshot, &me)) {
          do {
               if (_wcsicmp(me.szModule, moduleName) == 0) {
                    found = true;
                    break;
               }
          } while (Module32NextW(snapshot, &me));
     }

     CloseHandle(snapshot);
     return found;
}

static bool WaitForModule(HANDLE process, DWORD processId, const wchar_t* moduleName, DWORD timeoutMs)
{
     DWORD start = GetTickCount();
     while ((GetTickCount() - start) < timeoutMs) {
          const DWORD state = WaitForSingleObject(process, 0);
          if (state == WAIT_OBJECT_0) {
               wprintf(L"[ERROR] Target process exited before initialization.\n");
               return false;
          }
          if (state == WAIT_FAILED) {
               wprintf(L"[ERROR] Could not check target process state. GetLastError=%lu\n", GetLastError());
               return false;
          }
          if (IsModuleLoaded(processId, moduleName)) {
               return true;
          }
          Sleep(100);
     }
     wprintf(L"[ERROR] Timed out waiting for %s.\n", moduleName);
     return false;
}

static bool InjectDllWithLoadLibrary(HANDLE processHandle, const std::wstring& dllPath)
{
     const SIZE_T bytes = (dllPath.size() + 1) * sizeof(wchar_t);
     void* remoteMem = VirtualAllocEx(processHandle, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
     if (!remoteMem) {
          return false;
     }

     SIZE_T written = 0;
     BOOL wrote = WriteProcessMemory(processHandle, remoteMem, dllPath.c_str(), bytes, &written);
     if (!wrote || written != bytes) {
          VirtualFreeEx(processHandle, remoteMem, 0, MEM_RELEASE);
          return false;
     }

     HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
     if (!k32) {
          VirtualFreeEx(processHandle, remoteMem, 0, MEM_RELEASE);
          return false;
     }

     FARPROC loadLibraryW = GetProcAddress(k32, "LoadLibraryW");
     if (!loadLibraryW) {
          VirtualFreeEx(processHandle, remoteMem, 0, MEM_RELEASE);
          return false;
     }

     HANDLE remoteThread = CreateRemoteThread(
          processHandle,
          nullptr,
          0,
          reinterpret_cast<LPTHREAD_START_ROUTINE>(loadLibraryW),
          remoteMem,
          0,
          nullptr
     );

     if (!remoteThread) {
          VirtualFreeEx(processHandle, remoteMem, 0, MEM_RELEASE);
          return false;
     }

     const DWORD wait = WaitForSingleObject(remoteThread, 30000);
     if (wait != WAIT_OBJECT_0) {
          const DWORD error = wait == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError();
          wprintf(L"[ERROR] Remote loader did not finish. Its argument allocation is retained because the thread may still use it.\n");
          CloseHandle(remoteThread);
          SetLastError(error);
          return false;
     }

     DWORD remoteLoadResult = 0;
     const BOOL resultRead = GetExitCodeThread(remoteThread, &remoteLoadResult);
     const DWORD error = resultRead ? ERROR_DLL_INIT_FAILED : GetLastError();

     CloseHandle(remoteThread);
     VirtualFreeEx(processHandle, remoteMem, 0, MEM_RELEASE);

     if (!resultRead || remoteLoadResult == 0) {
          SetLastError(error);
          return false;
     }
     return true;
}

int wmain(int argc, wchar_t** argv)
{
     if (argc < 3) {
          wprintf(
               L"Usage:\n"
               L"  LaunchWithDll.exe <Phasmophobia.exe path> <Zackmophobia.dll path> [extra args]\n\n"
               L"Reuses an existing game with the same executable path. Unload old hooks with END first.\n\n"
               L"Example:\n"
               L"  LaunchWithDll.exe \"C:\\Program Files (x86)\\Steam\\steamapps\\common\\Phasmophobia\\Phasmophobia.exe\" \"D:\\git\\Zackmophobia\\x64\\Debug\\Zackmophobia.dll\"\n"
          );
          return 1;
     }

     const std::wstring exePath = ToAbsolutePath(argv[1]);
     const std::wstring dllPath = ToAbsolutePath(argv[2]);

     DWORD exeAttrib = GetFileAttributesW(exePath.c_str());
     if (exeAttrib == INVALID_FILE_ATTRIBUTES) {
          wprintf(L"[ERROR] Could not find executable: %s\n", exePath.c_str());
          return 2;
     }

     DWORD dllAttrib = GetFileAttributesW(dllPath.c_str());
     if (dllAttrib == INVALID_FILE_ATTRIBUTES) {
          wprintf(L"[ERROR] Could not find DLL: %s\n", dllPath.c_str());
          return 3;
     }

     DWORD runningId = 0;
     if (!FindRunningProcess(exePath, runningId)) return 7;

     std::wstring cmd = QuoteIfNeeded(exePath);
     for (int i = 3; i < argc; ++i) {
          cmd += L" ";
          cmd += QuoteIfNeeded(argv[i]);
     }

     STARTUPINFOW si{};
     PROCESS_INFORMATION pi{};
     si.cb = sizeof(si);

     std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
     mutableCmd.push_back(L'\0');

     BOOL ok = FALSE;
     if (runningId != 0) {
          pi.hProcess = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION |
               PROCESS_VM_WRITE | PROCESS_VM_READ | SYNCHRONIZE, FALSE, runningId);
          wchar_t actualPath[32768] = {};
          DWORD length = static_cast<DWORD>(std::size(actualPath));
          if (!pi.hProcess || !QueryFullProcessImageNameW(pi.hProcess, 0, actualPath, &length)) {
               wprintf(L"[ERROR] Could not open the running game. PID=%lu GetLastError=%lu\n", runningId, GetLastError());
               if (pi.hProcess) CloseHandle(pi.hProcess);
               return 4;
          }
          if (_wcsicmp(actualPath, exePath.c_str()) != 0) {
               wprintf(L"[ERROR] Target identity changed; injection cancelled.\n");
               CloseHandle(pi.hProcess);
               return 4;
          }
          pi.dwProcessId = runningId;
          ok = TRUE;
          wprintf(L"[INFO] Reusing running game. PID=%lu\n", runningId);
          if (argc > 3) wprintf(L"[INFO] Extra launch arguments are ignored for a running game.\n");
     } else {
          ok = CreateProcessW(
               exePath.c_str(),
               mutableCmd.data(),
               nullptr,
               nullptr,
               FALSE,
               CREATE_DEFAULT_ERROR_MODE,
               nullptr,
               nullptr,
               &si,
               &pi
          );
     }

     if (!ok) {
          DWORD err = GetLastError();
          wprintf(L"[ERROR] CreateProcessW failed. GetLastError=%lu\n", err);
          return 4;
     }

     if (pi.hThread) WaitForInputIdle(pi.hProcess, 30000);

     wprintf(L"[INFO] Waiting for GameAssembly.dll in target process...\n");
     if (!WaitForModule(pi.hProcess, pi.dwProcessId, L"GameAssembly.dll", 90000)) {
          if (pi.hThread) CloseHandle(pi.hThread);
          CloseHandle(pi.hProcess);
          return 5;
     }

     if (!CheckNoInjectedHook(pi.dwProcessId, dllPath)) {
          if (pi.hThread) CloseHandle(pi.hThread);
          CloseHandle(pi.hProcess);
          return 8;
     }

     wprintf(L"[INFO] Injecting DLL with LoadLibraryW...\n");
     if (!InjectDllWithLoadLibrary(pi.hProcess, dllPath)) {
          DWORD err = GetLastError();
          wprintf(L"[ERROR] Remote injection failed. GetLastError=%lu\n", err);
          if (pi.hThread) CloseHandle(pi.hThread);
          CloseHandle(pi.hProcess);
          return 6;
     }

     wprintf(L"[OK] %s and injected DLL after initialization. PID=%lu\n",
          runningId ? L"Reused running process" : L"Started process", pi.dwProcessId);
     if (pi.hThread) CloseHandle(pi.hThread);
     CloseHandle(pi.hProcess);
     return 0;
}
