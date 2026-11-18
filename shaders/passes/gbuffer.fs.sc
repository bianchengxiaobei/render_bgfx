$input v_texcoord0, v_worldPos, v_normal, v_tangent, v_bitangent

#define HBG_ADVANCED_MATERIAL 1

#include "material/surface_data.sc"
#include "material/normal_buffer.sc"
#include "material/material_gbuffer.sc"
#include "material/surface_data_advanced.sc"
#ifdef HAS_DECALS
#include "lighting/decal.sc"
#endif

void main()
{
    const vec3 normalWS = normalize(v_normal);
    const float viewSign = gl_FrontFacing ? -1.0 : 1.0;
    SurfaceData surfaceData = hbgMakeSurfaceData(v_worldPos, normalWS, v_tangent, v_bitangent,
                                                 v_texcoord0, viewSign);
#ifdef HAS_DECALS
    const vec2 screenSize = max(u_global_screenSize.xy, vec2(1.0, 1.0));
    surfaceData = hbgApplyDecals(surfaceData, gl_FragCoord.xy * (vec2(1.0, 1.0) / screenSize));
#endif

    surfaceData = hbgApplyAdvancedMaterial(surfaceData, v_tangent, v_bitangent, normalWS);

    vec4 baseColorOut = vec4(hbgSaturate(surfaceData.baseColor), surfaceData.alpha);
    vec4 emissiveOut = vec4(max(surfaceData.emissive, vec3(0.0, 0.0, 0.0)), 1.0);
#ifdef HAS_LIGHT_LAYERS
    const float layerMask = floor(u_object_lightLayerMask.x + 0.5);
    baseColorOut.a = mod(layerMask, 256.0) / 255.0;
    emissiveOut.a = floor(layerMask / 256.0) / 255.0;
#endif
    gl_FragData[0] = baseColorOut;
    gl_FragData[1] = vec4(hbgSaturate(surfaceData.metallic), hbgSaturate(surfaceData.ambientOcclusion), 0.0, 0.0);
    gl_FragData[2] = hbgEncodeNormalBuffer(surfaceData.normalWS, surfaceData.perceptualRoughness);
    gl_FragData[3] = emissiveOut;
    gl_FragData[4] = hbgEncodeMaterialAdvanced(surfaceData);
}
