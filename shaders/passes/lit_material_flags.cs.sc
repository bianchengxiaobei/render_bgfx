#include "bgfx_compute.sh"
#include "common/common.sc"
#include "include/lit_variants.sc"

SAMPLER2D(s_litGbuffer4, 1);
SAMPLER2D(s_litDepth, 2);
UIMAGE2D_WO(s_litTileFeatureFlagsOut, r32ui, 0);

#define HBG_LIT_TILE_SIZE 16u
#define HBG_LIT_TILE_THREADS 64u

SHARED uint s_litTileFeatureFlags;

NUM_THREADS(HBG_LIT_TILE_THREADS, 1, 1)

void main()
{
    const ivec2 tile = ivec2(gl_WorkGroupID.xy);
    const ivec2 tileOrigin = tile * int(HBG_LIT_TILE_SIZE);
    const ivec2 screenSize = ivec2(max(u_pass_lightScreenSize.xy, vec2(1.0, 1.0)));
    const uint baseLightFlags = uint(max(u_pass_litVariantParams.x, 0.0) + 0.5);

    if (gl_LocalInvocationIndex == 0)
    {
        s_litTileFeatureFlags = 0u;
    }
    barrier();

    uint materialFlags = 0u;
    for (uint i = 0u; i < 4u; ++i)
    {
        const uint index = i * HBG_LIT_TILE_THREADS + gl_LocalInvocationIndex;
        const ivec2 pixel = min(tileOrigin + ivec2(int(index & 15u), int(index >> 4u)),
                                screenSize - ivec2(1, 1));
        const vec2 depthUV = vec2((float(pixel.x) + 0.5) / float(screenSize.x),
                                  1.0 - (float(pixel.y) + 0.5) / float(screenSize.y));
        if (texture2DLod(s_litDepth, depthUV, 0.0).r < 1.0)
        {
            const vec2 gbufferUV = vec2(depthUV.x, 1.0 - depthUV.y);
            const vec4 advanced = texture2DLod(s_litGbuffer4, gbufferUV, 0.0);
            if (advanced.w > 0.5)
            {
                const uint features = uint(floor(advanced.w / 65536.0 + 0.5));
                materialFlags |= hbgLitMaterialFlagsFromFeatures(features);
            }
        }
    }

    atomicOr(s_litTileFeatureFlags, materialFlags);
    barrier();

    if (gl_LocalInvocationIndex == 0)
    {
        const uint featureFlags = s_litTileFeatureFlags != 0u ? (s_litTileFeatureFlags | baseLightFlags) : 0u;
        imageStore(s_litTileFeatureFlagsOut, tile, uvec4(featureFlags, 0u, 0u, 0u));
    }
}
