#pragma once
#include <windows.h>
#include "../SDK/IL2CPP.h"
#include "../SDK/il2cpp_subset.h"

namespace Hooks {

    struct GhostAI_o;
    struct GhostInfo_o;
    struct LevelRoom_o;
    struct GhostAI_fields;
    struct GhostInfo_Fields;

    // `UnityEngine_Vector3_o`, `FPC_o`, and `FPC_fields` are provided by
    // SDK/il2cpp_subset.h to avoid duplicating large generated headers here.

    struct GhostData_Fields {
        int32_t ghostTypeId1;
        int32_t ghostTypeId2;
        void* evidenceList1;
        void* evidenceList2;
        int32_t unkInt1;
        bool unkBool1;
        void* string;
        int32_t unkInt2;
        int32_t unkInt3;
        bool unkBool2;
        int32_t unkInt4;
        int32_t unkInt5;
        bool unkBool3;
    };

    struct GhostData_o {
        GhostData_Fields fields;
    };

    struct LevelRoom_o {
        void* klass;
        void* monitor;
        void* fields;
    };

    struct GhostInfo_Fields {
        void* super;
        void* unknown1;
        GhostAI_o* ghost;
        LevelRoom_o* levelRoom;
        float lastDeltaDiff;
        bool unknownBool;
    };

    struct GhostInfo_o {
        void* klass;
        void* monitor;
        GhostInfo_Fields fields;
    };

    struct GhostAI_fields {
        void* super;
        void* unknnown1;
        int32_t unknownInt1;
        GhostInfo_o* ghostInfo;
    };

    struct GhostAI_o {
        void* klass;
        void* monitor;
        GhostAI_fields fields;
    };

    // `Player_o` and `Player_fields` are provided by SDK/il2cpp_subset.h.

    typedef void (*Update_t)(void* instance, void* methodInfo);
    typedef void (*SetCard_t)(void* instance, int cardType, void* methodInfo);
    typedef int (*GetBonus_t)(void* instance);
    typedef bool (*IsPerfect_t)(void* instance);
    typedef void (*KickPlayerNetworked_t)(void* instance, bool isBanned, void* photonMessageInfo, void* methodInfo);
    typedef int (*GetRewardAmount_t)(void* instance, void* methodInfo);
    // void PlayerSanity__ChangeSanity(void* instance, float amount, void* methodInfo);
    typedef void (*ChangeSanity_t)(void* instance, float amount, void* methodInfo);
    typedef void (*GhostAI_Init_t)(void* instance, GhostData_o *data, void* methodInfo);
    typedef void (*GhostAI_Hunting_t)(void* instance, bool someBool, int32_t someInt32, void* photonMessageInfo, void* methodInfo);
    typedef void (*GhostAI_ChangeState_t)(void* instance, int32_t state, void* photonInteract, bool bParam2, void* methodInfo);
    typedef void (*SetupPlayer_t)(void* instance, void* methodInfo);

    typedef void (*EvidenceController_SpawnBoneDNAEvidence)(void* instance, void* levelRoom, void* methodInfo);
    typedef void (*DNAEvidence_Spawn_t)(void* instance, int32_t roomId, void* methodInfo);

    // Unity typedefs
    struct Vector3 {
        float x;
        float y;
        float z;
    };

    typedef void* (*GetTransform_t)(void* instance, void* method);
    typedef void (*GetPosition_t)(Vector3* ret, void* instance, void* method);

    // Cached ghost instance — set when GhostAI::Init fires, cleared when null
    inline GhostAI_o* gCurrentGhostAI = nullptr;
    inline void* gCurrentLightSwitch = nullptr;

    // Original function pointers
    inline Update_t oStaminaUpdate = nullptr;
    inline Update_t oFPCUpdate = nullptr;
    inline Update_t oGhostUpdate = nullptr;
    inline Update_t oLightSwitchStart = nullptr;
    inline SetCard_t oSetCard = nullptr;
    inline GetBonus_t oGetBonus = nullptr;
    inline IsPerfect_t oIsPerfect = nullptr;
    inline KickPlayerNetworked_t oServerManagerKickPlayerNetworked = nullptr;
    inline GetRewardAmount_t oGetRewardAmount = nullptr;
    inline ChangeSanity_t oChangeSanity = nullptr;
    inline GhostAI_Init_t oGhostAI_Init = nullptr;
	inline GhostAI_Hunting_t fnGhostAI_Hunting = nullptr; // direct call, not a detour
    inline GhostAI_ChangeState_t oGhostAI_ChangeState = nullptr;
    inline SetupPlayer_t oSetupPlayer = nullptr;

    inline EvidenceController_SpawnBoneDNAEvidence oEvidenceController_SpawnBoneDNAEvidence = nullptr;
    inline DNAEvidence_Spawn_t oDNAEvidence_Spawn = nullptr;

    // Unity function pointers (if needed for more complex hooks)
    inline GetTransform_t oGetTransform = nullptr; // UnityEngine.Component.get_transform
    inline GetPosition_t oGetPosition = nullptr; // UnityEngine.Transform.get_position

	// Hook function declarations
    void hkStaminaUpdate(void* instance, void* methodInfo);
    void hkFPCUpdate(void* instance, void* methodInfo);
    void hkGhostUpdate(void* instance, void* methodInfo);
    void hkSetCard(void* instance, int cardType, void* methodInfo);
    int hkGetBonus(void* instance);
    bool hkIsPerfect(void* instance);
    void hkKickPlayerNetworked(void* instance, bool isBanned, void* photonMessageInfo, void* methodInfo);
    int hkGetRewardAmount(void* instance, void* methodInfo);
    void hkChangeSanity(void* instance, float amount, void* methodInfo);
	void hkGhostAI_Init(void* instance, GhostData_o *data, void* methodInfo);
	void hkGhostAI_ChangeState(void* instance, int32_t state, void* photonInteract, bool bParam2, void* methodInfo);
	void ForceHunting(); // calls GhostAI::Hunting on the cached ghost instance
    void hkLightSwitchStart(void* instance, void* methodInfo);
    void hkSetupPlayer(void* instance, void* methodInfo);

    void hkEvidenceController_SpawnBoneDNAEvidence(void* instance, void* levelRoom, void* methodInfo);
    void hkDNAEvidence_Spawn(void* instance, int32_t roomId, void* methodInfo);

    void Init();
    void Shutdown();
}