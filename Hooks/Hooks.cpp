#include <windows.h>
#include <stdio.h>
#include "Hooks.h"
#include "detours.h"
#include "../SDK/IL2CPP.h"
#include "../Utils/Logger.h"
#include "../Features/GhostControl.h"
#include "../Features/Stamina.h"
#include "../Features/Reporting.h"

#pragma comment(lib, "../Lib/detours.lib") 

namespace Hooks {
     void Init() {
          Logger::Log("[SYSTEM] Initializing hooks.");

          void* domain = SDK::domain_get();
          void* assembly = SDK::assembly_open(domain, "Assembly-CSharp");
          void* image = SDK::assembly_get_image(assembly);

          if (!Reporting::Initialize(domain, image, GetModuleHandleW(L"GameAssembly.dll"))) {
               Logger::Log("[ERROR] Detection/report protections unavailable; gameplay hooks will not be installed.");
               return;
          }

          void* unityAssembly = SDK::assembly_open(domain, "UnityEngine.CoreModule");
          void* unityImage = SDK::assembly_get_image(unityAssembly);

          auto getMethod = [&](const char* className, const char* methodName, int args) {
               void* klass = SDK::class_from_name(image, "", className);
               return klass ? SDK::get_method(klass, methodName, args) : nullptr;
          };

          auto getUnityMethod = [&](const char* namespaze, const char* className, const char* methodName, int args) {
               void* klass = SDK::class_from_name(unityImage, namespaze, className);
               return klass ? SDK::get_method(klass, methodName, args) : nullptr;
          };

          // Existing Hooks (Dereferenced as per your SDK)
          if (Stamina::Initialize(image)) {
               void* pc = getMethod("PCStamina", "Update", 0);
               void* vr = getMethod("VRStamina", "Update", 0);
               if (pc) SDK::ReadSafe(pc, oPCStaminaUpdate);
               if (vr) SDK::ReadSafe(vr, oVRStaminaUpdate);
          }
          Logger::Log("[HOOK] PC/VR stamina updates %s / %s.",
               oPCStaminaUpdate ? "resolved" : "unavailable", oVRStaminaUpdate ? "resolved" : "unavailable");

          void* mFPC = getMethod("FirstPersonController", "Update", 0);
          if (mFPC) oFPCUpdate = *(Update_t*)mFPC;
          Logger::Log("[HOOK] FirstPersonController.Update %s", mFPC ? "resolved" : "missing");

          void* mGhost = getMethod("GhostInfo", "Update", 0);
          if (mGhost) oGhostUpdate = *(Update_t*)mGhost;
          Logger::Log("[HOOK] GhostInfo.Update %s", mGhost ? "resolved" : "missing");

          void* mLightSwitchStart = getMethod("LightSwitch", "Start", 0);
          if (mLightSwitchStart) oLightSwitchStart = *(Update_t*)mLightSwitchStart;
          Logger::Log("[HOOK] LightSwitch.Start %s", mLightSwitchStart ? "resolved" : "missing");

          void* mTarot = getMethod("TarotCards", "SetCard", 1);
          if (mTarot) oSetCard = *(SetCard_t*)mTarot;
          Logger::Log("[HOOK] TarotCards.SetCard %s", mTarot ? "resolved" : "missing");

          void* mBonus = getMethod("LevelValues", "GetInvestigationBonusReward", 0);
          if (mBonus) oGetBonus = *(GetBonus_t*)mBonus;
          Logger::Log("[HOOK] LevelValues.GetInvestigationBonusReward %s", mBonus ? "resolved" : "missing");

          void* mPerfect = getMethod("LevelValues", "IsPerfectGame", 0);
          if (mPerfect) oIsPerfect = *(IsPerfect_t*)mPerfect;
          Logger::Log("[HOOK] LevelValues.IsPerfectGame %s", mPerfect ? "resolved" : "missing");

          void* mKickPlayerNetworked = getMethod("ServerManager", "KickPlayerNetworked", 2);
          if (mKickPlayerNetworked) oServerManagerKickPlayerNetworked = *(KickPlayerNetworked_t*)mKickPlayerNetworked;
          Logger::Log("[HOOK] ServerManager.KickPlayerNetworked %s", mKickPlayerNetworked ? "resolved" : "missing");

          void* mGetRewardAmount = getMethod("Media", "GetRewardAmount", 0);
          if (mGetRewardAmount) oGetRewardAmount = *(GetRewardAmount_t*)mGetRewardAmount;
          Logger::Log("[HOOK] Media.GetRewardAmount %s", mGetRewardAmount ? "resolved" : "missing");

          void* mChangeSanity = getMethod("PlayerSanity", "ChangeSanity", 1);
          if (mChangeSanity) oChangeSanity = *(ChangeSanity_t*)mChangeSanity;
          Logger::Log("[HOOK] PlayerSanity.ChangeSanity %s", mChangeSanity ? "resolved" : "missing");

          void* mGhostAI_Init = getMethod("GhostAI", "Init", 1);
          if (mGhostAI_Init) oGhostAI_Init = *(GhostAI_Init_t*)mGhostAI_Init;
          Logger::Log("[HOOK] GhostAI.Init %s", mGhostAI_Init ? "resolved" : "missing");

          void* mGhostAI_Destroy = getMethod("GhostAI", "OnDestroy", 0);
          if (mGhostAI_Destroy) oGhostAI_Destroy = *(Update_t*)mGhostAI_Destroy;
          Logger::Log("[HOOK] GhostAI.OnDestroy %s", mGhostAI_Destroy ? "resolved" : "missing");

          if (SDK::get_field && SDK::field_get_offset && SDK::method_get_param &&
              SDK::class_from_type && SDK::class_value_size && mGhostAI_Init) {
               void* ghostInfoClass = SDK::class_from_name(image, "", "GhostInfo");
               void* ghostField = ghostInfoClass ? SDK::get_field(ghostInfoClass, "ghost") : nullptr;
               const void* dataType = SDK::method_get_param(mGhostAI_Init, 0);
               void* dataClass = dataType ? SDK::class_from_type(dataType) : nullptr;
               uint32_t alignment = 0;
               gGhostInfoLayoutValid = ghostField && dataClass &&
                    SDK::field_get_offset(ghostField) == offsetof(GhostInfo_o, fields) + offsetof(GhostInfo_Fields, ghost) &&
                    SDK::class_value_size(dataClass, &alignment) == sizeof(GhostData_o);
          }
          Logger::Log(gGhostInfoLayoutValid
               ? "[GHOST] GhostInfo layout matches the recovered x64 layout."
               : "[ERROR] Unsupported GhostInfo layout or missing metadata exports; ghost display disabled.");

          void* mGhostAI_Hunting = getMethod("GhostAI", "Hunting", 3);
          if (mGhostAI_Hunting) fnGhostAI_Hunting = *(GhostAI_Hunting_t*)mGhostAI_Hunting;
          Logger::Log("[CALL] GhostAI.Hunting %s", mGhostAI_Hunting ? "resolved" : "missing");

          void* mGhostAI_ChangeState = getMethod("GhostAI", "ChangeState", 3);
          if (mGhostAI_ChangeState) oGhostAI_ChangeState = *(GhostAI_ChangeState_t*)mGhostAI_ChangeState;
          Logger::Log("[HOOK] GhostAI.ChangeState %s", mGhostAI_ChangeState ? "resolved" : "missing");

          // Unity methods returned by il2cpp_class_get_method_from_name are MethodInfo*.
          // We must dereference once to get the native callable method pointer.
          void* mUnityGetTransform = getUnityMethod("UnityEngine", "Component", "get_transform", 0);
          Logger::Log("[UNITY] Component.get_transform %s", mUnityGetTransform ? "resolved" : "missing");

          void* mUnityGetPosition = getUnityMethod("UnityEngine", "Transform", "get_position", 0);
          Logger::Log("[UNITY] Transform.get_position %s", mUnityGetPosition ? "resolved" : "missing");

          getTransformMethodInfo = mUnityGetTransform;
          getPositionInjectedMethodInfo = getUnityMethod("UnityEngine", "Transform", "get_position_Injected", 1);
          if (getPositionInjectedMethodInfo) oGetPositionInjected = *(GetPositionInjected_t*)getPositionInjectedMethodInfo;
          Logger::Log(oGetPositionInjected
               ? "[UNITY] Transform.get_position_Injected resolved"
               : "[ERROR] Transform.get_position_Injected missing; ghost coordinates unavailable.");
          oGetTransform = mUnityGetTransform ? *(GetTransform_t*)mUnityGetTransform : nullptr;
          oGetPosition = mUnityGetPosition ? *(GetPosition_t*)mUnityGetPosition : nullptr;
          InitRadar(domain, image, unityImage);
          
          void* mEvidenceController_SpawnBoneDNAEvidence = getMethod("EvidenceController", "SpawnBoneDNAEvidence", 1);
          if (mEvidenceController_SpawnBoneDNAEvidence) oEvidenceController_SpawnBoneDNAEvidence = *(EvidenceController_SpawnBoneDNAEvidence*)mEvidenceController_SpawnBoneDNAEvidence;
          Logger::Log("[HOOK] EvidenceController.SpawnBoneDNAEvidence %s", mEvidenceController_SpawnBoneDNAEvidence ? "resolved" : "missing");

          void* mDNAEvidence_Spawn = getMethod("DNAEvidence", "Spawn", 1);
          if (mDNAEvidence_Spawn) oDNAEvidence_Spawn = *(DNAEvidence_Spawn_t*)mDNAEvidence_Spawn;
          Logger::Log("[HOOK] DNAEvidence.Spawn %s", mDNAEvidence_Spawn ? "resolved" : "missing");

          DetourTransactionBegin();
          DetourUpdateThread(GetCurrentThread());

          LONG worldHookError = Reporting::Install(true);
          const auto attachWorld = [&worldHookError](PVOID* original, PVOID hook) {
               const LONG error = DetourAttach(original, hook);
               if (error != NO_ERROR) worldHookError = error;
          };
          if (oPCStaminaUpdate) attachWorld(&(PVOID&)oPCStaminaUpdate, hkPCStaminaUpdate);
          if (oVRStaminaUpdate) attachWorld(&(PVOID&)oVRStaminaUpdate, hkVRStaminaUpdate);
          if (oFPCUpdate) DetourAttach(&(PVOID&)oFPCUpdate, hkFPCUpdate);
          if (oGhostUpdate) DetourAttach(&(PVOID&)oGhostUpdate, hkGhostUpdate);
          if (oLightSwitchStart) DetourAttach(&(PVOID&)oLightSwitchStart, hkLightSwitchStart);
          if (oSetCard) DetourAttach(&(PVOID&)oSetCard, hkSetCard);
          if (oGetBonus) DetourAttach(&(PVOID&)oGetBonus, hkGetBonus);
          if (oIsPerfect) DetourAttach(&(PVOID&)oIsPerfect, hkIsPerfect);
          if (oServerManagerKickPlayerNetworked) DetourAttach(&(PVOID&)oServerManagerKickPlayerNetworked, hkKickPlayerNetworked);
          if (oGetRewardAmount) DetourAttach(&(PVOID&)oGetRewardAmount, hkGetRewardAmount);
          if (oChangeSanity) DetourAttach(&(PVOID&)oChangeSanity, hkChangeSanity);
          if (oGhostAI_Init) DetourAttach(&(PVOID&)oGhostAI_Init, hkGhostAI_Init);
          if (oGhostAI_Destroy) DetourAttach(&(PVOID&)oGhostAI_Destroy, hkGhostAI_Destroy);
          if (oGhostAI_ChangeState) DetourAttach(&(PVOID&)oGhostAI_ChangeState, hkGhostAI_ChangeState);
          if (oGhostVisibility) attachWorld(&(PVOID&)oGhostVisibility, hkGhostVisibility);
          if (oPropGrabUpdate) attachWorld(&(PVOID&)oPropGrabUpdate, hkPropGrabUpdate);
          if (oDoorFixedUpdate) attachWorld(&(PVOID&)oDoorFixedUpdate, hkDoorFixedUpdate);
          if (oCursorLock) attachWorld(&(PVOID&)oCursorLock, hkCursorLock);
          if (oCursorVisible) attachWorld(&(PVOID&)oCursorVisible, hkCursorVisible);
          for (auto& input : worldInputHooks)
               if (input.original) attachWorld(&(PVOID&)input.original, input.hook);
          const LONG controlError = GhostControl::InstallHooks(true);
          if (controlError != NO_ERROR) worldHookError = controlError;

          if (oEvidenceController_SpawnBoneDNAEvidence) DetourAttach(&(PVOID&)oEvidenceController_SpawnBoneDNAEvidence, hkEvidenceController_SpawnBoneDNAEvidence);
          if (oDNAEvidence_Spawn) DetourAttach(&(PVOID&)oDNAEvidence_Spawn, hkDNAEvidence_Spawn);

          if (worldHookError != NO_ERROR) {
               DetourTransactionAbort();
               Logger::Log("[ERROR] Hook transaction aborted: world-option attachment failed (%ld).", worldHookError);
               return;
          }
          const LONG result = DetourTransactionCommit();
          Reporting::installed = result == NO_ERROR;
          SetWorldHooksInstalled(result == NO_ERROR);
          if (result == NO_ERROR) {
               Logger::Log("[SYSTEM] Hook transaction committed.");
               Logger::Log("[REPORTING] Local automatic SelfPlayerReport blocking active; other clients/server checks are unaffected.");
               Logger::Log("[DETECTION] HACKER-specific termination suppression active; unrelated crash/quit handling is unchanged.");
          } else {
               Logger::Log("[ERROR] Hook transaction failed (%ld).", result);
          }
     }

     bool Shutdown() {
          Logger::Log("[SYSTEM] Detaching hooks.");
          if (!Reporting::installed.load()) {
               Logger::Log("[SYSTEM] No committed gameplay hooks to detach.");
               return true;
          }
          SetWorldHooksInstalled(false);

          DetourTransactionBegin();
          DetourUpdateThread(GetCurrentThread());
          LONG detachError = Reporting::Install(false);
          const auto detach = [&detachError](PVOID* original, PVOID hook) {
               const LONG error = DetourDetach(original, hook);
               if (error != NO_ERROR) detachError = error;
          };
          if (oPCStaminaUpdate) detach(&(PVOID&)oPCStaminaUpdate, hkPCStaminaUpdate);
          if (oVRStaminaUpdate) detach(&(PVOID&)oVRStaminaUpdate, hkVRStaminaUpdate);
          if (oFPCUpdate) detach(&(PVOID&)oFPCUpdate, hkFPCUpdate);
          if (oGhostUpdate) detach(&(PVOID&)oGhostUpdate, hkGhostUpdate);
          if (oLightSwitchStart) detach(&(PVOID&)oLightSwitchStart, hkLightSwitchStart);
          if (oSetCard) detach(&(PVOID&)oSetCard, hkSetCard);
          if (oGetBonus) detach(&(PVOID&)oGetBonus, hkGetBonus);
          if (oIsPerfect) detach(&(PVOID&)oIsPerfect, hkIsPerfect);
          if (oServerManagerKickPlayerNetworked) detach(&(PVOID&)oServerManagerKickPlayerNetworked, hkKickPlayerNetworked);
          if (oGetRewardAmount) detach(&(PVOID&)oGetRewardAmount, hkGetRewardAmount);
          if (oChangeSanity) detach(&(PVOID&)oChangeSanity, hkChangeSanity);
          if (oGhostAI_Init) detach(&(PVOID&)oGhostAI_Init, hkGhostAI_Init);
          if (oGhostAI_Destroy) detach(&(PVOID&)oGhostAI_Destroy, hkGhostAI_Destroy);
          if (oGhostAI_ChangeState) detach(&(PVOID&)oGhostAI_ChangeState, hkGhostAI_ChangeState);
          if (oGhostVisibility) detach(&(PVOID&)oGhostVisibility, hkGhostVisibility);
          if (oPropGrabUpdate) detach(&(PVOID&)oPropGrabUpdate, hkPropGrabUpdate);
          if (oDoorFixedUpdate) detach(&(PVOID&)oDoorFixedUpdate, hkDoorFixedUpdate);
          if (oCursorLock) detach(&(PVOID&)oCursorLock, hkCursorLock);
          if (oCursorVisible) detach(&(PVOID&)oCursorVisible, hkCursorVisible);
          for (auto& input : worldInputHooks)
               if (input.original) detach(&(PVOID&)input.original, input.hook);
          const LONG controlError = GhostControl::InstallHooks(false);
          if (controlError != NO_ERROR) detachError = controlError;
          if (oEvidenceController_SpawnBoneDNAEvidence) detach(&(PVOID&)oEvidenceController_SpawnBoneDNAEvidence, hkEvidenceController_SpawnBoneDNAEvidence);
          if (oDNAEvidence_Spawn) detach(&(PVOID&)oDNAEvidence_Spawn, hkDNAEvidence_Spawn);

          if (detachError != NO_ERROR) {
               DetourTransactionAbort();
               Logger::Log("[ERROR] Hook detachment aborted (%ld); DLL will remain loaded.", detachError);
               return false;
          }

          if (DetourTransactionCommit() == NO_ERROR) {
               Reporting::installed = false;
               Logger::Log("[SYSTEM] Hook detachment committed.");
               return true;
          } else {
               Logger::Log("[ERROR] Hook detachment transaction failed.");
               return false;
          }
     }
}