#ifndef HBG_MATERIAL_GBUFFER_SC
#define HBG_MATERIAL_GBUFFER_SC

#include "common/common.sc"
#include "material/surface_data_types.sc"

float hbgPackMaterialByte(float value)
{
    return floor(hbgSaturate(value) * 255.0 + 0.5);
}

float hbgPackMaterialTriple(float a, float b, float c)
{
    return hbgPackMaterialByte(a) + hbgPackMaterialByte(b) * 256.0 +
           hbgPackMaterialByte(c) * 65536.0;
}

float hbgUnpackMaterialByte(float value, float shift)
{
    return floor(mod(value / shift, 256.0) + 0.5) / 255.0;
}

vec4 hbgEncodeMaterialAdvanced(SurfaceData surfaceData)
{
    return vec4(
        hbgPackMaterialTriple(surfaceData.clearCoatMask, surfaceData.clearCoatRoughness,
                              surfaceData.clearCoatNormalScale),
        hbgPackMaterialTriple(surfaceData.transmissionMask, hbgSaturate(surfaceData.thickness / 10.0),
                              surfaceData.subsurfaceMask),
        hbgPackMaterialTriple(surfaceData.anisotropy * 0.5 + 0.5,
                              surfaceData.anisotropyRotation / (2.0 * HBG_PI),
                              hbgSaturate((surfaceData.ior - 1.0) / 2.0)),
        hbgPackMaterialTriple(surfaceData.iridescenceMask,
                              hbgSaturate(surfaceData.iridescenceThickness / 1000.0),
                              hbgSaturate(surfaceData.waterMask)));
}

SurfaceData hbgDecodeMaterialAdvanced(vec4 packed, SurfaceData surfaceData)
{
    SurfaceData decoded = surfaceData;
    decoded.clearCoatMask = hbgUnpackMaterialByte(packed.x, 1.0);
    decoded.clearCoatRoughness = max(hbgUnpackMaterialByte(packed.x, 256.0), 0.002);
    decoded.clearCoatNormalScale = hbgUnpackMaterialByte(packed.x, 65536.0);
    decoded.transmissionMask = hbgUnpackMaterialByte(packed.y, 1.0);
    decoded.thickness = hbgUnpackMaterialByte(packed.y, 256.0) * 10.0;
    decoded.subsurfaceMask = hbgUnpackMaterialByte(packed.y, 65536.0);
    const float rawAnisotropy = floor(mod(packed.z, 256.0) + 0.5);
    decoded.anisotropy = rawAnisotropy == 128.0 ? 0.0 : (rawAnisotropy / 255.0) * 2.0 - 1.0;
    decoded.anisotropyRotation = hbgUnpackMaterialByte(packed.z, 256.0) * 2.0 * HBG_PI;
    decoded.ior = 1.0 + hbgUnpackMaterialByte(packed.z, 65536.0) * 2.0;
    decoded.iridescenceMask = hbgUnpackMaterialByte(packed.w, 1.0);
    decoded.iridescenceThickness = hbgUnpackMaterialByte(packed.w, 256.0) * 1000.0;
    decoded.waterMask = hbgUnpackMaterialByte(packed.w, 65536.0);

    const vec3 normalWS = normalize(decoded.normalWS);
    const vec3 up = abs(normalWS.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangentWS = normalize(cross(up, normalWS));
    const float angle = decoded.anisotropyRotation;
    tangentWS = normalize(tangentWS * cos(angle) + cross(normalWS, tangentWS) * sin(angle));
    decoded.tangentWS = tangentWS;
    return decoded;
}
#endif // HBG_MATERIAL_GBUFFER_SC