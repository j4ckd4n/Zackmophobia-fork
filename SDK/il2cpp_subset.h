// Auto-generated minimal subset of il2cpp_ghidra.h
// Contains only the small set of types we need right now (Vector3, Transform, Player, FPC)
// Keep types minimal and safe to avoid large header conflicts.
#pragma once

#include <cstdint>

struct UnityEngine_Vector3_fields {
    float x;
    float y;
    float z;
};

struct UnityEngine_Vector3_o {
    void* klass;
    void* monitor;
    UnityEngine_Vector3_fields fields;
};

struct UnityEngine_Transform_fields {
    void* super; // base MonoBehaviour fields (kept opaque)
    void* parent; // Transform_o*
    UnityEngine_Vector3_o* position; // often points to a Vector3 representation
};

struct UnityEngine_Transform_o {
    void* klass;
    void* monitor;
    UnityEngine_Transform_fields fields;
};

struct Player_fields {
    void* super;
    void* someBoolOrFlags;
    void* transformObj; // UnityEngine_Transform_o* (kept as void* to avoid tight coupling)
};

struct Player_o {
    void* klass;
    void* monitor;
    Player_fields* fields;
};

struct FPC_fields {
    void* super;
    UnityEngine_Vector3_o* vector3;
    UnityEngine_Vector3_o* vector3_2;
    void* transformObj; // UnityEngine_Transform_o*
};

struct FPC_o {
    void* klass;
    void* monitor;
    FPC_fields fields;
};
