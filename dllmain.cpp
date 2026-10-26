#include <windows.h>
#include <thread>
#include <atomic>
#include "SDK/IL2CPP.h"   
#include "Hooks/Hooks.h" 
#include "Menu/Menu.h"
#include "Utils/Logger.h"

namespace {
     HMODULE gSelfModule = nullptr;
     std::atomic<bool> gUnloadInProgress = false;
}

void MainThread() 
{
     AllocConsole(); // Allocate console
     FILE* f; freopen_s(&f, "CONOUT$", "w", stdout); // Initialize Console
     Logger::Init();
     Logger::Log("[SYSTEM] Main thread started.");

     // Wait for the game engine to fully load
     HMODULE gameAssembly = nullptr;
     while (!gameAssembly) {
          gameAssembly = GetModuleHandleW(L"GameAssembly.dll");
          Sleep(100);
     }
     Logger::Log("[SYSTEM] GameAssembly.dll detected.");

     // Initialize the SDK function pointers
     SDK::domain_get = (SDK::il2cpp_domain_get_t)GetProcAddress(gameAssembly, "il2cpp_domain_get");
     SDK::assembly_open = (SDK::il2cpp_domain_assembly_open_t)GetProcAddress(gameAssembly, "il2cpp_domain_assembly_open");
     SDK::assembly_get_image = (SDK::il2cpp_assembly_get_image_t)GetProcAddress(gameAssembly, "il2cpp_assembly_get_image");
     SDK::class_from_name = (SDK::il2cpp_class_from_name_t)GetProcAddress(gameAssembly, "il2cpp_class_from_name");
     SDK::get_method = (SDK::il2cpp_class_get_method_from_name_t)GetProcAddress(gameAssembly, "il2cpp_class_get_method_from_name");
     SDK::object_get_class = (SDK::il2cpp_object_get_class_t)GetProcAddress(gameAssembly, "il2cpp_object_get_class");
     SDK::class_get_name = (SDK::il2cpp_class_get_name_t)GetProcAddress(gameAssembly, "il2cpp_class_get_name");
     SDK::get_field = (SDK::il2cpp_class_get_field_from_name_t)GetProcAddress(gameAssembly, "il2cpp_class_get_field_from_name");
     SDK::field_get_offset = (SDK::il2cpp_field_get_offset_t)GetProcAddress(gameAssembly, "il2cpp_field_get_offset");
     SDK::method_get_param = (SDK::il2cpp_method_get_param_t)GetProcAddress(gameAssembly, "il2cpp_method_get_param");
     SDK::class_from_type = (SDK::il2cpp_class_from_type_t)GetProcAddress(gameAssembly, "il2cpp_class_from_type");
     SDK::class_value_size = (SDK::il2cpp_class_value_size_t)GetProcAddress(gameAssembly, "il2cpp_class_value_size");
     SDK::class_get_fields = (SDK::il2cpp_class_get_fields_t)GetProcAddress(gameAssembly, "il2cpp_class_get_fields");
     SDK::field_get_type = (SDK::il2cpp_field_get_type_t)GetProcAddress(gameAssembly, "il2cpp_field_get_type");
     SDK::class_get_type = (SDK::il2cpp_class_get_type_t)GetProcAddress(gameAssembly, "il2cpp_class_get_type");
     SDK::type_get_object = (SDK::il2cpp_type_get_object_t)GetProcAddress(gameAssembly, "il2cpp_type_get_object");
     Logger::Log("[SYSTEM] SDK function pointers initialized.");

     const auto attach = reinterpret_cast<void* (*)(void*)>(GetProcAddress(gameAssembly, "il2cpp_thread_attach"));
     const auto detach = reinterpret_cast<void (*)(void*)>(GetProcAddress(gameAssembly, "il2cpp_thread_detach"));
     void* thread = attach && detach ? attach(SDK::domain_get()) : nullptr;
     if (!thread) {
          Logger::Log("[ERROR] Cannot attach initialization thread to IL2CPP; hooks not installed.");
          return;
     }
     Hooks::Init();
     detach(thread);

     // Launch interactive console menu
     Menu::Start();
}

void UnloadThread()
{
     // END hotkey: clean unload so the output DLL file is no longer locked.
     while (!gUnloadInProgress.load()) {
          if (GetAsyncKeyState(VK_END) & 1) {
               Logger::Log("[SYSTEM] Unload requested via END key.");
               if (!Hooks::PrepareWorldOptionsForUnload()) continue;
               gUnloadInProgress = true;

               if (!Hooks::Shutdown()) {
                    gUnloadInProgress = false;
                    continue;
               }
               Menu::Shutdown();

               FreeConsole();
               // MSVC's std::thread holds a DLL reference until its normal CRT exit.
               if (!FreeLibrary(gSelfModule))
                    Logger::Log("[ERROR] Could not release the injected DLL reference (%lu).", GetLastError());
               return;
          }
          Sleep(100);
     }
}

BOOL APIENTRY DllMain(HMODULE hMod, DWORD reason, LPVOID res) 
{
     if (reason == DLL_PROCESS_ATTACH) {
          DisableThreadLibraryCalls(hMod);
          gSelfModule = hMod;
          std::thread(MainThread).detach();
          std::thread(UnloadThread).detach();
     }
     return TRUE;
}