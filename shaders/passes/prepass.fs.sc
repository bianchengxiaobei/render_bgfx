$input v_texcoord0, v_texcoord1, v_worldPos, v_normal, v_tangent, v_bitangent

#include "material/surface_data.sc"
#include "material/normal_buffer.sc"

void main()
{
    const vec3 normalWS = normalize(v_normal);
    const float viewSign = gl_FrontFacing ? -1.0 : 1.0;
    const SurfaceData surfaceData = hbgMakeSurfaceData(v_worldPos, normalWS, v_tangent, v_bitangent,
                                                       v_texcoord0, viewSign);

#ifdef ALPHA_CLIP
    if (surfaceData.alpha < u_material_metallicRoughness.w)
    {
        discard;
    }
#endif

    gl_FragColor = hbgEncodeNormalBuffer(surfaceData.normalWS, surfaceData.perceptualRoughness);
}
