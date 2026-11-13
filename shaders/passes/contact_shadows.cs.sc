#include "bgfx_compute.sh"
#include "common/common.sc"
#include "common/light_data.sc"

SAMPLER2D(s_depthSource, 1);
IMAGE2D_RW(s_contactShadowOut, rgba8, 0);

float hbgContactShadowIgn(vec2 pixel)
{
    return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}

vec2 hbgContactShadowNdc(vec2 uv)
{
    return vec2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
}

vec3 hbgContactShadowWorldPosition(vec2 uv, float rawDepth)
{
    vec4 world = mul(u_global_invViewProj, vec4(hbgContactShadowNdc(uv), rawDepth, 1.0));
    return world.xyz / max(world.w, 1.0e-6);
}

float hbgContactShadowLinearDepth(vec3 worldPosition)
{
    return max(-mul(u_global_view, vec4(worldPosition, 1.0)).z, 0.0);
}

bool hbgContactShadowTrace(vec3 positionWS, vec3 rayDirectionWS, float rayLength, ivec2 pixel, vec2 startScreenUV,
                             out float fade)
{
    fade = 0.0;

    const vec2 screenSize = max(u_global_screenSize.xy, vec2(1.0, 1.0));
    const vec3 rayStartWS = positionWS - positionWS * u_contact_params2.z;
    const vec3 rayEndWS = rayStartWS + rayDirectionWS * rayLength;

    const vec4 startClip = mul(u_global_viewProj, vec4(rayStartWS, 1.0));
    const vec4 endClip = mul(u_global_viewProj, vec4(rayEndWS, 1.0));
    if (startClip.w <= 1.0e-5 || endClip.w <= 1.0e-5)
    {
        return false;
    }

    const vec3 startNDC = startClip.xyz / startClip.w;
    const vec3 endNDC = endClip.xyz / endClip.w;
    const vec2 startUV = startScreenUV;
    const vec2 endUV = vec2(endNDC.x * 0.5 + 0.5, 0.5 - endNDC.y * 0.5);
    const vec2 rayDirUV = endUV - startUV;
    const float rayStartZ = startNDC.z;
    const float rayDirZ = endNDC.z - startNDC.z;

    const int sampleCount = clamp(int(u_contact_params3.x + 0.5), 4, 64);
    const float step = 1.0 / float(sampleCount);
    const float frame = u_global_taaFrameInfo.w > 0.5 ? mod(u_global_taaFrameInfo.z, 8.0) : 0.0;
    const float dither = hbgContactShadowIgn(vec2(pixel) + frame * vec2(5.588238, 5.588238)) - 0.5;

    const float thickness = max(0.004, u_contact_params2.w * 0.1);

    const float firstStep = clamp(floor(float(sampleCount) * 0.3), 1.0, 3.0);
    float t = step * (firstStep + dither);
    bool occluded = false;
    for (int i = 0; i < 64; ++i)
    {
        if (i >= sampleCount || t > 1.0)
        {
            break;
        }

        const vec2 sampleUV = startUV + rayDirUV * t;
        if (any(lessThan(sampleUV, vec2(0.0, 0.0))) || any(greaterThan(sampleUV, vec2(1.0, 1.0))))
        {
            break;
        }

        const ivec2 samplePixel =
            clamp(ivec2(sampleUV * screenSize), ivec2(0, 0), ivec2(screenSize) - ivec2(1, 1));
        const float sampleDepth = texelFetch(s_depthSource, samplePixel, 0).r;
        const float rayDepth = rayStartZ + rayDirZ * t;
        if (sampleDepth < 0.999999 && rayDepth > 0.0 && sampleDepth > rayDepth)
        {
            const vec3 sampleWS = hbgContactShadowWorldPosition(sampleUV, sampleDepth);
            const vec3 rayWS = rayStartWS + rayDirectionWS * (t * rayLength);
            if (length(sampleWS - rayWS) < thickness)
            {
                occluded = true;
                break;
            }
        }

        t += step;
    }

    const vec2 traceEndUV = startUV + rayDirUV * t;
    const vec2 vignette = max(6.0 * abs(traceEndUV) - 5.0, vec2(0.0, 0.0));
    fade = occluded ? hbgSaturate(1.0 - dot(vignette, vignette)) : 0.0;
    return occluded;
}
void hbgContactShadowTraceLight(vec3 positionWS, vec3 rayDirection, float rayLength, ivec2 pixel,
                                 vec2 startScreenUV, HbgLightData light, inout uint mask,
                                 inout float globalFade)
{
    const uint bit = uint(light.shadowFlags.z + 0.5);
    if (bit == 0u)
    {
        return;
    }

    float fade;
    if (hbgContactShadowTrace(positionWS, normalize(rayDirection), rayLength, pixel, startScreenUV, fade))
    {
        mask |= bit;
        globalFade = max(globalFade, fade);
    }
}

void hbgContactShadowStore(ivec2 outPixel, ivec2 fullPixel, bool halfResolution, bool gbufferTarget, vec4 packed)
{
    if (!gbufferTarget)
    {
        imageStore(s_contactShadowOut, outPixel, packed);
        return;
    }

    const int extent = halfResolution ? 2 : 1;
    const vec2 screenSize = max(u_global_screenSize.xy, vec2(1.0, 1.0));
    for (int y = 0; y < 2; ++y)
    {
        for (int x = 0; x < 2; ++x)
        {
            if (x >= extent || y >= extent)
            {
                continue;
            }
            const ivec2 target = fullPixel + ivec2(x, y);
            if (target.x >= int(screenSize.x) || target.y >= int(screenSize.y))
            {
                continue;
            }
            const vec4 previous = imageLoad(s_contactShadowOut, target);
            imageStore(s_contactShadowOut, target, vec4(previous.r, previous.g, packed.b, packed.a));
        }
    }
}
NUM_THREADS(8, 8, 1)

void main()
{
    const vec2 screenSize = max(u_global_screenSize.xy, vec2(1.0, 1.0));
    const bool halfResolution = u_contact_params3.w > 0.5;
    const ivec2 traceSize =
        halfResolution ? ivec2(ceil(screenSize * 0.5)) : ivec2(screenSize);
    const ivec2 outPixel = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(outPixel, traceSize)))
    {
        return;
    }

    const ivec2 fullPixel = halfResolution ? outPixel * 2 : outPixel;
    const bool gbufferTarget = u_contact_params4.x > 0.5;
    hbgContactShadowStore(outPixel, fullPixel, halfResolution, gbufferTarget, vec4(0.0, 0.0, 0.0, 0.0));
    const float rawDepth = texelFetch(s_depthSource, fullPixel, 0).r;
    if (rawDepth >= 0.999999)
    {
        return;
    }

    const vec2 fullUV = (vec2(fullPixel) + vec2(0.5, 0.5)) / screenSize;
    const vec3 worldPosition = hbgContactShadowWorldPosition(fullUV, rawDepth);
    const float linearDepth = hbgContactShadowLinearDepth(worldPosition);
    if (linearDepth < u_contact_params.z || linearDepth > u_contact_params.w ||
        u_contact_params3.y <= 0.0)
    {
        return;
    }

    const float fadeIn = u_contact_params2.y > 1.0e-4
                             ? hbgSaturate((linearDepth - u_contact_params.z) / u_contact_params2.y)
                             : 1.0;
    const float fadeOut = u_contact_params2.x > 1.0e-4
                              ? hbgSaturate((u_contact_params.w - linearDepth) / u_contact_params2.x)
                              : (linearDepth < u_contact_params.w ? 1.0 : 0.0);
    const float distanceFade = fadeIn * fadeOut;
    const float rayLength = max(u_contact_params.x, 0.0) *
                            max(0.5, linearDepth * max(u_contact_params.y, 0.0));

    uint mask = 0u;
    float globalFade = 0.0;
    const int lightCount = min(int(u_pass_lightCounts.x + 0.5), 32);
    const int directionalCount = min(int(u_pass_lightCounts.y + 0.5), lightCount);
    const bool directionalOnly = u_contact_params3.z > 0.5;

    for (int index = 0; index < directionalCount; ++index)
    {
        const HbgLightData light = hbgLoadLightData(index);
        hbgContactShadowTraceLight(worldPosition, light.directionCone.xyz, rayLength, fullPixel, fullUV, light,
                                   mask, globalFade);
    }

    if (!directionalOnly)
    {
        if (u_pass_lightListParams.w > 0.5)
        {
            const int tileSize = max(int(u_pass_lightListParams.x + 0.5), 1);
            const int tileCountX = max(int(u_pass_lightListParams.y + 0.5), 1);
            const int tileCountY = max(int(u_pass_lightListParams.z + 0.5), 1);
            const ivec2 tile = clamp(ivec2(floor(vec2(fullPixel) / float(tileSize))), ivec2(0, 0),
                                     ivec2(tileCountX - 1, tileCountY - 1));
            const int tileLightCount = min(int(hbgLightListCount(tile)), 32);
            for (int offset = 0; offset < tileLightCount; ++offset)
            {
                const int index = int(hbgLightListIndex(tile, offset));
                if (index < directionalCount || index >= lightCount)
                {
                    continue;
                }
                const HbgLightData light = hbgLoadLightData(index);
                hbgContactShadowTraceLight(worldPosition, light.positionRange.xyz - worldPosition, rayLength,
                                           fullPixel, fullUV, light, mask, globalFade);
            }
        }
        else
        {
            for (int index = directionalCount; index < lightCount; ++index)
            {
                const HbgLightData light = hbgLoadLightData(index);
                hbgContactShadowTraceLight(worldPosition, light.positionRange.xyz - worldPosition, rayLength,
                                           fullPixel, fullUV, light, mask, globalFade);
            }
        }
    }

    globalFade = hbgSaturate(globalFade * distanceFade);
    const uint fadeByte = uint(globalFade * 255.0 + 0.5);
    const uint maskByte = mask & 0xFFu;
    const vec4 packed = vec4(0.0, 0.0, float(maskByte) / 255.0, float(fadeByte) / 255.0);

    hbgContactShadowStore(outPixel, fullPixel, halfResolution, gbufferTarget, packed);
}
