#include "bgfx_compute.sh"
#include "common/common.sc"
#include "include/lit_variants.sc"

BUFFER_WO(s_litVariantIndirectArgs, uvec4, 1);
BUFFER_WO(s_litVariantTileList, uint, 2);

#define HBG_LIT_TILE_SIZE 16u

NUM_THREADS(64, 1, 1)

void main()
{
    const uint thread = gl_GlobalInvocationID.x;
    const ivec2 screenSize = ivec2(max(u_pass_lightScreenSize.xy, vec2(1.0, 1.0)));
    const uint tilesX = (uint(screenSize.x) + HBG_LIT_TILE_SIZE - 1u) / HBG_LIT_TILE_SIZE;
    const uint tilesY = (uint(screenSize.y) + HBG_LIT_TILE_SIZE - 1u) / HBG_LIT_TILE_SIZE;
    const uint numTiles = tilesX * tilesY;
    const uint tileListBase = uint(max(u_pass_litVariantParams.y, 0.0) + 0.5);
    const uint tileListCount = HBG_LIT_VARIANT_COUNT * numTiles;

    for (uint index = thread; index < tileListCount; index += 64u)
    {
        s_litVariantTileList[tileListBase + index] = 0xffffffffu;
    }

    for (uint variant = thread; variant < HBG_LIT_VARIANT_COUNT; variant += 64u)
    {
        s_litVariantIndirectArgs[variant * 2u + 0u] = uvec4(1u, 1u, 1u, 0u);
        s_litVariantIndirectArgs[variant * 2u + 1u] = uvec4(0u, 0u, 0u, 0u);
    }
}
