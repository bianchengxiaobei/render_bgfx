$input v_texcoord0

#include "common/common.sc"

SAMPLER2D(s_depthSource, 0);

void main()
{
    const vec2 ndc = v_texcoord0 * 2.0 - 1.0;
    const float depth = texture2D(s_depthSource, v_texcoord0).r;

    if (depth >= 0.999999)
    {
        gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    // bgfx targets D3D11/Vulkan with homogeneousDepth=false, so clip/depth z
    // is in [0, 1]. Reconstruct the world position from the depth buffer using
    // the current view-projection (including TAA jitter when enabled), then
    // project with the non-jittered current and previous matrices.
    vec4 worldPosition = mul(u_global_invViewProj, vec4(ndc, depth, 1.0));
    worldPosition /= worldPosition.w;

    const vec4 currentClip = mul(u_global_nonJitteredViewProj, worldPosition);
    const vec4 previousClip = mul(u_global_prevViewProj, worldPosition);
    const vec2 currentNDC = currentClip.xy / currentClip.w;
    const vec2 previousNDC = previousClip.xy / previousClip.w;
    vec2 motionVector = currentNDC - previousNDC;
    motionVector = clamp(motionVector, vec2(-1.0 + 1.0e-4, -1.0 + 1.0e-4),
                         vec2(1.0 - 1.0e-4, 1.0 - 1.0e-4));

    gl_FragColor = vec4(motionVector * 0.5, 0.0, 1.0);
}
