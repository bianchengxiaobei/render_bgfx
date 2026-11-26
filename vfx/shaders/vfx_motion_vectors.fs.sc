$input v_uv, v_currentClip, v_prevClip

#include "bgfx_shader.sh"

SAMPLER2D(s_vfxMain, 0);
uniform vec4 u_vfxMotionParams;

void main()
{
    if (u_vfxMotionParams.y > 0.5 && texture2D(s_vfxMain, v_uv).a < u_vfxMotionParams.x)
    {
        discard;
    }

    const vec2 currentNDC = v_currentClip.xy / max(v_currentClip.w, 1.0e-6);
    const vec2 previousNDC = v_prevClip.xy / max(v_prevClip.w, 1.0e-6);
    vec2 motionVector = currentNDC - previousNDC;
    motionVector = clamp(motionVector, vec2(-2.0 + 1.0e-4, -2.0 + 1.0e-4),
                         vec2(2.0 - 1.0e-4, 2.0 - 1.0e-4));
    gl_FragColor = vec4(motionVector * 0.5, 0.0, 1.0);
}
