$input v_texcoord0

#include "common/common.sc"
#include "lighting/gbuffer_decode.sc"

SAMPLER2D(s_depthDebug, 4);

void main()
{
    const float depth = texture2D(s_depthDebug, v_texcoord0).x;
    if (depth >= 0.999999)
    {
        gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    const vec2 gbufferUV = vec2(v_texcoord0.x, 1.0 - v_texcoord0.y);
    const vec4 gbuffer0 = texture2D(s_gbuffer0, gbufferUV);
    const vec4 gbuffer1 = texture2D(s_gbuffer1, gbufferUV);
    const vec4 gbuffer2 = texture2D(s_gbuffer2, gbufferUV);
    const vec4 gbuffer3 = texture2D(s_gbuffer3, gbufferUV);
    const int mode = int(floor(u_debug_clusterParams.y + 0.5));

    vec3 value = vec3(0.0, 0.0, 0.0);
    if (mode == 0)
    {
        value = hbgSaturate(gbuffer0.rgb);
    }
    else if (mode == 1)
    {
        const float roughness = hbgSaturate(gbuffer2.a);
        value = vec3(roughness, roughness, roughness);
    }
    else if (mode == 2)
    {
        const float metallic = hbgSaturate(gbuffer1.r);
        value = vec3(metallic, metallic, metallic);
    }
    else if (mode == 3)
    {
        const float ambientOcclusion = hbgSaturate(gbuffer1.g);
        value = vec3(ambientOcclusion, ambientOcclusion, ambientOcclusion);
    }
    else if (mode == 4)
    {
        value = hbgSaturate(gbuffer3.rgb);
    }

    gl_FragColor = vec4(hbgLinearToSrgb(value), 1.0);
}
