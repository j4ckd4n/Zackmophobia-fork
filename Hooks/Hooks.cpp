#include <windows.h>
#include <stdio.h>
#include "Hooks.h"
#include "detours.h"
#include "../SDK/IL2CPP.h"
#include "../Utils/Logger.h"

#pragma comment(lib, "../Lib/detours.lib") 

namespace Hooks {
     void Init() {
          Logger::Log("[SYSTEM] Initializing hooks.");

          void* domain = SDK::domain_get();
          void* assembly = SDK::assembly_open(domain, "Assembly-CSharp");
          void* image = SDK::assembly_get_image(assembly);

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
          void* mStamina = getMethod("PlayerStamina", "Update", 0);
          if (mStamina) oStaminaUpdate = *(Update_t*)mStamina;
          Logger::Log("[HOOK] PlayerStamina.Update %s", mStamina ? "resolved" : "missing");

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
          
          void* mEvidenceController_SpawnBoneDNAEvidence = getMethod("EvidenceController", "SpawnBoneDNAEvidence", 1);
          if (mEvidenceController_SpawnBoneDNAEvidence) oEvidenceController_SpawnBoneDNAEvidence = *(EvidenceController_SpawnBoneDNAEvidence*)mEvidenceController_SpawnBoneDNAEvidence;
          Logger::Log("[HOOK] EvidenceController.SpawnBoneDNAEvidence %s", mEvidenceController_SpawnBoneDNAEvidence ? "resolved" : "missing");

          void* mDNAEvidence_Spawn = getMethod("DNAEvidence", "Spawn", 1);
          if (mDNAEvidence_Spawn) oDNAEvidence_Spawn = *(DNAEvidence_Spawn_t*)mDNAEvidence_Spawn;
          Logger::Log("[HOOK] DNAEvidence.Spawn %s", mDNAEvidence_Spawn ? "resolved" : "missing");

          DetourTransactionBegin();
          DetourUpdateThread(GetCurrentThread());

          if (oStaminaUpdate) DetourAttach(&(PVOID&)oStaminaUpdate, hkStaminaUpdate);
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
          if (oGhostAI_ChangeState) DetourAttach(&(PVOID&)oGhostAI_ChangeState, hkGhostAI_ChangeState);

          if (oEvidenceController_SpawnBoneDNAEvidence) DetourAttach(&(PVOID&)oEvidenceController_SpawnBoneDNAEvidence, hkEvidenceController_SpawnBoneDNAEvidence);
          if (oDNAEvidence_Spawn) DetourAttach(&(PVOID&)oDNAEvidence_Spawn, hkDNAEvidence_Spawn);

          oGetTransform = mUnityGetTransform ? *(GetTransform_t*)mUnityGetTransform : nullptr;
          oGetPosition = mUnityGetPosition ? *(GetPosition_t*)mUnityGetPosition : nullptr;

          DetourTransactionCommit();
          printf("[SYSTEM] All Hooks Applied Successfully.\n");
          Logger::Log("[SYSTEM] Hook transaction committed.");
     }

     void Shutdown() {
          Logger::Log("[SYSTEM] Detaching hooks.");

          DetourTransactionBegin();
          DetourUpdateThread(GetCurrentThread());

          if (oStaminaUpdate) DetourDetach(&(PVOID&)oStaminaUpdate, hkStaminaUpdate);
          if (oFPCUpdate) DetourDetach(&(PVOID&)oFPCUpdate, hkFPCUpdate);
          if (oGhostUpdate) DetourDetach(&(PVOID&)oGhostUpdate, hkGhostUpdate);
          if (oLightSwitchStart) DetourDetach(&(PVOID&)oLightSwitchStart, hkLightSwitchStart);
          if (oSetCard) DetourDetach(&(PVOID&)oSetCard, hkSetCard);
          if (oGetBonus) DetourDetach(&(PVOID&)oGetBonus, hkGetBonus);
          if (oIsPerfect) DetourDetach(&(PVOID&)oIsPerfect, hkIsPerfect);
          if (oServerManagerKickPlayerNetworked) DetourDetach(&(PVOID&)oServerManagerKickPlayerNetworked, hkKickPlayerNetworked);
          if (oGetRewardAmount) DetourDetach(&(PVOID&)oGetRewardAmount, hkGetRewardAmount);
          if (oChangeSanity) DetourDetach(&(PVOID&)oChangeSanity, hkChangeSanity);
          if (oGhostAI_Init) DetourDetach(&(PVOID&)oGhostAI_Init, hkGhostAI_Init);
          if (oGhostAI_ChangeState) DetourDetach(&(PVOID&)oGhostAI_ChangeState, hkGhostAI_ChangeState);
          if (oEvidenceController_SpawnBoneDNAEvidence) DetourDetach(&(PVOID&)oEvidenceController_SpawnBoneDNAEvidence, hkEvidenceController_SpawnBoneDNAEvidence);
          if (oDNAEvidence_Spawn) DetourDetach(&(PVOID&)oDNAEvidence_Spawn, hkDNAEvidence_Spawn);

          if (DetourTransactionCommit() == NO_ERROR) {
               Logger::Log("[SYSTEM] Hook detachment committed.");
          } else {
               Logger::Log("[ERROR] Hook detachment transaction failed.");
          }
     }
}