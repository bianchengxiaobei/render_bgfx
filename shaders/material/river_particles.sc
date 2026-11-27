#ifndef HBG_RIVER_PARTICLES_SC
#define HBG_RIVER_PARTICLES_SC

#include "common/common.sc"
#include "common/packing.sc"

#if defined(HBG_RIVER_PARTICLES) && defined(HBG_RIVER_PARTICLE_STAGE14)
SAMPLER2D(s_riverParticleNormal, 14);
#elif defined(HBG_RIVER_PARTICLES)
SAMPLER2D(s_riverParticleNormal, 8);
#endif

vec3 hbgRiverParticleNormalWS(vec3 worldPos, vec3 baseNormalWS, vec3 geometricNormalWS, vec3 tangentWS,
                              vec3 bitangentWS, float viewSign)
{
#ifndef HBG_RIVER_PARTICLES
    return baseNormalWS;
#else
    if (u_material_riverParticleParams.x <= 0.5)
    {
        return baseNormalWS;
    }
    const vec4 clip = mul(u_global_viewProj, vec4(worldPos, 1.0));
    const vec2 screenUV = clip.xy / max(clip.w, 1.0e-6) * vec2(0.5, -0.5) + vec2(0.5, 0.5);
    const vec4 particle = texture2D(s_riverParticleNormal, screenUV);
    if (particle.b <= 1.0e-6)
    {
        return baseNormalWS;
    }
    const vec2 slopes = particle.rg / sqrt(particle.b);
    const vec3 normalTS = hbgUnpackSlopeNormal(slopes, max(u_material_riverParticleParams.y, 0.0));
    vec3 normalWS = geometricNormalWS;
#ifdef DOUBLE_SIDED
    if (viewSign < 0.0)
    {
        normalWS = -normalWS;
    }
#endif
    return normalize(mul(mtxFromCols(tangentWS, bitangentWS, normalWS), normalTS));
#endif
}

#endif // HBG_RIVER_PARTICLES_SC
