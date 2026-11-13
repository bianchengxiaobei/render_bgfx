$input v_texcoord0

#include "common/common.sc"

SAMPLER2D(s_source, 0);

void main()
{
    const vec2 uv = vec2(v_texcoord0.x, 1.0 - v_texcoord0.y);
    const vec2 texel = u_postprocess_screenParams.zw;
    const vec3 center = max(texture2D(s_source, uv).rgb, vec3(0.0, 0.0, 0.0));
    const vec3 left = max(texture2D(s_source, uv - vec2(texel.x, 0.0)).rgb, vec3(0.0, 0.0, 0.0));
    const vec3 right = max(texture2D(s_source, uv + vec2(texel.x, 0.0)).rgb, vec3(0.0, 0.0, 0.0));
    const vec3 up = max(texture2D(s_source, uv - vec2(0.0, texel.y)).rgb, vec3(0.0, 0.0, 0.0));
    const vec3 down = max(texture2D(s_source, uv + vec2(0.0, texel.y)).rgb, vec3(0.0, 0.0, 0.0));
    const vec3 average = (left + right + up + down) * 0.25;
    const vec3 minimum = min(min(center, left), min(min(right, up), down));
    const vec3 maximum = max(max(center, left), max(max(right, up), down));

    const float strength = clamp(u_postprocess_taaParams2.y, 0.0, 1.0);
    const vec3 sharpened = clamp(center + max(center - average, vec3(0.0, 0.0, 0.0)) * strength * 1.5,
                                minimum, maximum);
    gl_FragColor = vec4(sharpened, 1.0);
}
