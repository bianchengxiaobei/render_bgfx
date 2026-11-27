$input v_texcoord0

#include "common/common.sc"

SAMPLER2D(s_source, 0);

void main()
{
    vec2 step = vec2(u_postprocess_bloomTexelSize.z, 0.0);
#ifdef BLOOM_VERTICAL
    step = vec2(0.0, u_postprocess_bloomTexelSize.w);
#endif

    vec2 uv = v_texcoord0;
    vec3 color = texture2D(s_source, uv).rgb * 0.2734375;
    color += (texture2D(s_source, uv + step).rgb + texture2D(s_source, uv - step).rgb) * 0.2187500;
    color += (texture2D(s_source, uv + step * 2.0).rgb + texture2D(s_source, uv - step * 2.0).rgb) * 0.1093750;
    color += (texture2D(s_source, uv + step * 3.0).rgb + texture2D(s_source, uv - step * 3.0).rgb) * 0.0312500;
    color += (texture2D(s_source, uv + step * 4.0).rgb + texture2D(s_source, uv - step * 4.0).rgb) * 0.00390625;

    gl_FragColor = vec4(max(color, vec3(0.0, 0.0, 0.0)), 1.0);
}
