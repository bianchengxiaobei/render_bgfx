$input v_texcoord0

#include "common/common.sc"
#include "postprocess/dof.sc"

SAMPLER2D(s_dofDepth, 0);

void main()
{
    const vec2 sourceUv = v_texcoord0;
    const float depth = texture2D(s_dofDepth, sourceUv).r;
    const float linearEyeDepth = hbgDofLinearEyeDepth(depth, sourceUv);
    const float coc = hbgDofNormalizedCoc(linearEyeDepth);
    gl_FragColor = vec4(coc, linearEyeDepth, 0.0, 1.0);
}
