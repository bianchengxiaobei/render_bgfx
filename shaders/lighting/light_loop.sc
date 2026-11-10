#ifndef HBG_LIGHT_LOOP_SC
#define HBG_LIGHT_LOOP_SC

#include "common/common.sc"
#include "common/light_data.sc"
#include "material/lit.sc"
#include "lighting/shadows.sc"

vec3 hbgEvaluateLightData(int index, SurfaceData surfaceData, vec3 viewWS, vec3 worldPosition,
                          float directionalShadowVisibility)
{
    if (index < 0 || index >= 32)
    {
        return vec3(0.0, 0.0, 0.0);
    }

    const HbgLightData light = hbgLoadLightData(index);
    const int lightType = hbgLightType(light.shadowFlags);
    vec3 lightDirection;
    vec3 radiance;
    float visibility = 1.0;

    if (lightType == 0)
    {
        lightDirection = normalize(light.directionCone.xyz);
        radiance = light.colorIntensity.rgb * light.colorIntensity.w;
        if (light.shadowFlags.x >= 0.0)
        {
            visibility = directionalShadowVisibility;
        }
    }
    else
    {
        const vec3 toLight = light.positionRange.xyz - worldPosition;
        const float distanceSq = max(dot(toLight, toLight), 1.0e-4);
        lightDirection = toLight * inversesqrt(distanceSq);
        float attenuation = hbgDistanceAttenuation(distanceSq, light.positionRange.w);
        if (lightType == 2)
        {
            const vec3 fromLight = -lightDirection;
            const float cosTheta = dot(normalize(light.directionCone.xyz), fromLight);
            attenuation *= hbgSpotAttenuation(cosTheta, light.directionCone.w, light.shadowFlags.y);
        }
        if (hbgLightCastShadow(light.shadowFlags) && light.shadowFlags.x >= 0.0)
        {
            visibility = hbgSamplePunctualShadow(int(light.shadowFlags.x + 0.5), lightType,
                                                 light.positionRange.xyz, light.directionCone.xyz,
                                                 worldPosition, surfaceData.normalWS);
        }
        radiance = light.colorIntensity.rgb * light.colorIntensity.w * attenuation;
    }

    const LitResult direct = hbgEvaluateLight(surfaceData, viewWS, lightDirection, radiance, visibility);
    return direct.diffuse + direct.specular;
}

vec3 hbgEvaluateLightLoop(SurfaceData surfaceData, vec3 viewWS, vec3 worldPosition,
                          float directionalShadowVisibility)
{
    vec3 color = hbgEvaluateAmbient(surfaceData, viewWS, u_pass_ambientColor.rgb,
                                    u_pass_ambientColor.w);

    const int lightCount = min(int(u_pass_lightCounts.x + 0.5), 32);
    const int directionalCount = min(int(u_pass_lightCounts.y + 0.5), lightCount);
    for (int index = 0; index < directionalCount; ++index)
    {
        color += hbgEvaluateLightData(index, surfaceData, viewWS, worldPosition,
                                      directionalShadowVisibility);
    }

    const bool useLightList = int(u_pass_lightListParams.w + 0.5) != 0;
    if (useLightList)
    {
        const ivec2 tile = hbgLightTileCoord(worldPosition);
        const int tileLightCount = min(int(hbgLightListCount(tile)), 32);
        for (int offset = 0; offset < tileLightCount; ++offset)
        {
            const int index = int(hbgLightListIndex(tile, offset));
            if (index >= directionalCount && index < lightCount)
            {
                color += hbgEvaluateLightData(index, surfaceData, viewWS, worldPosition, 1.0);
            }
        }
    }
    else
    {
        const int start = directionalCount;
        const int fallbackCount = min(lightCount - start, 32);
        for (int offset = 0; offset < fallbackCount; ++offset)
        {
            color += hbgEvaluateLightData(start + offset, surfaceData, viewWS, worldPosition, 1.0);
        }
    }

    return color;
}

#endif // HBG_LIGHT_LOOP_SC
