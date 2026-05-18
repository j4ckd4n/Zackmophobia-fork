#include <windows.h>
#include <stdio.h>
#include <string>
#include <vector>

#include "../Hooks/detours.h"

#pragma comment(lib, "../Lib/detours.lib")

static std::wstring QuoteIfNeeded(const std::wstring& value)
{
     if (value.find(L' ') == std::wstring::npos) {
          return value;
     }

     return L"\"" + value + L"\"";
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

     const std::wstring exePath = argv[1];
     const std::wstring dllPath = argv[2];

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

     const std::string dllPathAnsi(dllPath.begin(), dllPath.end());

     BOOL ok = DetourCreateProcessWithDllExW(
          exePath.c_str(),
          mutableCmd.data(),
          nullptr,
          nullptr,
          FALSE,
          CREATE_DEFAULT_ERROR_MODE,
          nullptr,
          nullptr,
          &si,
          &pi,
          dllPathAnsi.c_str(),
          nullptr
     );

     if (!ok) {
          DWORD err = GetLastError();
          wprintf(L"[ERROR] DetourCreateProcessWithDllExW failed. GetLastError=%lu\n", err);
          return 4;
     }

     wprintf(L"[OK] Started process with injected DLL. PID=%lu\n", pi.dwProcessId);
     CloseHandle(pi.hThread);
     CloseHandle(pi.hProcess);
     return 0;
}
