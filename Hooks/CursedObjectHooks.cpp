#include "Hooks.h"
#include "../Features/Features.h"
#include <stdio.h>
#include <cstdint>

namespace Hooks {
     void hkCursedAwake(void* instance, void* methodInfo) {
          // 1. Let the original Awake run so the list gets populated
          oCursedAwake(instance, methodInfo);

          if (instance) {
               printf("[DEBUG] CursedItemsController::Awake triggered at %p\n", instance);

               // We are looking for the List<Enum> ?????????
               // Based on typical IL2CPP patterns, it's likely between offset 0x20 and 0xB0
               for (int offset = 0x10; offset < 0x150; offset += 8) {
                    void* potentialList = *(void**)((uintptr_t)instance + offset);

                    if (potentialList && !IsBadReadPtr(potentialList, 8)) {
                         // In IL2CPP, List<T> has an internal array at offset 0x10 and size at 0x18
                         void* internalArray = *(void**)((uintptr_t)potentialList + 0x10);
                         int size = *(int*)((uintptr_t)potentialList + 0x18);

                         if (size > 0 && size < 10) { // Cursed items list is usually small
                              printf("  -> Potential List found at offset 0x%X (Size: %d)\n", offset, size);

                              // If we find a list with size 1, let's try to force it to 1 (Tarot)
                              // *(int*)((uintptr_t)internalArray + 0x20) = 1; 
                         }
                    }
               }
          }
     }
}