$input v_texcoord0

#include "common/common.sc"
#include "postprocess/bloom.sc"
#include "postprocess/color_grading.sc"

SAMPLER2D(s_source, 0);
SAMPLER2D(s_bloom, 2);

void main()
{
    vec3 color = max(texture2D(s_source, v_texcoord0).rgb, vec3(0.0, 0.0, 0.0));

    // Fixed exposure is normally applied by the legacy Tonemap. Uber owns the
    // fixed path as well; automatic/histogram exposure was already applied.
    if (u_postprocess_exposureParams.x < 0.5) {
        color *= max(u_postprocess_exposureParams.y, 0.0);
    }

    // HDRP composites bloom before vignette and color grading.
    if (u_postprocess_bloomParams.z > 0.5) {
        vec3 bloom = max(texture2D(s_bloom, v_texcoord0).rgb, vec3(0.0, 0.0, 0.0));
        vec3 thresholded = hbgBloomThreshold(color, u_postprocess_bloomParams.y);
        color = mix(color, (color - thresholded) + bloom * u_postprocess_bloomTint.xyz,
                    clamp(u_postprocess_bloomParams.x, 0.0, 1.0));
    }

    color = hbgApplyVignette(color, vec2(v_texcoord0.x, 1.0 - v_texcoord0.y));

    // HDRP color grades and tonemaps in a LogC LUT. Post exposure is applied
    // before the LUT and does not affect bloom/DOF.
    color *= max(u_postprocess_gradingParams.x, 0.0);
    vec3 lutColor = hbgSampleColorGradingLut(saturate(hbgLinearToLogC(color)));

    gl_FragColor = vec4(max(lutColor, vec3(0.0, 0.0, 0.0)), 1.0);
}
