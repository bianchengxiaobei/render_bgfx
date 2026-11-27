#ifndef HBG_SURFACE_DATA_ADVANCED_SC
#define HBG_SURFACE_DATA_ADVANCED_SC

#include "material/surface_data_types.sc"

SurfaceData hbgApplyAdvancedMaterial(SurfaceData surfaceData, vec3 tangentWS, vec3 bitangentWS,
                                       vec3 geometricNormalWS)
{
    SurfaceData result = surfaceData;
    vec3 tangent = tangentWS;
    vec3 bitangent = bitangentWS;
    if (dot(tangent, tangent) < 1.0e-8 || dot(bitangent, bitangent) < 1.0e-8)
    {
        const vec3 up = abs(geometricNormalWS.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
        tangent = normalize(cross(up, geometricNormalWS));
        bitangent = normalize(cross(geometricNormalWS, tangent));
    }
    result.tangentWS = normalize(tangent);
    result.clearCoatMask = hbgSaturate(u_material_advanced0.x);
    result.clearCoatRoughness = clamp(u_material_advanced0.y, 0.002, 1.0);
    result.clearCoatNormalScale = u_material_advanced2.w;
    result.transmissionMask = hbgSaturate(u_material_advanced0.z);
    result.thickness = max(u_material_advanced0.w, 0.0);
    result.anisotropy = clamp(u_material_advanced1.x, -1.0, 1.0);
    result.anisotropyRotation = u_material_advanced1.y;
    result.ior = max(u_material_advanced1.z, 1.0);
    result.iridescenceMask = hbgSaturate(u_material_advanced1.w);
    result.iridescenceThickness = max(u_material_advanced2.x, 0.0);
    result.subsurfaceMask = hbgSaturate(u_material_advanced2.y);
    return result;
}

#endif // HBG_SURFACE_DATA_ADVANCED_SC