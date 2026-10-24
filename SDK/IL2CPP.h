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
     using il2cpp_object_get_class_t = void* (*)(void* obj);
     using il2cpp_class_get_name_t = const char* (*)(void* klass);
     using il2cpp_class_get_field_from_name_t = void* (*)(void* klass, const char* name);
     using il2cpp_field_get_offset_t = size_t (*)(void* field);
     using il2cpp_method_get_param_t = const void* (*)(void* method, uint32_t index);
     using il2cpp_class_from_type_t = void* (*)(const void* type);
     using il2cpp_class_value_size_t = int32_t (*)(void* klass, uint32_t* alignment);
     using il2cpp_class_get_fields_t = void* (*)(void* klass, void** iterator);
     using il2cpp_field_get_type_t = const void* (*)(void* field);
     using il2cpp_class_get_type_t = const void* (*)(void* klass);
     using il2cpp_type_get_object_t = void* (*)(const void* type);

     // Global pointers to be filled in dllmain.cpp
     inline il2cpp_domain_get_t domain_get = nullptr;
     inline il2cpp_domain_assembly_open_t assembly_open = nullptr;
     inline il2cpp_assembly_get_image_t assembly_get_image = nullptr;
     inline il2cpp_class_from_name_t class_from_name = nullptr;
     inline il2cpp_class_get_method_from_name_t get_method = nullptr;
     inline il2cpp_object_get_class_t object_get_class = nullptr;
     inline il2cpp_class_get_name_t class_get_name = nullptr;
     inline il2cpp_class_get_field_from_name_t get_field = nullptr;
     inline il2cpp_field_get_offset_t field_get_offset = nullptr;
     inline il2cpp_method_get_param_t method_get_param = nullptr;
     inline il2cpp_class_from_type_t class_from_type = nullptr;
     inline il2cpp_class_value_size_t class_value_size = nullptr;
     inline il2cpp_class_get_fields_t class_get_fields = nullptr;
     inline il2cpp_field_get_type_t field_get_type = nullptr;
     inline il2cpp_class_get_type_t class_get_type = nullptr;
     inline il2cpp_type_get_object_t type_get_object = nullptr;

     // Helper to convert Unity's internal System.String to a standard C++ string
     inline std::string IL2CPP_To_String(void* ptr) 
     {
          if (!ptr || (uintptr_t)ptr < 0x10000 || IsBadReadPtr(ptr, 0x18)) return "";
          int32_t length = *(int32_t*)((char*)ptr + 0x10);
          if (length <= 0 || length > 512) return "";
          wchar_t* raw = (wchar_t*)((char*)ptr + 0x14);
          if (IsBadReadPtr(raw, length * 2)) return "";
          const int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, raw, length, nullptr, 0, nullptr, nullptr);
          if (bytes <= 0) return "";
          std::string result(bytes, '\0');
          if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, raw, length, result.data(), bytes, nullptr, nullptr) != bytes)
               return "";
          return result;
     }

     // Simple readable-check helper
     inline bool IsReadable(void* ptr, size_t size) {
          if (!ptr) return false;
          if ((uintptr_t)ptr < 0x10000) return false;
          return !IsBadReadPtr(ptr, (UINT)size);
     }

     // Safe read/write helpers
     template<typename T>
     inline bool ReadSafe(void* src, T &out) {
          if (!IsReadable(src, sizeof(T))) return false;
          memcpy(&out, src, sizeof(T));
          return true;
     }

     template<typename T>
     inline bool WriteSafe(void* dst, const T &val) {
          if (!IsReadable(dst, sizeof(T))) return false;
          memcpy(dst, &val, sizeof(T));
          return true;
     }

     // Return the managed 'fields' pointer from an object instance (common offset 0x10 on x64)
     inline void* GetFieldsPtr(void* instance, size_t fieldsOffset = 0x10) {
          if (!instance) return nullptr;
          void* possible = nullptr;
          if (!IsReadable((char*)instance + fieldsOffset, sizeof(void*))) return nullptr;
          possible = *(void**)((char*)instance + fieldsOffset);
          if (!IsReadable(possible, sizeof(void*))) return nullptr;
          return possible;
     }

     template<typename T>
     inline T* GetFields(void* instance, size_t fieldsOffset = 0x10) {
          void* p = GetFieldsPtr(instance, fieldsOffset);
          return p ? reinterpret_cast<T*>(p) : nullptr;
     }
}