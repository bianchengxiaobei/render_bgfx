$input v_texcoord0

#include "common/common.sc"
#include "common/light_data.sc"

void main()
{
    const vec2 screenSize = max(u_global_screenSize.xy, vec2(1.0, 1.0));
    const float tileSize = max(u_pass_lightListParams.x, 1.0);
    const ivec2 tileCount = ivec2(max(u_pass_lightListParams.yz, vec2(1.0, 1.0)));
    const ivec2 tile = clamp(ivec2(floor((v_texcoord0 * screenSize) / tileSize)), ivec2(0, 0),
                             tileCount - ivec2(1, 1));

    vec3 color = vec3(0.015, 0.015, 0.015);
    if (u_pass_lightListParams.w > 0.5)
    {
        const uint count = hbgLightListCount(tile);
        if (count > 0u)
        {
            const HbgLightData light = hbgLoadLightData(int(hbgLightListIndex(tile, 0)));
            const vec3 lightColor = hbgSaturate(light.colorIntensity.xyz * 0.35);
            const float heat = hbgSaturate(float(count) / 4.0);
            const float energy = hbgSaturate(light.colorIntensity.w * 0.05);
            color = lightColor * (0.35 + 0.65 * heat) + vec3(0.05, 0.35, 0.9) * heat * (0.35 + energy);
        }
    }

    const vec2 grid = fract((v_texcoord0 * screenSize) / tileSize);
    const float gridLine = (grid.x < 0.035 || grid.y < 0.035) ? 1.0 : 0.0;
    color = mix(color, vec3(0.0, 0.32, 0.45), gridLine * 0.7);
    gl_FragColor = vec4(color, 1.0);
}
