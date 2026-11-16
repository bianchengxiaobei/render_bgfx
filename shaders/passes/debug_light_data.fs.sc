#include "common/common.sc"
#include "common/light_data.sc"

void main()
{
    const vec2 screen = max(u_global_screenSize.xy / max(u_global_dynamicResolutionScale.xy, vec2(0.001, 0.001)),
                            vec2(1.0, 1.0));
    const ivec2 cell = clamp(ivec2(floor((gl_FragCoord.xy / screen) * vec2(4.0, 32.0))),
                             ivec2(0, 0), ivec2(3, 31));
    const int lightIndex = cell.y;
    const bool active = lightIndex < hbgLightListCapacity() && float(lightIndex) < u_pass_lightCounts.x;
    const vec4 value = texelFetch(s_lightData, cell, 0);

    vec3 color = vec3(0.025, 0.025, 0.025);
    if (active)
    {
        if (cell.x == 0)
        {
            color = abs(value.xyz) * 0.05;
        }
        else if (cell.x == 1)
        {
            color = value.rgb * max(value.w, 1.0) * 0.15;
        }
        else if (cell.x == 2)
        {
            color = value.xyz * 0.5 + 0.5;
        }
        else
        {
            color = vec3(value.x < 0.0 ? 0.0 : 0.5, value.y * 0.5 + 0.5, value.w * 0.1);
        }
    }

    gl_FragColor = vec4(color, 1.0);
}
