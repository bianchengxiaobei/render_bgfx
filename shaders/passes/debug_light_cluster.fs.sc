$input v_texcoord0

#include "common/common.sc"
#include "common/light_data.sc"

SAMPLER2D(s_depthSource, 1);

void main()
{
    const vec2 screenSize = max(u_global_screenSize.xy, vec2(1.0, 1.0));
    const bool clusterEnabled = u_cluster_depthParams.w > 0.5;
    const float tileSize = clusterEnabled ? max(u_cluster_params.x, 1.0) : max(u_pass_lightListParams.x, 1.0);
    const ivec2 tileCount =
        clusterEnabled ? ivec2(max(u_cluster_params.yz, vec2(1.0, 1.0)))
                       : ivec2(max(u_pass_lightListParams.yz, vec2(1.0, 1.0)));
    const ivec2 tile = clamp(ivec2(floor((v_texcoord0 * screenSize) / tileSize)), ivec2(0, 0),
                             tileCount - ivec2(1, 1));

    const float depth = texture2D(s_depthSource, v_texcoord0).x;
    const bool hasDepth = depth < 0.999999;
    int slice = 0;
    float linearDepth = 0.0;
    if (hasDepth)
    {
        const vec2 ndc = v_texcoord0 * 2.0 - 1.0;
        vec4 worldPosition = mul(u_global_invViewProj, vec4(ndc, depth, 1.0));
        worldPosition /= worldPosition.w;
        linearDepth = hbgLinearDepth(worldPosition.xyz);
        slice = hbgClusterSliceForDepth(linearDepth);
    }

    const int fixedSlice = int(floor(u_debug_clusterParams.x + 0.5));
    if (fixedSlice >= 0)
    {
        slice = clamp(fixedSlice, 0, int(HBG_LIGHT_CLUSTER_SLICE_COUNT) - 1);
    }

    vec3 color = vec3(0.015, 0.015, 0.015);
    if (u_cluster_depthParams.w > 0.5)
    {
        const uint count = hbgClusterLightListCount(tile, slice, 0);
        const float heat = hbgSaturate(float(count) / 4.0);
        color = mix(vec3(0.02, 0.02, 0.06), vec3(1.0, 0.45, 0.08), heat);
        if (count > 0u)
        {
            color += vec3(0.0, 0.18, 0.35);
        }
        const float sliceTint = float(slice) / max(HBG_LIGHT_CLUSTER_SLICE_COUNT - 1.0, 1.0);
        color = mix(color, vec3(0.08, 0.18, 0.35), sliceTint * 0.25);
    }
    else if (u_pass_lightListParams.w > 0.5)
    {
        const ivec2 fptlTileCount = ivec2(max(u_pass_lightListParams.yz, vec2(1.0, 1.0)));
        const ivec2 fptlTile = clamp(ivec2(floor((v_texcoord0 * screenSize) / max(u_pass_lightListParams.x, 1.0))),
                                     ivec2(0, 0), fptlTileCount - ivec2(1, 1));
        const uint count = hbgLightListCount(fptlTile, 0);
        if (count > 0u)
        {
            const HbgLightData light = hbgLoadLightData(int(hbgLightListIndex(fptlTile, 0, 0)));
            color = hbgSaturate(light.colorIntensity.xyz * 0.35) + vec3(0.05, 0.35, 0.9);
        }
    }

    const vec2 grid = fract((v_texcoord0 * screenSize) / tileSize);
    const float gridLine = (grid.x < 0.035 || grid.y < 0.035) ? 1.0 : 0.0;
    color = mix(color, vec3(0.0, 0.32, 0.45), gridLine * 0.7);
    gl_FragColor = vec4(color, 1.0);
}
