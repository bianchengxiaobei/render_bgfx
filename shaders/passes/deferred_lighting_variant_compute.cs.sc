#define HBG_VARIANT_MODE 1
#define HBG_VARIANT_COMPUTE 1

#ifndef HBG_VARIANT_INDEX
#define HBG_VARIANT_INDEX 28u
#endif
#ifndef HBG_VARIANT_LIGHT_FLAGS
#define HBG_VARIANT_LIGHT_FLAGS 0xffffffffu
#endif
#ifndef HBG_VARIANT_MATERIAL_FLAGS
#define HBG_VARIANT_MATERIAL_FLAGS 1u
#endif

#if ((HBG_VARIANT_MATERIAL_FLAGS & 0x7eu) != 0)
#define HBG_ADVANCED_MATERIAL 1
#endif

#include "bgfx_compute.sh"

#if BGFX_SHADER_LANGUAGE_HLSL
#undef texture2D
#define texture2D(_sampler, _coord) texture2DLod(_sampler, _coord, 0.0)
#endif

#include "common/common.sc"
#include "lighting/gbuffer_decode.sc"
#if ((HBG_VARIANT_MATERIAL_FLAGS & 0x7eu) != 0)
#include "material/material_gbuffer.sc"
#endif
#include "lighting/light_loop.sc"
#include "include/lit_variants.sc"

SAMPLER2D(s_depthPyramid, 4);
#if ((HBG_VARIANT_MATERIAL_FLAGS & 0x7eu) != 0)
SAMPLER2D(s_gbuffer4, 12);
#endif
SAMPLER2D(s_ssao, 15);
IMAGE2D_WO(s_sceneColorOut, rgba16f, 14);

#define HBG_LIT_TILE_SIZE 16u
#define HBG_LIT_GROUP_SIZE 8u

NUM_THREADS(HBG_LIT_GROUP_SIZE, HBG_LIT_GROUP_SIZE, 1)

void main()
{
    const ivec2 screenSize = ivec2(max(u_pass_lightScreenSize.xy, vec2(1.0, 1.0)));
    const uint tilesX = (uint(screenSize.x) + HBG_LIT_TILE_SIZE - 1u) / HBG_LIT_TILE_SIZE;
    const uint tilesY = (uint(screenSize.y) + HBG_LIT_TILE_SIZE - 1u) / HBG_LIT_TILE_SIZE;
    const uint numTiles = tilesX * tilesY;
    const uint groupId = gl_WorkGroupID.x;
    const uint tileListOffset = groupId / 4u;
    const uint tileListBase = uint(max(u_pass_litVariantParams.y, 0.0) + 0.5);
    if (tileListOffset >= numTiles)
    {
        return;
    }

    const uint tileIndex = s_lightList[tileListBase + HBG_VARIANT_INDEX * numTiles + tileListOffset];
    if (tileIndex == 0xffffffffu)
    {
        return;
    }
    const ivec2 tileCoord = ivec2(int(tileIndex % tilesX), int(tileIndex / tilesX));
    const ivec2 pixel = tileCoord * int(HBG_LIT_TILE_SIZE) +
                        ivec2(int((groupId & 1u) * HBG_LIT_GROUP_SIZE),
                              int(((groupId >> 1u) & 1u) * HBG_LIT_GROUP_SIZE)) +
                        ivec2(gl_LocalInvocationID.xy);
    if (pixel.x >= screenSize.x || pixel.y >= screenSize.y)
    {
        return;
    }

    const vec2 depthUV = vec2((float(pixel.x) + 0.5) / float(screenSize.x),
                              1.0 - (float(pixel.y) + 0.5) / float(screenSize.y));
    const vec2 gbufferUV = vec2(depthUV.x, 1.0 - depthUV.y);
    const float depth = texture2DLod(s_depthPyramid, depthUV, 0.0).r;
    if (depth >= 1.0)
    {
        return;
    }

    const vec4 ndc = vec4(depthUV * vec2(2.0, 2.0) - vec2(1.0, 1.0), depth, 1.0);
    vec4 worldPosition = mul(u_global_invViewProj, ndc);
    worldPosition.xyz /= worldPosition.w;

    SurfaceData surfaceData = hbgDecodeGBufferSurface(gbufferUV);
#if ((HBG_VARIANT_MATERIAL_FLAGS & 0x7eu) != 0)
    surfaceData = hbgDecodeMaterialAdvanced(texture2DLod(s_gbuffer4, gbufferUV, 0.0), surfaceData);
#endif
    if (u_ssao_params2.w > 0.5)
    {
        surfaceData.ambientOcclusion *= hbgSaturate(texture2DLod(s_ssao, depthUV, 0.0).r);
    }

    const vec3 viewWS = u_global_cameraPos.xyz - worldPosition.xyz;
    float shadowVisibility = 1.0;
#if ((HBG_VARIANT_LIGHT_FLAGS & 8u) != 0)
    shadowVisibility = hbgSampleDirectionalShadow(worldPosition.xyz, surfaceData.normalWS);
#endif
    const vec3 color = hbgEvaluateLightLoop(surfaceData, viewWS, worldPosition.xyz, shadowVisibility) +
                       surfaceData.emissive;

    imageStore(s_sceneColorOut, ivec2(pixel), vec4(max(color, vec3(0.0, 0.0, 0.0)), 1.0));
}
