#ifndef HBG_SURFACE_DATA_TYPES_SC
#define HBG_SURFACE_DATA_TYPES_SC

#include "common/common.sc"

struct SurfaceData
{
    vec3  baseColor;
    float alpha;
    float metallic;
    float perceptualRoughness;
    float roughness;
    float ambientOcclusion;
    float materialAmbientOcclusion;
    vec3  normalWS;
    vec3  geometricNormalWS;
    vec3  tangentWS;
#ifdef HBG_ADVANCED_MATERIAL
    float clearCoatMask;
    float clearCoatRoughness;
    float clearCoatNormalScale;
    float transmissionMask;
    float thickness;
    float ior;
    float anisotropy;
    float anisotropyRotation;
    float iridescenceMask;
    float iridescenceThickness;
    float subsurfaceMask;
#endif
    vec3  emissive;
    float contactShadowMask;
    float contactShadowFade;
    float lightLayerMask;
    float waterMask;
};

#endif // HBG_SURFACE_DATA_TYPES_SC
