$input v_texcoord0

#include "common/common.sc"
#include "postprocess/dof.sc"

SAMPLER2D(s_source, 0);
SAMPLER2D(s_dofGather, 1);
SAMPLER2D(s_dofDepth, 2);

void main()
{
    const vec2 sourceUv = vec2(v_texcoord0.x, 1.0 - v_texcoord0.y);
    const vec3 sharpColor = max(texture2D(s_source, sourceUv).rgb, vec3(0.0, 0.0, 0.0));
    const vec3 blurredColor = max(texture2D(s_dofGather, vec2(v_texcoord0.x, 1.0 - v_texcoord0.y)).rgb, vec3(0.0, 0.0, 0.0));

    const float depth = texture2D(s_dofDepth, v_texcoord0).r;
    const float linearEyeDepth = hbgDofLinearEyeDepth(depth, v_texcoord0);
    const float coc = clamp(hbgDofNormalizedCoc(linearEyeDepth), -1.0, 1.0);
    const float blend = sqrt(hbgSaturate(abs(coc)));

    gl_FragColor = vec4(mix(sharpColor, blurredColor, blend), 1.0);
}
