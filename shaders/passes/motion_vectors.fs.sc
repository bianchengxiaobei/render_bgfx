$input v_currentClip, v_prevClip

#include "common/common.sc"

void main()
{
    const vec2 currentNDC = v_currentClip.xy / v_currentClip.w;
    const vec2 previousNDC = v_prevClip.xy / v_prevClip.w;
    vec2 motionVector = currentNDC - previousNDC;
    motionVector = clamp(motionVector, vec2(-2.0 + 1.0e-4, -2.0 + 1.0e-4),
                         vec2(2.0 - 1.0e-4, 2.0 - 1.0e-4));

    // Match HDRP: motion vectors are stored in NDC 0..1 space.
    gl_FragColor = vec4(motionVector * 0.5, 0.0, 1.0);
}
