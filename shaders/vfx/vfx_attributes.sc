#ifndef VFX_ATTRIBUTES_GENERATED_SC
#define VFX_ATTRIBUTES_GENERATED_SC

#include "bgfx_compute.sh"

#ifndef VFX_ATTRIBUTE_BUFFER_STAGE
#define VFX_ATTRIBUTE_BUFFER_STAGE 0
#endif

#if defined(VFX_ATTRIBUTE_BUFFER_WRITE)
BUFFER_RAW_RW(s_vfxAttributes, VFX_ATTRIBUTE_BUFFER_STAGE);
#else
BUFFER_RAW_RO(s_vfxAttributes, VFX_ATTRIBUTE_BUFFER_STAGE);
#endif

#define VFX_ATTRIBUTE_COUNT 16u
#define VFX_ATTRIBUTE_STRIDE 31u
#define VFX_ATTRIBUTE_STRIDE_BYTES 124u
#define VFX_ATTRIBUTE_CAPACITY 1024u
#define VFX_ATTRIBUTE_OFFSET_position 0u
#define VFX_ATTRIBUTE_BYTE_OFFSET_position 0u
#define VFX_ATTRIBUTE_OFFSET_velocity 3u
#define VFX_ATTRIBUTE_BYTE_OFFSET_velocity 12u
#define VFX_ATTRIBUTE_OFFSET_color 6u
#define VFX_ATTRIBUTE_BYTE_OFFSET_color 24u
#define VFX_ATTRIBUTE_OFFSET_alpha 9u
#define VFX_ATTRIBUTE_BYTE_OFFSET_alpha 36u
#define VFX_ATTRIBUTE_OFFSET_size 10u
#define VFX_ATTRIBUTE_BYTE_OFFSET_size 40u
#define VFX_ATTRIBUTE_OFFSET_age 11u
#define VFX_ATTRIBUTE_BYTE_OFFSET_age 44u
#define VFX_ATTRIBUTE_OFFSET_lifetime 12u
#define VFX_ATTRIBUTE_BYTE_OFFSET_lifetime 48u
#define VFX_ATTRIBUTE_OFFSET_seed 13u
#define VFX_ATTRIBUTE_BYTE_OFFSET_seed 52u
#define VFX_ATTRIBUTE_OFFSET_particleId 14u
#define VFX_ATTRIBUTE_BYTE_OFFSET_particleId 56u
#define VFX_ATTRIBUTE_OFFSET_spawnIndex 15u
#define VFX_ATTRIBUTE_BYTE_OFFSET_spawnIndex 60u
#define VFX_ATTRIBUTE_OFFSET_alive 16u
#define VFX_ATTRIBUTE_BYTE_OFFSET_alive 64u
#define VFX_ATTRIBUTE_OFFSET_angle 17u
#define VFX_ATTRIBUTE_BYTE_OFFSET_angle 68u
#define VFX_ATTRIBUTE_OFFSET_scale 18u
#define VFX_ATTRIBUTE_BYTE_OFFSET_scale 72u
#define VFX_ATTRIBUTE_OFFSET_targetPosition 21u
#define VFX_ATTRIBUTE_BYTE_OFFSET_targetPosition 84u
#define VFX_ATTRIBUTE_OFFSET_oldPosition 24u
#define VFX_ATTRIBUTE_BYTE_OFFSET_oldPosition 96u
#define VFX_ATTRIBUTE_OFFSET_user0 27u
#define VFX_ATTRIBUTE_BYTE_OFFSET_user0 108u

struct VfxAttributes
{
    uint base;

    void Init(uint particleIndex)
    {
        base = particleIndex * VFX_ATTRIBUTE_STRIDE;
    }

    vec3 Load_position()
    {
        return vec3(rawLoadFloat(s_vfxAttributes, base + 0u), rawLoadFloat(s_vfxAttributes, base + 1u), rawLoadFloat(s_vfxAttributes, base + 2u));
    }

#if defined(VFX_ATTRIBUTE_BUFFER_WRITE)
    void Store_position(vec3 value)
    {
        rawStoreFloat(s_vfxAttributes, base + 0u, value.x);
        rawStoreFloat(s_vfxAttributes, base + 1u, value.y);
        rawStoreFloat(s_vfxAttributes, base + 2u, value.z);
    }
#endif

    vec3 Load_velocity()
    {
        return vec3(rawLoadFloat(s_vfxAttributes, base + 3u), rawLoadFloat(s_vfxAttributes, base + 4u), rawLoadFloat(s_vfxAttributes, base + 5u));
    }

#if defined(VFX_ATTRIBUTE_BUFFER_WRITE)
    void Store_velocity(vec3 value)
    {
        rawStoreFloat(s_vfxAttributes, base + 3u, value.x);
        rawStoreFloat(s_vfxAttributes, base + 4u, value.y);
        rawStoreFloat(s_vfxAttributes, base + 5u, value.z);
    }
#endif

    vec3 Load_color()
    {
        return vec3(rawLoadFloat(s_vfxAttributes, base + 6u), rawLoadFloat(s_vfxAttributes, base + 7u), rawLoadFloat(s_vfxAttributes, base + 8u));
    }

#if defined(VFX_ATTRIBUTE_BUFFER_WRITE)
    void Store_color(vec3 value)
    {
        rawStoreFloat(s_vfxAttributes, base + 6u, value.x);
        rawStoreFloat(s_vfxAttributes, base + 7u, value.y);
        rawStoreFloat(s_vfxAttributes, base + 8u, value.z);
    }
#endif

    float Load_alpha()
    {
        return rawLoadFloat(s_vfxAttributes, base + 9u);
    }

#if defined(VFX_ATTRIBUTE_BUFFER_WRITE)
    void Store_alpha(float value)
    {
        rawStoreFloat(s_vfxAttributes, base + 9u, value);
    }
#endif

    float Load_size()
    {
        return rawLoadFloat(s_vfxAttributes, base + 10u);
    }

#if defined(VFX_ATTRIBUTE_BUFFER_WRITE)
    void Store_size(float value)
    {
        rawStoreFloat(s_vfxAttributes, base + 10u, value);
    }
#endif

    float Load_age()
    {
        return rawLoadFloat(s_vfxAttributes, base + 11u);
    }

#if defined(VFX_ATTRIBUTE_BUFFER_WRITE)
    void Store_age(float value)
    {
        rawStoreFloat(s_vfxAttributes, base + 11u, value);
    }
#endif

    float Load_lifetime()
    {
        return rawLoadFloat(s_vfxAttributes, base + 12u);
    }

#if defined(VFX_ATTRIBUTE_BUFFER_WRITE)
    void Store_lifetime(float value)
    {
        rawStoreFloat(s_vfxAttributes, base + 12u, value);
    }
#endif

    uint Load_seed()
    {
        return rawLoadUint(s_vfxAttributes, base + 13u);
    }

#if defined(VFX_ATTRIBUTE_BUFFER_WRITE)
    void Store_seed(uint value)
    {
        rawStoreUint(s_vfxAttributes, base + 13u, value);
    }
#endif

    uint Load_particleId()
    {
        return rawLoadUint(s_vfxAttributes, base + 14u);
    }

#if defined(VFX_ATTRIBUTE_BUFFER_WRITE)
    void Store_particleId(uint value)
    {
        rawStoreUint(s_vfxAttributes, base + 14u, value);
    }
#endif

    uint Load_spawnIndex()
    {
        return rawLoadUint(s_vfxAttributes, base + 15u);
    }

#if defined(VFX_ATTRIBUTE_BUFFER_WRITE)
    void Store_spawnIndex(uint value)
    {
        rawStoreUint(s_vfxAttributes, base + 15u, value);
    }
#endif

    bool Load_alive()
    {
        return (rawLoadUint(s_vfxAttributes, base + 16u) != 0u);
    }

#if defined(VFX_ATTRIBUTE_BUFFER_WRITE)
    void Store_alive(bool value)
    {
        rawStoreUint(s_vfxAttributes, base + 16u, value ? 1u : 0u);
    }
#endif

    float Load_angle()
    {
        return rawLoadFloat(s_vfxAttributes, base + 17u);
    }

#if defined(VFX_ATTRIBUTE_BUFFER_WRITE)
    void Store_angle(float value)
    {
        rawStoreFloat(s_vfxAttributes, base + 17u, value);
    }
#endif

    vec3 Load_scale()
    {
        return vec3(rawLoadFloat(s_vfxAttributes, base + 18u), rawLoadFloat(s_vfxAttributes, base + 19u), rawLoadFloat(s_vfxAttributes, base + 20u));
    }

#if defined(VFX_ATTRIBUTE_BUFFER_WRITE)
    void Store_scale(vec3 value)
    {
        rawStoreFloat(s_vfxAttributes, base + 18u, value.x);
        rawStoreFloat(s_vfxAttributes, base + 19u, value.y);
        rawStoreFloat(s_vfxAttributes, base + 20u, value.z);
    }
#endif

    vec3 Load_targetPosition()
    {
        return vec3(rawLoadFloat(s_vfxAttributes, base + 21u), rawLoadFloat(s_vfxAttributes, base + 22u), rawLoadFloat(s_vfxAttributes, base + 23u));
    }

#if defined(VFX_ATTRIBUTE_BUFFER_WRITE)
    void Store_targetPosition(vec3 value)
    {
        rawStoreFloat(s_vfxAttributes, base + 21u, value.x);
        rawStoreFloat(s_vfxAttributes, base + 22u, value.y);
        rawStoreFloat(s_vfxAttributes, base + 23u, value.z);
    }
#endif

    vec3 Load_oldPosition()
    {
        return vec3(rawLoadFloat(s_vfxAttributes, base + 24u), rawLoadFloat(s_vfxAttributes, base + 25u), rawLoadFloat(s_vfxAttributes, base + 26u));
    }

#if defined(VFX_ATTRIBUTE_BUFFER_WRITE)
    void Store_oldPosition(vec3 value)
    {
        rawStoreFloat(s_vfxAttributes, base + 24u, value.x);
        rawStoreFloat(s_vfxAttributes, base + 25u, value.y);
        rawStoreFloat(s_vfxAttributes, base + 26u, value.z);
    }
#endif

    vec4 Load_user0()
    {
        return vec4(rawLoadFloat(s_vfxAttributes, base + 27u), rawLoadFloat(s_vfxAttributes, base + 28u), rawLoadFloat(s_vfxAttributes, base + 29u), rawLoadFloat(s_vfxAttributes, base + 30u));
    }

#if defined(VFX_ATTRIBUTE_BUFFER_WRITE)
    void Store_user0(vec4 value)
    {
        rawStoreFloat(s_vfxAttributes, base + 27u, value.x);
        rawStoreFloat(s_vfxAttributes, base + 28u, value.y);
        rawStoreFloat(s_vfxAttributes, base + 29u, value.z);
        rawStoreFloat(s_vfxAttributes, base + 30u, value.w);
    }
#endif
};

#endif
