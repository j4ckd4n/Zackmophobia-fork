#include <windows.h>
#include <cmath>
#include "Hooks.h"
#include "../SDK/IL2CPP.h"
#include "../Features/Features.h"
#include "../Utils/Logger.h"

namespace Hooks {
     namespace {
          inline bool IsFinite3(const Vector3& v) {
               return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
          }

          // Reject obviously bogus vectors to avoid logging random memory as world position.
          inline bool LooksLikeWorldPos(const Vector3& v) {
               if (!IsFinite3(v)) return false;
               const float MAX_REASONABLE = 100000.0f;
               return (fabsf(v.x) < MAX_REASONABLE && fabsf(v.y) < MAX_REASONABLE && fabsf(v.z) < MAX_REASONABLE);
          }

          bool TryReadVec3At(void* base, size_t offset, Vector3& out) {
               if (!base) return false;
               return SDK::ReadSafe((char*)base + offset, out) && LooksLikeWorldPos(out);
          }

          bool TryGetFPCPosition(void* fpcInstance, Vector3& out, const char*& source) {
               source = "none";
               if (!SDK::IsReadable(fpcInstance, 0x20)) return false;

               // Path 1: call Unity API when signatures are valid.
               if (oGetTransform && oGetPosition) {
                    __try {
                         void* transform = oGetTransform(fpcInstance, nullptr);
                         if (SDK::IsReadable(transform, 0x20)) {
                              // On x64 IL2CPP, Vector3 can be returned through an explicit out pointer in many builds.
                              Hooks::Vector3 pos{};
                              oGetPosition(&pos, transform, nullptr);
                              if (LooksLikeWorldPos(pos)) {
                                   out = pos;
                                   source = "unity:get_position";
                                   return true;
                              }
                         }
                    }
                    __except (EXCEPTION_EXECUTE_HANDLER) {
                         // Signature/layout mismatches are common across Unity versions.
                    }
               }

               // Path 2: robust memory fallback from FPC fields -> physics controller.
               // Known candidate layout in this project: fields + 0x78 = physics controller object.
               void* fpcFields = SDK::GetFieldsPtr(fpcInstance);
               if (SDK::IsReadable(fpcFields, 0x80)) {
                    void* physicsObj = nullptr;
                    if (SDK::ReadSafe((char*)fpcFields + 0x78, physicsObj) && SDK::IsReadable(physicsObj, 0x20)) {
                         void* physicsFields = SDK::GetFieldsPtr(physicsObj);
                         if (physicsFields) {
                              if (TryReadVec3At(physicsFields, 0xAC, out)) { source = "physics:fields+0xAC"; return true; }
                              if (TryReadVec3At(physicsFields, 0xB8, out)) { source = "physics:fields+0xB8"; return true; }
                         }

                         // Some builds store directly inside object storage rather than fields.
                         if (TryReadVec3At(physicsObj, 0xAC, out)) { source = "physics:obj+0xAC"; return true; }
                         if (TryReadVec3At(physicsObj, 0xB8, out)) { source = "physics:obj+0xB8"; return true; }
                    }
               }

               return false;
          }
     }

     void hkStaminaUpdate(void* instance, void* methodInfo) 
     {
          static bool loggedStamina = false;
          if (!loggedStamina) {
               Logger::Log("[HOOK] PlayerStamina.Update is executing.");
               loggedStamina = true;
          }

          if (Features::bStaminaEnabled && instance && !IsBadReadPtr(instance, 0x100)) 
          {
               float* stamina = (float*)((char*)instance + 0x50);
               if (!IsBadReadPtr(stamina, 4)) *stamina = 3.0f;
          }
          oStaminaUpdate(instance, methodInfo); // Call original game code
     }

     void hkFPCUpdate(void* instance, void* methodInfo) {
          static bool loggedSpeed = false;
          if (!loggedSpeed) {
               Logger::Log("[HOOK] FirstPersonController.Update is executing.");
               loggedSpeed = true;
          }

          static int sampleTick = 0;
          if ((++sampleTick % 60) == 0) {
               Hooks::Vector3 pos{};
               const char* source = nullptr;
               if (TryGetFPCPosition(instance, pos, source)) {
                    Features::cPlayerPosSource = const_cast<char*>(source);
                    Features::cPlayerPos[0] = pos.x;
                    Features::cPlayerPos[1] = pos.y;
                    Features::cPlayerPos[2] = pos.z;
                    // Logger::Log("[FPC] Position (%s): X=%.2f Y=%.2f Z=%.2f", source, pos.x, pos.y, pos.z);
               }
          }

          if (Features::bSpeedEnabled && instance && !IsBadReadPtr(instance, 0x200)) 
          {
               // Named indices for clarity and safer writes
               constexpr int WALK_SPEED_IDX = 37;
               constexpr int RUN_SPEED_IDX = 38;
               constexpr int SPRINT_SPEED_IDX = 40;

               float* movement = (float*)instance;
               auto setMovement = [&](int idx, float val){
                    if (!movement) return;
                    if (!SDK::IsReadable(&movement[idx], sizeof(float))) {
                         Logger::Log("[FPC] movement[%d] not readable", idx);
                         return;
                    }
                    SDK::WriteSafe(&movement[idx], val);
               };

               setMovement(WALK_SPEED_IDX, Features::fSprintValue); // Walk Speed
               setMovement(RUN_SPEED_IDX, Features::fSprintValue); // Run Speed
               setMovement(SPRINT_SPEED_IDX, Features::fSprintValue); // Sprint Speed
          }

          oFPCUpdate(instance, methodInfo); // Call original game code
     }

     void hkChangeSanity(void* instance, float amount, void* methodInfo) 
     {
          static bool loggedSanity = false;
          if (!loggedSanity) {
               Logger::Log("[HOOK] PlayerSanity.ChangeSanity is executing.");
               loggedSanity = true;
          }
          if (Features::bNoSanityLoss && instance && !IsBadReadPtr(instance, 0x100)) 
          {
               // Negate sanity loss (keep sanity the same or increase it)
               if (amount < 0) amount = 0.0f; 
          }
          oChangeSanity(instance, amount, methodInfo); // Call original game code
     }
}