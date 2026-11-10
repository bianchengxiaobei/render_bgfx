$input v_texcoord0

#include "common/common.sc"

SAMPLER2D(s_depthPyramid, 0);

void main()
{
    const float depth = texture2D(s_depthPyramid, v_texcoord0).x;
    if (depth >= 0.999999)
    {
        gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    const vec2 ndc = v_texcoord0 * 2.0 - 1.0;
    vec4 worldPosition = mul(u_global_invViewProj, vec4(ndc, depth, 1.0));
    worldPosition /= worldPosition.w;
    const vec4 viewPosition = mul(u_global_view, vec4(worldPosition.xyz, 1.0));
    const float linearDepth = max(-viewPosition.z, 0.0);
    const float nearPlane = max(u_debug_depthParams.x, 0.0001);
    const float farPlane = max(u_debug_depthParams.y, nearPlane + 0.0001);
    const float value = sqrt(hbgSaturate((linearDepth - nearPlane) / (farPlane - nearPlane)));
    gl_FragColor = vec4(vec3(value, value, value), 1.0);
}
