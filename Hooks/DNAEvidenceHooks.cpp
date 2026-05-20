#include "Hooks.h"
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
    }

    if (oDNAEvidence_Spawn) {
        oDNAEvidence_Spawn(instance, iParam1, methodInfo);
    }
}