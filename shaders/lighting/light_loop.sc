#ifndef HBG_LIGHT_LOOP_SC
#define HBG_LIGHT_LOOP_SC

#include "common/common.sc"
#include "common/light_data.sc"
#include "lighting/direct_lighting.sc"
#include "lighting/area_lighting.sc"
#include "lighting/shadows.sc"
#include "lighting/contact_shadow.sc"

#ifndef HBG_VARIANT_LIGHT_FLAGS
#define HBG_VARIANT_LIGHT_FLAGS 0xffffffffu
#endif
#ifndef HBG_VARIANT_MATERIAL_FLAGS
#define HBG_VARIANT_MATERIAL_FLAGS 1u
#endif
#define HBG_VARIANT_HAS_SHADOWS ((HBG_VARIANT_LIGHT_FLAGS & 8u) != 0u)

vec3 hbgApplyAreaShadow(HbgLightData light, vec3 diffuse, vec3 specular, vec3 worldPosition,
                         vec3 normalWS)
{
    float visibility = 1.0;
    const float shadowDimmer = hbgSaturate(light.shadowTintDimmer.w);
#if HBG_VARIANT_HAS_SHADOWS
    if (u_pass_lightListParams2.w > 0.5 && hbgLightCastShadow(light.shadowFlags) &&
        light.shadowFlags.x >= 0.0 && shadowDimmer > 0.0)
    {
        visibility = mix(1.0,
                         hbgSampleAreaShadow(int(light.shadowFlags.x + 0.5), worldPosition, normalWS),
                         shadowDimmer);
    }
#endif
    const vec3 shadowColor = hbgComputeShadowColor(visibility, light.shadowTintDimmer.rgb,
                                                   light.shadowFadeFlags.y);
    return (diffuse + specular) * shadowColor;
}

vec3 hbgEvaluateAreaLightData(int index, HbgLightData light, SurfaceData surfaceData, vec3 viewWS,
                              vec3 worldPosition)
{
    const int lightType = hbgLightType(light.shadowFlags);
    if (lightType < 3)
    {
        return vec3(0.0, 0.0, 0.0);
    }

    const vec3 unL = light.positionRange.xyz - worldPosition;
    const float distanceSq = max(dot(unL, unL), 1.0e-4);
    const float attenuation = hbgDistanceAttenuation(distanceSq, light.positionRange.w);
    if (attenuation <= 0.0 || light.colorIntensity.w <= 0.0)
    {
        return vec3(0.0, 0.0, 0.0);
    }

    const vec3 forward = normalize(light.directionCone.xyz);
    if (lightType == 3 && dot(unL, forward) >= 0.0)
    {
        return vec3(0.0, 0.0, 0.0);
    }
    const float rangeAttenuation = attenuation * light.colorIntensity.w;

    vec3 normalWS = normalize(surfaceData.normalWS);
    const vec3 viewDirection = normalize(viewWS);
    const float ndotV = max(dot(normalWS, viewDirection), 1.0e-4);
    vec3 tangent = viewDirection - normalWS * ndotV;
    if (dot(tangent, tangent) < 1.0e-6)
    {
        tangent = cross(normalWS, abs(normalWS.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0));
    }
    tangent = normalize(tangent);
    const vec3 bitangent = normalize(cross(normalWS, tangent));

    const vec3 center = vec3(dot(unL, tangent), dot(unL, bitangent), dot(unL, normalWS));
    const vec3 right = vec3(dot(light.rightHalfWidth.xyz, tangent), dot(light.rightHalfWidth.xyz, bitangent),
                            dot(light.rightHalfWidth.xyz, normalWS));
    const vec3 up = vec3(dot(light.upHalfHeight.xyz, tangent), dot(light.upHalfHeight.xyz, bitangent),
                         dot(light.upHalfHeight.xyz, normalWS));
    const float halfWidth = light.rightHalfWidth.w;
    const float halfHeight = light.upHalfHeight.w;
    if (halfWidth <= 0.0 && halfHeight <= 0.0)
    {
        return vec3(0.0, 0.0, 0.0);
    }

    const int cookieMode = int(light.cookieInfo.x + 0.5);
    const vec2 cookieScale = light.cookieTransform.xy;
    const vec2 cookieOffset = light.cookieTransform.zw;
    const float perceptualRoughness = surfaceData.perceptualRoughness;

    // HDRP evaluates Tube lights through the line branch of EvaluateLTC_Area:
    // identity LTC for diffuse, GGX LTC for specular, and no area cookie.
    if (lightType == 4)
    {
        const vec4 ltcMatrix = hbgSampleLtcGgx(perceptualRoughness, ndotV);
        const float diffuseIrradiance = hbgLineDiffuseIrradiance(center, right, halfWidth);
        const float specularIrradiance = hbgLtcLineIrradiance(ltcMatrix, center, right, halfWidth);
        const vec3 radiance = light.colorIntensity.rgb * rangeAttenuation;
        const vec3 diffuseColor = surfaceData.baseColor * (1.0 - surfaceData.metallic);
        vec3 f0 = mix(vec3(0.04, 0.04, 0.04), surfaceData.baseColor, surfaceData.metallic);
        vec3 diffuse = diffuseColor * diffuseIrradiance * radiance;
        vec3 specular = hbgF_Schlick(f0, ndotV) * specularIrradiance * radiance;
#if (HBG_VARIANT_MATERIAL_FLAGS & 32)
        if (surfaceData.iridescenceMask > 0.0)
        {
            f0 = hbgAdvancedFresnel0(surfaceData, ndotV);
            specular = hbgF_Schlick(f0, ndotV) * specularIrradiance * radiance;
        }
#endif
#if (HBG_VARIANT_MATERIAL_FLAGS & 64)
        const float coatMask = hbgSaturate(surfaceData.clearCoatMask);
        if (coatMask > 0.0)
        {
            const float coatPerceptualRoughness =
                sqrt(clamp(surfaceData.clearCoatRoughness, 0.002, 1.0));
            const vec4 coatLtc = hbgSampleLtcGgx(coatPerceptualRoughness, ndotV);
            const float coatIrradiance = hbgLtcLineIrradiance(coatLtc, center, right, halfWidth);
            const float coatFresnel = 0.04 + 0.96 * pow(1.0 - ndotV, 5.0);
            const float coatWeight = coatMask * coatFresnel;
            diffuse *= 1.0 - coatWeight * 0.5;
            specular = specular * (1.0 - coatWeight) + radiance * coatIrradiance * coatWeight;
        }
#endif
#if (HBG_VARIANT_MATERIAL_FLAGS & 8)
        const float transmissionMask = hbgSaturate(surfaceData.transmissionMask);
        if (transmissionMask > 0.0)
        {
            const float backLight = hbgSaturate(dot(-normalWS, normalize(unL)));
            const float transmittance = exp(-max(surfaceData.thickness, 0.0) * 0.15);
            diffuse += surfaceData.baseColor * transmissionMask * backLight * transmittance *
                       radiance * 0.5;
        }
#endif
#if (HBG_VARIANT_MATERIAL_FLAGS & 4)
        const float subsurfaceMask = hbgSaturate(surfaceData.subsurfaceMask);
        if (subsurfaceMask > 0.0)
        {
            diffuse += surfaceData.baseColor * subsurfaceMask * diffuseIrradiance * radiance * 0.25;
        }
#endif
        return hbgApplyAreaShadow(light, diffuse, specular, worldPosition, normalWS);
    }

    // Disc has no real-time HDRP raster path; do not render it as a rectangle.
    if (lightType == 5)
    {
        return vec3(0.0, 0.0, 0.0);
    }

    const vec3 v0 = center - halfWidth * right - halfHeight * up;
    const vec3 v1 = v0 + (2.0 * halfHeight) * up;
    const vec3 v2 = v1 + (2.0 * halfWidth) * right;
    const vec3 v3 = v2 - (2.0 * halfHeight) * up;
    const vec3 formFactorDiffuse = hbgPolygonFormFactor(v0, v1, v2, v3);
    const float diffuseIrradiance = hbgPolygonIrradiance(formFactorDiffuse);
    const vec4 ltcMatrix = hbgSampleLtcGgx(perceptualRoughness, ndotV);
    const vec3 formFactorSpecular =
        hbgLtcRectFormFactor(ltcMatrix, center, right, up, halfWidth, halfHeight);
    const float specularIrradiance = hbgPolygonIrradiance(formFactorSpecular);

    vec3 cookie = vec3(1.0, 1.0, 1.0);
    if (cookieMode != HBG_LIGHT_COOKIE_MODE_NONE)
    {
        cookie = hbgLtcRectCookie(v0, v1, v2, v3, formFactorDiffuse, index, cookieMode, cookieScale,
                                  cookieOffset, perceptualRoughness);
    }

    const vec3 radiance = light.colorIntensity.rgb * rangeAttenuation * cookie;
    const vec3 diffuseColor = surfaceData.baseColor * (1.0 - surfaceData.metallic);
    vec3 f0 = mix(vec3(0.04, 0.04, 0.04), surfaceData.baseColor, surfaceData.metallic);
    vec3 diffuse = diffuseColor * diffuseIrradiance * radiance * HBG_INV_PI;
    vec3 specular = hbgF_Schlick(f0, ndotV) * specularIrradiance * radiance;
#if (HBG_VARIANT_MATERIAL_FLAGS & 32)
    if (surfaceData.iridescenceMask > 0.0)
    {
        f0 = hbgAdvancedFresnel0(surfaceData, ndotV);
        specular = hbgF_Schlick(f0, ndotV) * specularIrradiance * radiance;
    }
#endif
#if (HBG_VARIANT_MATERIAL_FLAGS & 64)
    const float coatMask = hbgSaturate(surfaceData.clearCoatMask);
    if (coatMask > 0.0)
    {
        const float coatPerceptualRoughness =
            sqrt(clamp(surfaceData.clearCoatRoughness, 0.002, 1.0));
        const vec4 coatLtc = hbgSampleLtcGgx(coatPerceptualRoughness, ndotV);
        const vec3 coatFormFactor =
            hbgLtcRectFormFactor(coatLtc, center, right, up, halfWidth, halfHeight);
        const float coatIrradiance = hbgPolygonIrradiance(coatFormFactor);
        const float coatFresnel = 0.04 + 0.96 * pow(1.0 - ndotV, 5.0);
        const float coatWeight = coatMask * coatFresnel;
        diffuse *= 1.0 - coatWeight * 0.5;
        specular = specular * (1.0 - coatWeight) + radiance * coatIrradiance * coatWeight;
    }
#endif
#if (HBG_VARIANT_MATERIAL_FLAGS & 8)
    const float transmissionMask = hbgSaturate(surfaceData.transmissionMask);
    if (transmissionMask > 0.0)
    {
        const vec3 flipCenter = vec3(center.x, -center.y, -center.z);
        const vec3 flipRight = vec3(right.x, -right.y, -right.z);
        const vec3 flipUp = vec3(up.x, -up.y, -up.z);
        const vec3 t0 = flipCenter - halfWidth * flipRight - halfHeight * flipUp;
        const vec3 t1 = t0 + (2.0 * halfHeight) * flipUp;
        const vec3 t2 = t1 + (2.0 * halfWidth) * flipRight;
        const vec3 t3 = t2 - (2.0 * halfHeight) * flipUp;
        const float transmittance = exp(-max(surfaceData.thickness, 0.0) * 0.15);
        diffuse += surfaceData.baseColor * transmissionMask * transmittance *
                   hbgPolygonIrradiance(hbgPolygonFormFactor(t0, t1, t2, t3)) * radiance * 0.5;
    }
#endif
#if (HBG_VARIANT_MATERIAL_FLAGS & 4)
    const float subsurfaceMask = hbgSaturate(surfaceData.subsurfaceMask);
    if (subsurfaceMask > 0.0)
    {
        diffuse += surfaceData.baseColor * subsurfaceMask * diffuseIrradiance * radiance *
                   HBG_INV_PI * 0.25;
    }
#endif
    return hbgApplyAreaShadow(light, diffuse, specular, worldPosition, normalWS);
}

vec3 hbgEvaluateLightData(int index, SurfaceData surfaceData, vec3 viewWS, vec3 worldPosition,
                          float directionalShadowVisibility)
{
    if (index < 0 || index >= hbgLightListCapacity())
    {
        return vec3(0.0, 0.0, 0.0);
    }

    const HbgLightData light = hbgLoadLightData(index);
    if (u_object_lightLayerMask.w > 0.5 &&
        (int(light.layerCategoryFlags.x + 0.5) & int(surfaceData.lightLayerMask + 0.5)) == 0)
    {
        return vec3(0.0, 0.0, 0.0);
    }
    const int lightType = hbgLightType(light.shadowFlags);
    if (lightType >= 3)
    {
        return hbgEvaluateAreaLightData(index, light, surfaceData, viewWS, worldPosition);
    }
    const float shadowDimmer = hbgSaturate(light.shadowTintDimmer.w);
    vec3 lightDirection;
    vec3 radiance;
    float visibility = 1.0;

    if (lightType == 0)
    {
        lightDirection = normalize(light.directionCone.xyz);
        radiance = light.colorIntensity.rgb * light.colorIntensity.w;
        const int cookieMode = int(light.cookieInfo.x + 0.5);
        if (cookieMode != HBG_LIGHT_COOKIE_MODE_NONE &&
            int(light.cookieInfo.y + 0.5) == HBG_LIGHT_COOKIE_TYPE_2D)
        {
            const vec2 cookieUV = vec2(dot(worldPosition, light.rightHalfWidth.xyz),
                                       dot(worldPosition, light.upHalfHeight.xyz)) * 0.1 + vec2(0.5, 0.5);
            radiance *= hbgSampleLightCookieScaled(index, cookieUV, cookieMode,
                                                   light.cookieTransform.xy, light.cookieTransform.zw);
        }
#if HBG_VARIANT_HAS_SHADOWS
        if (light.shadowFlags.x >= 0.0 && shadowDimmer > 0.0)
        {
            visibility = mix(1.0, directionalShadowVisibility, shadowDimmer);
        }
#endif
        if (u_pass_shadowParams.y > 0.0)
        {
            const float NdotL = dot(normalize(surfaceData.normalWS), lightDirection);
            visibility *= hbgComputeMicroShadowing(surfaceData.materialAmbientOcclusion, NdotL,
                                                   u_pass_shadowParams.y);
        }
    }
    else
    {
        const vec3 toLight = light.positionRange.xyz - worldPosition;
        const float distanceSq = max(dot(toLight, toLight), 1.0e-4);
        lightDirection = toLight * inversesqrt(distanceSq);
        float attenuation = hbgDistanceAttenuation(distanceSq, light.positionRange.w);
        const int cookieMode = int(light.cookieInfo.x + 0.5);
        const vec2 cookieScale = light.cookieTransform.xy;
        const vec2 cookieOffset = light.cookieTransform.zw;
        if (lightType == 2)
        {
            const vec3 fromLight = normalize(-lightDirection);
            const float cosTheta = dot(normalize(light.directionCone.xyz), fromLight);
            attenuation *= hbgSpotAttenuation(cosTheta, light.directionCone.w, light.shadowFlags.y);
            if (cookieMode != HBG_LIGHT_COOKIE_MODE_NONE && int(light.cookieInfo.y + 0.5) == HBG_LIGHT_COOKIE_TYPE_2D)
            {
                const vec3 forward = normalize(light.directionCone.xyz);
                vec3 right = cross(vec3(0.0, 1.0, 0.0), forward);
                if (dot(right, right) < 1.0e-6)
                {
                    right = cross(vec3(1.0, 0.0, 0.0), forward);
                }
                right = normalize(right);
                const vec3 up = cross(forward, right);
                const float cosOuter = max(light.directionCone.w, 1.0e-4);
                const float tanOuter = sqrt(max(1.0 - cosOuter * cosOuter, 0.0)) / cosOuter;
                const vec2 cookieUV = vec2(dot(fromLight, right), dot(fromLight, up)) /
                                      max(tanOuter * 2.0, 1.0e-4) + vec2(0.5, 0.5);
                attenuation *= hbgSampleLightCookieScaled(index, cookieUV, cookieMode, cookieScale, cookieOffset);
            }
            if (hbgLightHasIes(light.cookieInfo))
            {
                if (light.shadowFadeFlags.z > 0.0 && cosTheta < light.shadowFadeFlags.z)
                {
                    attenuation = 0.0;
                }
                attenuation *= hbgSampleLightIes(index, acos(clamp(cosTheta, -1.0, 1.0)));
            }
        }
        else if (lightType == 1 && cookieMode != HBG_LIGHT_COOKIE_MODE_NONE &&
                 int(light.cookieInfo.y + 0.5) == HBG_LIGHT_COOKIE_TYPE_OCTA)
        {
            attenuation *= hbgSampleLightCookieScaled(index, hbgOctahedralEncode(-lightDirection), cookieMode,
                                                      cookieScale, cookieOffset);
        }
#if HBG_VARIANT_HAS_SHADOWS
        if (hbgLightCastShadow(light.shadowFlags) && light.shadowFlags.x >= 0.0 && shadowDimmer > 0.0)
        {
            visibility = mix(1.0,
                             hbgSamplePunctualShadow(int(light.shadowFlags.x + 0.5), lightType,
                                                     light.positionRange.xyz, light.directionCone.xyz,
                                                     worldPosition, surfaceData.normalWS),
                             shadowDimmer);
        }
#endif
        radiance = light.colorIntensity.rgb * light.colorIntensity.w * attenuation;
    }

    visibility *= hbgContactShadowVisibility(surfaceData.contactShadowMask, surfaceData.contactShadowFade,
                                             light.shadowFlags.z);

    const vec3 shadowColor = hbgComputeShadowColor(visibility, light.shadowTintDimmer.rgb,
                                                   light.shadowFadeFlags.y);
    const LitResult direct = hbgEvaluateLight(surfaceData, viewWS, lightDirection, radiance, shadowColor);
    return direct.diffuse + direct.specular;
}

vec3 hbgEvaluateLightLoop(SurfaceData surfaceData, vec3 viewWS, vec3 worldPosition,
                          float directionalShadowVisibility)
{
    vec3 color = hbgEvaluateAmbient(surfaceData, viewWS, worldPosition, u_pass_ambientColor.rgb,
                                    u_pass_ambientColor.w);

    const int lightCount = min(int(u_pass_lightCounts.x + 0.5), hbgLightListCapacity());
    const int directionalCount = min(int(u_pass_lightCounts.y + 0.5), lightCount);
    const bool clusterActive = u_cluster_depthParams.w > 0.5;
    const bool tileListActive = u_pass_lightListParams.w > 0.5;

#if defined(HBG_VARIANT_MODE)
#if (HBG_VARIANT_LIGHT_FLAGS & 4)
    for (int index = 0; index < directionalCount; ++index)
    {
        color += hbgEvaluateLightData(index, surfaceData, viewWS, worldPosition,
                                      directionalShadowVisibility);
    }
#endif
#if (HBG_VARIANT_LIGHT_FLAGS & 3)
#if defined(HBG_VARIANT_CATCHALL)
    const bool featureFlagsValid = u_pass_lightListParams2.z > 0.5;
    ivec2 featureTile = ivec2(0, 0);
    uint tileFeatureFlags = 0xffffffffu;
    if (clusterActive)
    {
        featureTile = hbgLightClusterTileCoord(worldPosition);
        if (featureFlagsValid)
        {
            tileFeatureFlags = hbgClusterFeatureFlags(featureTile);
        }
    }
    else if (tileListActive)
    {
        featureTile = hbgLightTileCoord(worldPosition);
        if (featureFlagsValid)
        {
            tileFeatureFlags = hbgLightFeatureFlags(featureTile);
        }
    }
#else
    const ivec2 featureTile =
        clusterActive ? hbgLightClusterTileCoord(worldPosition) : hbgLightTileCoord(worldPosition);
    const uint tileFeatureFlags = HBG_VARIANT_LIGHT_FLAGS;
#endif
    if (clusterActive)
    {
        const ivec2 tile = featureTile;
        const int slice = hbgClusterSliceForDepth(hbgLinearDepth(worldPosition));
        for (int category = 0; category < HBG_LIGHT_LIST_CATEGORY_COUNT; ++category)
        {
            const uint categoryFlag =
                category == HBG_LIGHT_CATEGORY_PUNCTUAL ? HBG_LIGHT_FEATURE_PUNCTUAL : HBG_LIGHT_FEATURE_AREA;
            if ((tileFeatureFlags & categoryFlag) == 0u)
            {
                continue;
            }
            const int clusterLightCount =
                min(int(hbgClusterLightListCount(tile, slice, category)), hbgLightListCapacity());
            for (int offset = 0; offset < clusterLightCount; ++offset)
            {
                const int index = int(hbgClusterLightListIndex(tile, slice, offset, category));
                if (index >= directionalCount && index < lightCount)
                {
                    color += hbgEvaluateLightData(index, surfaceData, viewWS, worldPosition, 1.0);
                }
            }
        }
    }
    else if (tileListActive)
    {
        const ivec2 tile = featureTile;
        for (int category = 0; category < HBG_LIGHT_LIST_CATEGORY_COUNT; ++category)
        {
            const uint categoryFlag =
                category == HBG_LIGHT_CATEGORY_PUNCTUAL ? HBG_LIGHT_FEATURE_PUNCTUAL : HBG_LIGHT_FEATURE_AREA;
            if ((tileFeatureFlags & categoryFlag) == 0u)
            {
                continue;
            }
            const int tileLightCount = min(int(hbgLightListCount(tile, category)), hbgLightListCapacity());
            for (int offset = 0; offset < tileLightCount; ++offset)
            {
                const int index = int(hbgLightListIndex(tile, offset, category));
                if (index >= directionalCount && index < lightCount)
                {
                    color += hbgEvaluateLightData(index, surfaceData, viewWS, worldPosition, 1.0);
                }
            }
        }
    }
    else
    {
        const int start = directionalCount;
        const int fallbackCount = min(lightCount - start, hbgLightListCapacity());
        for (int offset = 0; offset < fallbackCount; ++offset)
        {
            color += hbgEvaluateLightData(start + offset, surfaceData, viewWS, worldPosition, 1.0);
        }
    }
#endif
#else
    const bool featureFlagsValid = u_pass_lightListParams2.z > 0.5;
    ivec2 featureTile = ivec2(0, 0);
    uint tileFeatureFlags = 0xffffffffu;
    if (clusterActive)
    {
        featureTile = hbgLightClusterTileCoord(worldPosition);
        if (featureFlagsValid)
        {
            tileFeatureFlags = hbgClusterFeatureFlags(featureTile);
        }
    }
    else if (tileListActive)
    {
        featureTile = hbgLightTileCoord(worldPosition);
        if (featureFlagsValid)
        {
            tileFeatureFlags = hbgLightFeatureFlags(featureTile);
        }
    }

    if ((tileFeatureFlags & HBG_LIGHT_FEATURE_DIRECTIONAL) != 0u)
    {
        for (int index = 0; index < directionalCount; ++index)
        {
            color += hbgEvaluateLightData(index, surfaceData, viewWS, worldPosition,
                                          directionalShadowVisibility);
        }
    }

    if (clusterActive)
    {
        const ivec2 tile = featureTile;
        const int slice = hbgClusterSliceForDepth(hbgLinearDepth(worldPosition));
        for (int category = 0; category < HBG_LIGHT_LIST_CATEGORY_COUNT; ++category)
        {
            const uint categoryFlag =
                category == HBG_LIGHT_CATEGORY_PUNCTUAL ? HBG_LIGHT_FEATURE_PUNCTUAL : HBG_LIGHT_FEATURE_AREA;
            if ((tileFeatureFlags & categoryFlag) == 0u)
            {
                continue;
            }
            const int clusterLightCount =
                min(int(hbgClusterLightListCount(tile, slice, category)), hbgLightListCapacity());
            for (int offset = 0; offset < clusterLightCount; ++offset)
            {
                const int index = int(hbgClusterLightListIndex(tile, slice, offset, category));
                if (index >= directionalCount && index < lightCount)
                {
                    color += hbgEvaluateLightData(index, surfaceData, viewWS, worldPosition, 1.0);
                }
            }
        }
    }
    else if (tileListActive)
    {
        const ivec2 tile = featureTile;
        for (int category = 0; category < HBG_LIGHT_LIST_CATEGORY_COUNT; ++category)
        {
            const uint categoryFlag =
                category == HBG_LIGHT_CATEGORY_PUNCTUAL ? HBG_LIGHT_FEATURE_PUNCTUAL : HBG_LIGHT_FEATURE_AREA;
            if ((tileFeatureFlags & categoryFlag) == 0u)
            {
                continue;
            }
            const int tileLightCount = min(int(hbgLightListCount(tile, category)), hbgLightListCapacity());
            for (int offset = 0; offset < tileLightCount; ++offset)
            {
                const int index = int(hbgLightListIndex(tile, offset, category));
                if (index >= directionalCount && index < lightCount)
                {
                    color += hbgEvaluateLightData(index, surfaceData, viewWS, worldPosition, 1.0);
                }
            }
        }
    }
    else
    {
        const int start = directionalCount;
        const int fallbackCount = min(lightCount - start, hbgLightListCapacity());
        for (int offset = 0; offset < fallbackCount; ++offset)
        {
            color += hbgEvaluateLightData(start + offset, surfaceData, viewWS, worldPosition, 1.0);
        }
    }
#endif

    return color;
}

#endif // HBG_LIGHT_LOOP_SC
