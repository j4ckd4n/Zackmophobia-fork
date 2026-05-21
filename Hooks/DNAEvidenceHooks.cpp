#include "Hooks.h"
#include <array>
#include <cmath>
#include <stdio.h>
#include "../Features/Features.h"
#include "../Utils/Logger.h"

namespace { 
    inline bool IsFinite3(const Hooks::Vector3& v) {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    }

    inline bool LooksLikeWorldPos(const Hooks::Vector3& v) {
        if (!IsFinite3(v)) return false;
        const float MAX_REASONABLE = 100000.0f;
        return (fabsf(v.x) < MAX_REASONABLE && fabsf(v.y) < MAX_REASONABLE && fabsf(v.z) < MAX_REASONABLE);
    }

    bool TryExtractRoomNameFromLevelRoom(void* levelRoomObj, std::string& outRoom, const char*& source)
    {
        if (!SDK::IsReadable(levelRoomObj, 0x18)) {
            return false;
        }

        void* levelRoomFields = SDK::GetFieldsPtr(levelRoomObj);
        if (!SDK::IsReadable(levelRoomFields, 0x60)) {
            return false;
        }

        // From observed layout, name-like System_String* tends to be around these offsets.
        constexpr std::array<size_t, 6> ROOM_NAME_OFFSETS = {0x40, 0x48, 0x50, 0x58, 0x60, 0x68};
        for (size_t off : ROOM_NAME_OFFSETS) {
            void* roomNamePtr = nullptr;
            if (!SDK::ReadSafe((char*)levelRoomFields + off, roomNamePtr)) {
                continue;
            }
            if (!SDK::IsReadable(roomNamePtr, 0x18)) {
                continue;
            }

            std::string name = SDK::IL2CPP_To_String(roomNamePtr);
            if (!name.empty()) {
                outRoom = name;
                source = "LevelRoom.fields";
                return true;
            }
        }

        return false;
    }

    inline void* evidenceControllerFields = nullptr;
}

void Hooks::hkEvidenceController_SpawnBoneDNAEvidence(void* instance, void* levelRoom, void* methodInfo)
{
    if (!evidenceControllerFields) {
        evidenceControllerFields = instance;
        Logger::Log("[HOOK] EvidenceController.SpawnBoneDNAEvidence called. Instance pointer stored for later use.");
    }

    if (oEvidenceController_SpawnBoneDNAEvidence) {
        oEvidenceController_SpawnBoneDNAEvidence(instance, levelRoom, methodInfo);
    }
}

void Hooks::hkDNAEvidence_Spawn(void* instance, int32_t iParam1, void* methodInfo)
{
    static bool logged = false;
    if (!logged) {
        Logger::Log("[HOOK] DNAEvidence.Spawn is executing.");
        logged = true;
    }

    Logger::Log("[DNA] Spawn called with iParam1=%d", iParam1);

    if(SDK::IsReadable(instance, 0x60)) {
        if(Hooks::oGetTransform && Hooks::oGetPosition) {
            void* transform = Hooks::oGetTransform(instance, nullptr);
            if (transform) {
                Hooks::Vector3 pos{};
                Hooks::oGetPosition(&pos, transform, nullptr);
                if(LooksLikeWorldPos(pos)) {
                    Features::fBonePos[0] = pos.x;
                    Features::fBonePos[1] = pos.y;
                    Features::fBonePos[2] = pos.z;
                    Logger::Log("[DNA] Spawn position: X=%.2f Y=%.2f Z=%.2f", pos.x, pos.y, pos.z);
                }
            }
        }

        // lets extract levelRoom_array from EvidenceController fields
        void* evidenceControllerFields = SDK::GetFieldsPtr(instance);
        Logger::Log("[DNA] eController fields pointer: %p", evidenceControllerFields);

        // need to figure out a way to extract the room name from the parameter being passed.
    }

    if (oDNAEvidence_Spawn) {
        oDNAEvidence_Spawn(instance, iParam1, methodInfo);
    }
}