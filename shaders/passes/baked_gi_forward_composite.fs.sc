$input v_texcoord0

#include "common/common.sc"

SAMPLER2D(s_bakedIndirect, 13);
SAMPLER2D(s_refractionSourceDepth, 0);
SAMPLER2D(s_refractionPreColor, 1);
uniform vec4 u_bakedGICompositeMode;

void main()
{
    const bool copySource = u_bakedGICompositeMode.x > 1.5;
    const bool combineSource = u_bakedGICompositeMode.x > 2.5;
    const vec2 uv = copySource ? gl_FragCoord.xy * u_global_screenSize.zw
                               : vec2(v_texcoord0.x, 1.0 - v_texcoord0.y);
    const vec4 baked = texture2D(s_bakedIndirect, uv);
    const vec4 preColor = texture2D(s_refractionPreColor, uv);
    const float sourceDepth = texture2D(s_refractionSourceDepth, uv).r;

    vec3 rgb = baked.rgb;
    float alpha = u_bakedGICompositeMode.x < 0.5 ? 0.0 : baked.a;
    if (copySource)
    {
        alpha = u_bakedGICompositeMode.y > 0.5 ? sourceDepth : 1.0;
        if (combineSource)
        {
            rgb = preColor.rgb + baked.rgb * (1.0 - preColor.a);
        }
    }
    gl_FragColor = vec4(rgb, alpha);
}
