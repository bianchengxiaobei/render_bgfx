$input v_texcoord0

#include "common/common.sc"

SAMPLER2D(s_exposure, 0);
USAMPLER2D(s_histogram, 1);

void main()
{
    vec2 uv = v_texcoord0;
    float exposure = max(texture2D(s_exposure, vec2(0.5, 0.5)).x, 0.0);
    vec3 color = vec3(0.02, 0.02, 0.025);
    color += vec3(exposure * 0.06, exposure * 0.06, exposure * 0.06);

    int bin = int(clamp(uv.x, 0.0, 0.999) * 128.0);
    float count = float(texelFetch(s_histogram, ivec2(bin, 0), 0).x);
    float height = clamp(log2(1.0 + count) / 12.0, 0.0, 1.0);
    if (uv.y > 1.0 - height) {
        color = vec3(0.8, 0.55, 0.2);
    }

    if (uv.x < 0.16 && uv.y < 0.12) {
        color = vec3(exposure * 0.5, exposure * 0.5, exposure * 0.5);
    }
    gl_FragColor = vec4(color, 1.0);
}
