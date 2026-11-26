$input v_currentClip, v_prevClip, v_texcoord0

#include "common/common.sc"

SAMPLER2D(s_baseColorMap, 0);

void main()
{
    const vec4 baseColor = texture2D(s_baseColorMap, v_texcoord0) * u_material_baseColorFactor;
    if (baseColor.a < 0.001)
    {
        discard;
    }

    const vec2 currentNDC = v_currentClip.xy / v_currentClip.w;
    const vec2 previousNDC = v_prevClip.xy / v_prevClip.w;
    vec2 motionVector = currentNDC - previousNDC;
    motionVector = clamp(motionVector, vec2(-2.0 + 1.0e-4, -2.0 + 1.0e-4),
                         vec2(2.0 - 1.0e-4, 2.0 - 1.0e-4));

    gl_FragColor = vec4(motionVector * 0.5, 0.0, 1.0);
}
