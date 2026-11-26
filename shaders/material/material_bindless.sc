#ifndef HBG_MATERIAL_BINDLESS_SC
#define HBG_MATERIAL_BINDLESS_SC

#include "common/common.sc"

#define HBG_BINDLESS_MATERIAL_LIMIT 32
#define HBG_BINDLESS_MATERIAL_STRIDE 12
uniform vec4 u_bindlessMaterialData[HBG_BINDLESS_MATERIAL_LIMIT * HBG_BINDLESS_MATERIAL_STRIDE];

int hbgMaterialBase(int materialIndex)
{
    return materialIndex * HBG_BINDLESS_MATERIAL_STRIDE;
}

vec4 hbgMaterialBaseColorFactor(int materialIndex)
{
    return u_bindlessMaterialData[hbgMaterialBase(materialIndex) + 0];
}

vec4 hbgMaterialMetallicRoughness(int materialIndex)
{
    return u_bindlessMaterialData[hbgMaterialBase(materialIndex) + 1];
}

vec4 hbgMaterialEmissiveFactor(int materialIndex)
{
    return u_bindlessMaterialData[hbgMaterialBase(materialIndex) + 2];
}

vec4 hbgMaterialAdvanced0(int materialIndex)
{
    return u_bindlessMaterialData[hbgMaterialBase(materialIndex) + 3];
}

vec4 hbgMaterialAdvanced1(int materialIndex)
{
    return u_bindlessMaterialData[hbgMaterialBase(materialIndex) + 4];
}

vec4 hbgMaterialAdvanced2(int materialIndex)
{
    return u_bindlessMaterialData[hbgMaterialBase(materialIndex) + 5];
}

vec4 hbgMaterialBindlessFlags(int materialIndex)
{
    return u_bindlessMaterialData[hbgMaterialBase(materialIndex) + 6];
}

vec4 hbgMaterialUvScale0(int materialIndex)
{
    return u_bindlessMaterialData[hbgMaterialBase(materialIndex) + 7];
}

vec4 hbgMaterialUvScale1(int materialIndex)
{
    return u_bindlessMaterialData[hbgMaterialBase(materialIndex) + 8];
}

vec4 hbgMaterialUvScale2(int materialIndex)
{
    return u_bindlessMaterialData[hbgMaterialBase(materialIndex) + 9];
}

vec4 hbgMaterialUvScale3(int materialIndex)
{
    return u_bindlessMaterialData[hbgMaterialBase(materialIndex) + 10];
}

vec4 hbgMaterialUvScale4(int materialIndex)
{
    return u_bindlessMaterialData[hbgMaterialBase(materialIndex) + 11];
}

#endif // HBG_MATERIAL_BINDLESS_SC
