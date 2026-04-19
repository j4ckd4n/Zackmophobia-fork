#include <windows.h>
#include <thread>
#include "SDK/IL2CPP.h"   // Look DOWN into SDK folder
#include "Hooks/Hooks.h" // Look DOWN into Hooks folder

void MainThread() {
     AllocConsole();
     FILE* f; freopen_s(&f, "CONOUT$", "w", stdout);

     // Wait for the game engine to fully load
     HMODULE gameAssembly = nullptr;
     while (!gameAssembly) {
          gameAssembly = GetModuleHandleW(L"GameAssembly.dll");
          Sleep(100);
     }

     // Initialize the SDK function pointers
     SDK::domain_get = (SDK::il2cpp_domain_get_t)GetProcAddress(gameAssembly, "il2cpp_domain_get");
     SDK::assembly_open = (SDK::il2cpp_domain_assembly_open_t)GetProcAddress(gameAssembly, "il2cpp_domain_assembly_open");
     SDK::assembly_get_image = (SDK::il2cpp_assembly_get_image_t)GetProcAddress(gameAssembly, "il2cpp_assembly_get_image");
     SDK::class_from_name = (SDK::il2cpp_class_from_name_t)GetProcAddress(gameAssembly, "il2cpp_class_from_name");
     SDK::get_method = (SDK::il2cpp_class_get_method_from_name_t)GetProcAddress(gameAssembly, "il2cpp_class_get_method_from_name");

     // Start the hooking process
     Hooks::Init();
}

BOOL APIENTRY DllMain(HMODULE hMod, DWORD reason, LPVOID res) {
     if (reason == DLL_PROCESS_ATTACH) {
          std::thread(MainThread).detach();
     }
     return TRUE;
}