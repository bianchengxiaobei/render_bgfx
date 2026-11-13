$input v_texcoord0

#include "common/common.sc"
#include "material/normal_buffer.sc"
#include "lighting/shadows.sc"
#include "lighting/shadow_debug.sc"

SAMPLER2D(s_depthSource, 1);
SAMPLER2D(s_normalBuffer, 0);


void main()
{
    const float rawDepth = texture2D(s_depthSource, v_texcoord0).x;
    if (rawDepth >= 0.999999)
    {
        gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    const vec2 ndc = v_texcoord0 * 2.0 - 1.0;
    vec4 worldPosition = mul(u_global_invViewProj, vec4(ndc, rawDepth, 1.0));
    worldPosition.xyz /= max(worldPosition.w, 1.0e-6);

    const vec2 normalUV = vec2(v_texcoord0.x, u_debug_depthParams.z > 0.5 ? 1.0 - v_texcoord0.y : v_texcoord0.y);
    const vec4 normalBuffer = texture2D(s_normalBuffer, normalUV);
    if (normalBuffer.r == 0.0 && normalBuffer.g == 0.0 && normalBuffer.b == 0.0)
    {
        gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    const vec3 normalWS = hbgDecodeNormalBuffer(normalBuffer);
    const float viewDepth = -mul(u_global_view, vec4(worldPosition.xyz, 1.0)).z;
    const int cascade = hbgShadowDebugCascade(viewDepth);
    if (cascade < 0)
    {
        gl_FragColor = vec4(0.08, 0.08, 0.08, 1.0);
        return;
    }

    const float visibility = hbgSampleDirectionalShadow(worldPosition.xyz, normalWS);
    const vec3 color = hbgShadowDebugCascadeColor(cascade) * visibility;
    gl_FragColor = vec4(color, 1.0);
}
