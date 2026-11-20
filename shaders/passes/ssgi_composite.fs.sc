$input v_texcoord0

#include "common/common.sc"
#include "lighting/ssgi_filter.sc"

SAMPLER2D(s_gbuffer0, 1);
SAMPLER2D(s_gbuffer1, 2);
SAMPLER2D(s_ssao, 11);
SAMPLER2D(s_bakedIndirect, 13);

void main()
{
    float centerDepth = 0.0;
    const vec3 filtered = hbgSampleSsgiFiltered(v_texcoord0, centerDepth);
    if (centerDepth >= 0.999999)
    {
        gl_FragColor = vec4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    const vec2 gbufferUV = vec2(v_texcoord0.x, 1.0 - v_texcoord0.y);
    const vec4 gbuffer0 = texture2D(s_gbuffer0, gbufferUV);
    const vec4 gbuffer1 = texture2D(s_gbuffer1, gbufferUV);
    const vec3 diffuseColor = max(gbuffer0.rgb * (1.0 - hbgSaturate(gbuffer1.r)), vec3(0.0, 0.0, 0.0));
    const float ambientOcclusion =
        hbgSaturate(gbuffer1.g) * hbgSaturate(texture2D(s_ssao, v_texcoord0).r);
    const float bakedMask = hbgSaturate(texture2D(s_bakedIndirect, gbufferUV).a);
    const vec3 indirect = filtered * diffuseColor * ambientOcclusion * max(u_ssgi_params.x, 0.0) *
                          (1.0 - bakedMask);
    gl_FragColor = vec4(min(max(indirect, vec3(0.0, 0.0, 0.0)), vec3(8.0, 8.0, 8.0)), 0.0);
}
