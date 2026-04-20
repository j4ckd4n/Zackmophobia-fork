#pragma once
#include <windows.h>
#include <string>

namespace SDK {
     // Function signatures for internal Unity (IL2CPP) exports
     using il2cpp_domain_get_t = void* (*)();
     using il2cpp_domain_assembly_open_t = void* (*)(void* domain, const char* name);
     using il2cpp_assembly_get_image_t = void* (*)(void* assembly);
     using il2cpp_class_from_name_t = void* (*)(void* image, const char* namespaze, const char* name);
     using il2cpp_class_get_method_from_name_t = void* (*)(void* klass, const char* name, int argsCount);

     // Global pointers to be filled in dllmain.cpp
     inline il2cpp_domain_get_t domain_get = nullptr;
     inline il2cpp_domain_assembly_open_t assembly_open = nullptr;
     inline il2cpp_assembly_get_image_t assembly_get_image = nullptr;
     inline il2cpp_class_from_name_t class_from_name = nullptr;
     inline il2cpp_class_get_method_from_name_t get_method = nullptr;

     // Helper to convert Unity's internal System.String to a standard C++ string
     inline std::string IL2CPP_To_String(void* ptr) 
     {
          if (!ptr || (uintptr_t)ptr < 0x10000 || IsBadReadPtr(ptr, 0x18)) return "";
          int32_t length = *(int32_t*)((char*)ptr + 0x10);
          if (length <= 0 || length > 32) return "";
          wchar_t* raw = (wchar_t*)((char*)ptr + 0x14);
          if (IsBadReadPtr(raw, length * 2)) return "";
          std::wstring ws(raw, length);
          return std::string(ws.begin(), ws.end());
     }
}