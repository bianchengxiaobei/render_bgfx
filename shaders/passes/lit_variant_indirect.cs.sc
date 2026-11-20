#include "bgfx_compute.sh"
#include "common/common.sc"
#include "include/lit_variants.sc"

USAMPLER2D(s_litTileFeatureFlags, 0);
BUFFER_WO(s_litVariantTileList, uint, 1);
#if BGFX_SHADER_LANGUAGE_HLSL
RWBuffer<uint> s_litVariantIndirectArgs : register(u2);
#else
BUFFER_RW(s_litVariantIndirectArgs, uvec4, 2);
#endif

#define HBG_LIT_TILE_SIZE 16u

NUM_THREADS(64, 1, 1)

void main()
{
    const uint tileIndex = gl_GlobalInvocationID.x;
    const ivec2 screenSize = ivec2(max(u_pass_lightScreenSize.xy, vec2(1.0, 1.0)));
    const uint tilesX = (uint(screenSize.x) + HBG_LIT_TILE_SIZE - 1u) / HBG_LIT_TILE_SIZE;
    const uint tilesY = (uint(screenSize.y) + HBG_LIT_TILE_SIZE - 1u) / HBG_LIT_TILE_SIZE;
    const uint numTiles = tilesX * tilesY;
    if (tileIndex >= numTiles)
    {
        return;
    }

    const ivec2 tileCoord = ivec2(int(tileIndex % tilesX), int(tileIndex / tilesX));
    const uint featureFlags = texelFetch(s_litTileFeatureFlags, tileCoord, 0).x;
    const uint tileListBase = uint(max(u_pass_litVariantParams.y, 0.0) + 0.5);
    if ((featureFlags & (HBG_LIT_MATERIAL_STANDARD | HBG_LIT_MATERIAL_SUBSURFACE | HBG_LIT_MATERIAL_TRANSMISSION |
                         HBG_LIT_MATERIAL_ANISOTROPY | HBG_LIT_MATERIAL_IRIDESCENCE | HBG_LIT_MATERIAL_CLEAR_COAT)) == 0u)
    {
        return;
    }

    const uint variant = hbgLitVariantFor(featureFlags);
    uint previousGroupCount = 0u;
#if BGFX_SHADER_LANGUAGE_HLSL
    atomicFetchAndAdd(s_litVariantIndirectArgs[variant * 8u], 4u, previousGroupCount);
#else
    atomicFetchAndAdd(s_litVariantIndirectArgs[variant * 2u + 0u].x, 4u, previousGroupCount);
#endif
    const uint tileOffset = (previousGroupCount - 1u) / 4u;
    if (tileOffset < numTiles)
    {
        s_litVariantTileList[tileListBase + variant * numTiles + tileOffset] = tileIndex;
    }
}
