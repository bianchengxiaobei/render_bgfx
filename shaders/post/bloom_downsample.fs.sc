$input v_texcoord0

#include "common/common.sc"

SAMPLER2D(s_source, 0);

void main()
{
    vec2 uv = v_texcoord0;
    vec2 texel = u_postprocess_bloomTexelSize.zw;
    vec3 color = vec3(0.0, 0.0, 0.0);

    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            float weight = (x == 0 ? 0.5 : 0.25) * (y == 0 ? 0.5 : 0.25);
            color += texture2D(s_source, uv + vec2(float(x), float(y)) * texel).rgb * weight;
        }
    }

    gl_FragColor = vec4(max(color, vec3(0.0, 0.0, 0.0)), 1.0);
}
