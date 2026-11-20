#ifndef HBG_BAKED_GI_SC
#define HBG_BAKED_GI_SC

#include "common/common.sc"

SAMPLER2DARRAY(s_lightmapSH, 15);

vec3 hbgDecodeBakedGI(vec4 incident, vec4 direction, vec3 normalWS)
{
    const float halfLambert = dot(normalWS, direction.xyz - 0.5) + 0.5;
    const float directionality = max(direction.w, 1.0e-4);
    return max(incident.rgb * halfLambert / directionality, vec3(0.0, 0.0, 0.0));
}

vec3 hbgSampleBakedGI(vec2 lightmapUV, vec3 normalWS)
{
    const float layerBase = u_object_lightmapParams.y;
    const vec4 incident = texture2DArray(s_lightmapSH, vec3(lightmapUV, layerBase));
    const vec4 direction = texture2DArray(s_lightmapSH, vec3(lightmapUV, layerBase + 1.0));
    return hbgDecodeBakedGI(incident, direction, normalWS) * u_pass_lightmapParams2.x;
}

#endif // HBG_BAKED_GI_SC
