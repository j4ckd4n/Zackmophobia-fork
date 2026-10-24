#include "Hooks.h"
#include "../Utils/Logger.h"

void Hooks::hkEvidenceController_SpawnBoneDNAEvidence(void* instance, void* levelRoom, void* methodInfo)
{
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

    if (oDNAEvidence_Spawn) {
        oDNAEvidence_Spawn(instance, iParam1, methodInfo);
    }
}