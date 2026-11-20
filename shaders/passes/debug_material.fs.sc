$input v_texcoord0, v_texcoord1, v_worldPos, v_normal, v_tangent, v_bitangent

#include "material/surface_data.sc"

void main()
{
    const vec3 normalWS = normalize(v_normal);
    const float viewSign = gl_FrontFacing ? -1.0 : 1.0;
    const SurfaceData surfaceData =
        hbgMakeSurfaceData(v_worldPos, normalWS, v_tangent, v_bitangent, v_texcoord0, viewSign);

#ifdef ALPHA_CLIP
    if (surfaceData.alpha < u_material_metallicRoughness.w)
    {
        discard;
    }
#endif

#ifdef DEBUG_ROUGHNESS
    const vec3 value = vec3(surfaceData.perceptualRoughness, surfaceData.perceptualRoughness,
                            surfaceData.perceptualRoughness);
#else
    const vec3 value = surfaceData.baseColor;
#endif

    gl_FragColor = vec4(hbgLinearToSrgb(hbgSaturate(value)), 1.0);
}
