$input v_texcoord0

#include "common/common.sc"
#include "common/light_data.sc"
#include "lighting/shadows.sc"

SAMPLER2D(s_depthSource, 2);
SAMPLER2D(s_motionVectors, 3);
SAMPLER2D(s_fogHistory, 4);
SAMPLER2D(s_fogMask0, 10);
SAMPLER2D(s_fogMask1, 11);
SAMPLER2D(s_fogMask2, 12);
SAMPLER2D(s_fogMask3, 13);

vec3 hbgFogWorldPosition(vec2 uv, float rawDepth)
{
    const vec4 clip = vec4(uv * 2.0 - 1.0, rawDepth, 1.0);
    vec4 world = mul(u_global_invViewProj, clip);
    world.xyz /= max(world.w, 1.0e-6);
    return world.xyz;
}

float hbgFogHeightMultiplier(float height)
{
    const float baseHeight = u_fog_params.x;
    const float rcpScaleHeight = max(u_fog_params.y, 1.0e-6);
    return exp(-max(height - baseHeight, 0.0) * rcpScaleHeight);
}

float hbgFogPhase(float anisotropy, float cosTheta)
{
    const float g = anisotropy;
    const float g2 = g * g;
    const float constant = (3.0 / (8.0 * HBG_PI)) * (1.0 - g2) / (2.0 + g2);
    const float symmetric = 1.0 + cosTheta * cosTheta;
    const float x = max(1.0 + g2 - 2.0 * g * cosTheta, 1.0e-4);
    return constant * symmetric * pow(x, -1.5);
}

vec3 hbgFogSampleSH9(vec3 normal)
{
    const vec4 vA = vec4(normal, 1.0);
    vec3 result;
    result.r = dot(u_environment_shAr, vA);
    result.g = dot(u_environment_shAg, vA);
    result.b = dot(u_environment_shAb, vA);

    const vec4 vB = normal.xyzz * normal.yzzx;
    vec3 quadratic;
    quadratic.r = dot(u_environment_shBr, vB);
    quadratic.g = dot(u_environment_shBg, vB);
    quadratic.b = dot(u_environment_shBb, vB);

    const float vC = normal.x * normal.x - normal.y * normal.y;
    result += quadratic + u_environment_shCr.xyz * vC;
    return max(result, vec3(0.0, 0.0, 0.0));
}

float hbgFogHash(vec2 position)
{
    vec2 p = fract(position * vec2(443.897, 441.423));
    p += dot(p, p.yx + 19.19);
    return fract((p.x + p.y) * p.x);
}

vec3 hbgFogPunctualLight(HbgLightData light, vec3 samplePosition, vec3 rayDirection, float anisotropy)
{
    const int lightType = hbgLightType(light.shadowFlags);
    if (lightType != 1 && lightType != 2)
    {
        return vec3(0.0, 0.0, 0.0);
    }

    const vec3 toLight = light.positionRange.xyz - samplePosition;
    const float distanceSq = max(dot(toLight, toLight), 1.0e-4);
    const vec3 lightDirection = toLight * inversesqrt(distanceSq);
    float attenuation = hbgDistanceAttenuation(distanceSq, light.positionRange.w);
    if (lightType == 2)
    {
        const float cosTheta = dot(normalize(light.directionCone.xyz), -lightDirection);
        attenuation *= hbgSpotAttenuation(cosTheta, light.directionCone.w, light.shadowFlags.y);
    }

    float visibility = 1.0;
    if (u_fog_params3.w > 0.5 && hbgLightCastShadow(light.shadowFlags) && light.shadowFlags.x >= 0.0)
    {
        visibility = hbgSamplePunctualShadow(int(light.shadowFlags.x + 0.5), lightType,
                                             light.positionRange.xyz, light.directionCone.xyz,
                                             samplePosition, -rayDirection);
    }

    const float phase = hbgFogPhase(anisotropy, dot(lightDirection, rayDirection));
    return max(light.colorIntensity.rgb, vec3(0.0, 0.0, 0.0)) * light.colorIntensity.w * attenuation *
           visibility * phase;
}

vec3 hbgFogPunctualRadiance(vec3 samplePosition, vec3 rayDirection, float anisotropy)
{
    if (u_fog_params3.y < 0.5)
    {
        return vec3(0.0, 0.0, 0.0);
    }

    const int lightCount = min(int(u_pass_lightCounts.x + 0.5), 32);
    const int directionalCount = min(int(u_pass_lightCounts.y + 0.5), lightCount);
    const int maxLights = clamp(int(u_fog_params5.x + 0.5), 0, 16);
    if (maxLights <= 0)
    {
        return vec3(0.0, 0.0, 0.0);
    }

    vec3 radiance = vec3(0.0, 0.0, 0.0);
    if (u_fog_params5.z > 0.5 && u_cluster_depthParams.w > 0.5)
    {
        const ivec2 tile = hbgLightClusterTileCoord(samplePosition);
        const int slice = hbgClusterSliceForDepth(hbgLinearDepth(samplePosition));
        const int clusterLightCount = min(int(hbgClusterLightListCount(tile, slice)), maxLights);
        for (int offset = 0; offset < 16; ++offset)
        {
            if (offset >= clusterLightCount)
            {
                break;
            }
            const int index = int(hbgClusterLightListIndex(tile, slice, offset));
            if (index >= directionalCount && index < lightCount)
            {
                radiance += hbgFogPunctualLight(hbgLoadLightData(index), samplePosition, rayDirection,
                                                anisotropy);
            }
        }
        return radiance;
    }

    if (u_pass_lightListParams.w > 0.5)
    {
        const ivec2 tile = hbgLightTileCoord(samplePosition);
        const int tileLightCount = min(int(hbgLightListCount(tile)), maxLights);
        for (int offset = 0; offset < 16; ++offset)
        {
            if (offset >= tileLightCount)
            {
                break;
            }
            const int index = int(hbgLightListIndex(tile, offset));
            if (index >= directionalCount && index < lightCount)
            {
                radiance += hbgFogPunctualLight(hbgLoadLightData(index), samplePosition, rayDirection,
                                                anisotropy);
            }
        }
        return radiance;
    }

    const int fallbackCount = min(lightCount - directionalCount, maxLights);
    for (int offset = 0; offset < 16; ++offset)
    {
        if (offset >= fallbackCount)
        {
            break;
        }
        radiance += hbgFogPunctualLight(hbgLoadLightData(directionalCount + offset), samplePosition,
                                        rayDirection, anisotropy);
    }
    return radiance;
}

float hbgFogSampleMask(int slot, vec2 uv)
{
    float mask = 1.0;
    if (slot == 0)
    {
        mask = texture2DLod(s_fogMask0, uv, 0.0).r;
    }
    else if (slot == 1)
    {
        mask = texture2DLod(s_fogMask1, uv, 0.0).r;
    }
    else if (slot == 2)
    {
        mask = texture2DLod(s_fogMask2, uv, 0.0).r;
    }
    else if (slot == 3)
    {
        mask = texture2DLod(s_fogMask3, uv, 0.0).r;
    }
    return mask;
}

float hbgFogLocalFade(int index, vec3 local, float distance)
{
    const vec4 right = u_fog_localObbRight[index];
    const vec4 extents = u_fog_localObbExtents[index];
    const vec4 facePos = u_fog_localFaceFadePos[index];
    const vec4 faceNeg = u_fog_localFaceFadeNeg[index];
    const vec4 distanceFade = u_fog_localDistanceFade[index];
    const bool sphere = right.w > 0.5;
    float fade = 0.0;
    if (sphere)
    {
        const float radius = length(local);
        if (radius > 1.0)
        {
            return 0.0;
        }
        const float edge = clamp(facePos.x, 1.0e-4, 1.0);
        fade = saturate((1.0 - radius) / edge);
    }
    else
    {
        if (max(max(abs(local.x), abs(local.y)), abs(local.z)) > 1.0)
        {
            return 0.0;
        }
        const vec3 coord = local * 0.5 + vec3(0.5, 0.5, 0.5);
        const vec3 positiveFade = saturate((vec3(1.0, 1.0, 1.0) - coord) * facePos.xyz);
        const vec3 negativeFade = saturate(coord * faceNeg.xyz);
        fade = positiveFade.x * positiveFade.y * positiveFade.z * negativeFade.x * negativeFade.y *
               negativeFade.z;
    }

    if (faceNeg.w > 0.5)
    {
        fade = pow(max(fade, 0.0), 2.2);
    }
    if (facePos.w > 0.5)
    {
        fade = 1.0 - fade;
    }
    const float distanceFadeFactor = saturate(distanceFade.y - distance * distanceFade.x);
    return saturate(fade * distanceFadeFactor);
}

vec3 hbgFogLocalScattering(int index, vec3 samplePosition, float distance, out float localExtinction)
{
    const vec4 center = u_fog_localObbCenter[index];
    const vec4 right = u_fog_localObbRight[index];
    const vec4 up = u_fog_localObbUp[index];
    const vec4 extents = u_fog_localObbExtents[index];
    const vec4 scattering = u_fog_localScattering[index];
    const vec4 maskTiling = u_fog_localMaskTiling[index];
    const vec4 maskScroll = u_fog_localMaskScroll[index];

    const vec3 offset = samplePosition - center.xyz;
    const vec3 halfExtents = max(extents.xyz, vec3(1.0e-4, 1.0e-4, 1.0e-4));
    const vec3 forward = cross(right.xyz, up.xyz);
    const vec3 local = vec3(dot(offset, right.xyz) / halfExtents.x,
                            dot(offset, up.xyz) / halfExtents.y,
                            dot(offset, forward) / halfExtents.z);
    const float fade = hbgFogLocalFade(index, local, distance);
    if (fade <= 0.0)
    {
        localExtinction = 0.0;
        return vec3(0.0, 0.0, 0.0);
    }

    vec2 maskUv;
    const int maskPlane = int(maskTiling.w + 0.5);
    if (maskPlane == 0)
    {
        maskUv = local.xy;
    }
    else if (maskPlane == 1)
    {
        maskUv = local.xz;
    }
    else
    {
        maskUv = local.yz;
    }
    maskUv = fract((maskUv * 0.5 + vec2(0.5, 0.5)) * maskTiling.xy + maskScroll.xy);
    float mask = 1.0;
    if (maskTiling.z > 0.0)
    {
        const int maskSlot = int(up.w + 0.5);
        mask = hbgFogSampleMask(maskSlot, maskUv);
        mask = mix(1.0, mask, maskTiling.z);
        if (maskScroll.z > 0.5)
        {
            mask = 1.0 - mask;
        }
        mask = saturate(mask);
    }

    const float density = fade * mask;
    localExtinction = scattering.w * density;
    return scattering.rgb * density;
}

vec3 hbgFogLocalScattering(vec3 samplePosition, float distance, out float extinction)
{
    vec3 scattering = vec3(0.0, 0.0, 0.0);
    extinction = 0.0;
    const int volumeCount = min(int(u_fog_params6.x + 0.5), 8);
    float localExtinction = 0.0;

    if (volumeCount > 0)
    {
        scattering += hbgFogLocalScattering(0, samplePosition, distance, localExtinction);
        extinction += localExtinction;
    }
    if (volumeCount > 1)
    {
        scattering += hbgFogLocalScattering(1, samplePosition, distance, localExtinction);
        extinction += localExtinction;
    }
    if (volumeCount > 2)
    {
        scattering += hbgFogLocalScattering(2, samplePosition, distance, localExtinction);
        extinction += localExtinction;
    }
    if (volumeCount > 3)
    {
        scattering += hbgFogLocalScattering(3, samplePosition, distance, localExtinction);
        extinction += localExtinction;
    }
    if (volumeCount > 4)
    {
        scattering += hbgFogLocalScattering(4, samplePosition, distance, localExtinction);
        extinction += localExtinction;
    }
    if (volumeCount > 5)
    {
        scattering += hbgFogLocalScattering(5, samplePosition, distance, localExtinction);
        extinction += localExtinction;
    }
    if (volumeCount > 6)
    {
        scattering += hbgFogLocalScattering(6, samplePosition, distance, localExtinction);
        extinction += localExtinction;
    }
    if (volumeCount > 7)
    {
        scattering += hbgFogLocalScattering(7, samplePosition, distance, localExtinction);
        extinction += localExtinction;
    }
    return scattering;
}

void main()
{
    const vec2 uv = v_texcoord0;
    const vec2 sourceUv = vec2(uv.x, 1.0 - uv.y);
    const float rawDepth = texture2D(s_depthSource, uv).r;

    const float depthExtent = max(u_fog_params2.x, 0.1);
    const vec3 cameraPosition = u_global_cameraPos.xyz;
    vec3 rayDirection;
    float rayDistance = depthExtent;
    if (rawDepth < 0.999999)
    {
        const vec3 worldPosition = hbgFogWorldPosition(uv, rawDepth);
        const vec3 toWorld = worldPosition - cameraPosition;
        const float worldDistance = length(toWorld);
        rayDistance = min(worldDistance, depthExtent);
        if (worldDistance <= 1.0e-4)
        {
            gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
            return;
        }
        rayDirection = toWorld / worldDistance;
    }
    else
    {
        const vec4 farWorld = mul(u_global_nonJitteredInvViewProj, vec4(uv * 2.0 - 1.0, 1.0, 1.0));
        rayDirection = normalize(farWorld.xyz / max(farWorld.w, 1.0e-6) - cameraPosition);
    }
    const int stepCount = clamp(int(u_fog_params2.y + 0.5), 1, 64);
    const float stepLength = rayDistance / float(stepCount);
    const float baseExtinction = max(u_fog_params.z, 0.0);
    const float anisotropy = clamp(u_fog_params.w, -0.99, 0.99);
    const float globalLightProbeDimmer = max(u_fog_params2.w, 0.0);
    const bool temporal = u_fog_params3.z > 0.5;
    const bool temporalJitter = temporal && u_fog_params5.y > 0.5;
    const float frameIndex = floor(u_global_time.z);
    const float jitter = temporalJitter ? hbgFogHash(gl_FragCoord.xy + frameIndex * 17.0) : 0.5;

    const vec3 lightDirection = normalize(u_pass_lightDirection.xyz);
    const vec3 directionalRadiance = u_pass_lightColor.rgb * max(u_pass_lightDirection.w, 0.0);
    const vec3 ambientRadiance = hbgFogSampleSH9(rayDirection) * globalLightProbeDimmer;
    const float phase = hbgFogPhase(anisotropy, dot(lightDirection, rayDirection));
    const vec3 fogAlbedo = max(u_fog_albedo.rgb, vec3(0.0, 0.0, 0.0));

    vec3 inScattering = vec3(0.0, 0.0, 0.0);
    float transmittance = 1.0;
    for (int stepIndex = 0; stepIndex < 64; ++stepIndex)
    {
        if (stepIndex >= stepCount)
        {
            break;
        }

        const float distance = (float(stepIndex) + jitter) * stepLength;
        const vec3 samplePosition = cameraPosition + rayDirection * distance;
        const float heightMultiplier = hbgFogHeightMultiplier(samplePosition.y);
        const float globalExtinction = baseExtinction * heightMultiplier;
        float localExtinction = 0.0;
        const vec3 localScattering = hbgFogLocalScattering(samplePosition, distance, localExtinction);
        const float extinction = globalExtinction + localExtinction;
        const float opacity = 1.0 - exp(-extinction * stepLength);

        float directionalVisibility = 1.0;
        if (u_fog_params3.x > 0.5)
        {
            directionalVisibility = hbgSampleDirectionalShadowVolumetric(samplePosition, -rayDirection);
        }

        const vec3 directional = directionalRadiance * phase * directionalVisibility;
        const vec3 punctual = hbgFogPunctualRadiance(samplePosition, rayDirection, anisotropy);
        const vec3 radiance = directional + punctual + ambientRadiance;
        const vec3 scatteringCoefficient = fogAlbedo * globalExtinction + localScattering;
        inScattering += transmittance * (scatteringCoefficient / max(extinction, 1.0e-6)) * radiance * opacity;
        transmittance *= 1.0 - opacity;
    }

    if (temporal && u_fog_params4.w > 0.5 && u_fog_params4.z > 0.0)
    {
        const vec2 motion = texture2D(s_motionVectors, sourceUv).xy;
        const vec2 previousUv = sourceUv - vec2(motion.x, -motion.y);
        const bool offScreen = any(lessThan(previousUv, vec2(0.0, 0.0))) ||
                               any(greaterThan(previousUv, vec2(1.0, 1.0)));
        if (!offScreen)
        {
            const vec2 previousDepthUv = vec2(previousUv.x, 1.0 - previousUv.y);
            const float previousDepth = texture2D(s_depthSource, previousDepthUv).r;
            const float depthTolerance = 0.002 + rawDepth * 0.02;
            const float depthWeight = 1.0 - hbgSaturate(abs(rawDepth - previousDepth) / depthTolerance);
            const vec4 history = texture2D(s_fogHistory, previousUv);
            const float historyWeight = clamp(u_fog_params4.z * depthWeight, 0.0, 0.95);
            inScattering = mix(inScattering, max(history.rgb, vec3(0.0, 0.0, 0.0)), historyWeight);
            transmittance = mix(transmittance, clamp(history.a, 0.0, 1.0), historyWeight);
        }
    }

    gl_FragColor = vec4(max(inScattering, vec3(0.0, 0.0, 0.0)), clamp(transmittance, 0.0, 1.0));
}
