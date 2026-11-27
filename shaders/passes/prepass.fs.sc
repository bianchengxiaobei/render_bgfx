$input v_texcoord0, v_worldPos, v_normal, v_tangent, v_bitangent

#define HBG_RIVER_PARTICLES 1

#include "material/surface_data.sc"
#include "material/normal_buffer.sc"
#include "material/river_particles.sc"

void main()
{
    const vec3 normalWS = normalize(v_normal);
    const float viewSign = gl_FrontFacing ? -1.0 : 1.0;
    SurfaceData surfaceData = hbgMakeSurfaceData(v_worldPos, normalWS, v_tangent, v_bitangent,
                                                 v_texcoord0, viewSign);
    surfaceData.normalWS = hbgRiverParticleNormalWS(v_worldPos, surfaceData.normalWS, surfaceData.geometricNormalWS,
                                                    v_tangent, v_bitangent, viewSign);

#ifdef ALPHA_CLIP
    if (surfaceData.alpha < u_material_metallicRoughness.w)
    {
        discard;
    }
#endif

    gl_FragColor = hbgEncodeNormalBuffer(surfaceData.normalWS, surfaceData.perceptualRoughness);
}
