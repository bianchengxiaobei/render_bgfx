$input v_texcoord0

#include "common/common.sc"

SAMPLER2D(s_shadowAtlas, 1);

void main()
{
    const vec2 screenSize = max(u_global_screenSize.xy, vec2(1.0, 1.0));
    const float aspect = screenSize.x / screenSize.y;
    const vec2 atlasScale = aspect > 1.0 ? vec2(1.0 / aspect, 1.0) : vec2(1.0, aspect);
    const vec2 atlasUV = (v_texcoord0 - vec2(0.5, 0.5)) / atlasScale + vec2(0.5, 0.5);

    if (atlasUV.x < 0.0 || atlasUV.x > 1.0 || atlasUV.y < 0.0 || atlasUV.y > 1.0)
    {
        gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    const float shadowDepth = texture2D(s_shadowAtlas, atlasUV).x;
    const float value = shadowDepth >= 0.999999 ? 0.0 : pow(hbgSaturate(1.0 - shadowDepth), 0.1);
    gl_FragColor = vec4(vec3(value, value, value), 1.0);
}
