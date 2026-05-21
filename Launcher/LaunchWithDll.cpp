#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
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

static bool WaitForModule(DWORD processId, const wchar_t* moduleName, DWORD timeoutMs)
{
     DWORD start = GetTickCount();
     while ((GetTickCount() - start) < timeoutMs) {
          if (IsModuleLoaded(processId, moduleName)) {
               return true;
          }
          Sleep(100);
     }
     return false;
}

static bool InjectDllWithLoadLibrary(HANDLE processHandle, const std::wstring& dllPath)
{
     const SIZE_T bytes = (dllPath.size() + 1) * sizeof(wchar_t);
     void* remoteMem = VirtualAllocEx(processHandle, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
     if (!remoteMem) {
          return false;
     }

     BOOL wrote = WriteProcessMemory(processHandle, remoteMem, dllPath.c_str(), bytes, nullptr);
     if (!wrote) {
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

     WaitForSingleObject(remoteThread, 30000);

     DWORD remoteLoadResult = 0;
     GetExitCodeThread(remoteThread, &remoteLoadResult);

     CloseHandle(remoteThread);
     VirtualFreeEx(processHandle, remoteMem, 0, MEM_RELEASE);

     return remoteLoadResult != 0;
}

int wmain(int argc, wchar_t** argv)
{
     if (argc < 3) {
          wprintf(
               L"Usage:\n"
               L"  LaunchWithDll.exe <Phasmophobia.exe path> <Zackmophobia.dll path> [extra args]\n\n"
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

     BOOL ok = CreateProcessW(
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

     if (!ok) {
          DWORD err = GetLastError();
          wprintf(L"[ERROR] CreateProcessW failed. GetLastError=%lu\n", err);
          return 4;
     }

     WaitForInputIdle(pi.hProcess, 30000);

     wprintf(L"[INFO] Waiting for GameAssembly.dll in target process...\n");
     if (!WaitForModule(pi.dwProcessId, L"GameAssembly.dll", 90000)) {
          wprintf(L"[ERROR] Timed out waiting for GameAssembly.dll.\n");
          CloseHandle(pi.hThread);
          CloseHandle(pi.hProcess);
          return 5;
     }

     wprintf(L"[INFO] Injecting DLL with LoadLibraryW...\n");
     if (!InjectDllWithLoadLibrary(pi.hProcess, dllPath)) {
          DWORD err = GetLastError();
          wprintf(L"[ERROR] Remote injection failed. GetLastError=%lu\n", err);
          CloseHandle(pi.hThread);
          CloseHandle(pi.hProcess);
          return 6;
     }

     wprintf(L"[OK] Started process and injected DLL after initialization. PID=%lu\n", pi.dwProcessId);
     CloseHandle(pi.hThread);
     CloseHandle(pi.hProcess);
     return 0;
}
