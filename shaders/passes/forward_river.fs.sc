$input v_texcoord0, v_worldPos, v_normal, v_tangent, v_bitangent

#define HBG_RIVER_PARTICLES 1
#define HBG_RIVER_PARTICLE_STAGE14 1
#define HBG_PROBE_MAPS_STAGE 13

#include "material/surface_data.sc"
#include "lighting/ocean_brdf.sc"
#include "lighting/shadows.sc"
#include "material/river_particles.sc"

SAMPLER2D(s_hdrColor, 15);
SAMPLER2D(s_depthSource, 5);

float hbgRiverBedDistance(vec2 sceneUV)
{
    const float rawDepth = texture2DLod(s_depthSource, sceneUV, 0.0).x;
    if (rawDepth >= 0.999999)
    {
        return -1.0;
    }
    const vec2 ndc = vec2(sceneUV.x * 2.0 - 1.0, 1.0 - sceneUV.y * 2.0);
    const vec4 worldClip = mul(u_global_invViewProj, vec4(ndc, rawDepth, 1.0));
    const vec3 sceneWorld = worldClip.xyz / max(worldClip.w, 1.0e-6);
    return distance(u_global_cameraPos.xyz, sceneWorld);
}

bool hbgRiverTraceReflection(vec3 originWS, vec3 directionWS, float waterDistance, out vec2 hitUV)
{
    hitUV = vec2(0.0, 0.0);
    const float maxDistance = 60.0;
    const float stepSize = maxDistance / 24.0;
    const float thickness = 3.0;
    const vec3 origin = originWS + directionWS * 0.15;
    for (int step = 1; step <= 24; ++step)
    {
        const vec3 sampleWS = origin + directionWS * (stepSize * float(step));
        const vec4 clip = mul(u_global_viewProj, vec4(sampleWS, 1.0));
        if (clip.w <= 1.0e-5)
        {
            break;
        }
        const vec2 uv = clip.xy / clip.w * vec2(0.5, -0.5) + vec2(0.5, 0.5);
        if (uv.x <= 0.0 || uv.x >= 1.0 || uv.y <= 0.0 || uv.y >= 1.0)
        {
            break;
        }
        const float sceneDistance = hbgRiverBedDistance(uv);
        if (sceneDistance <= waterDistance + 0.1)
        {
            continue;
        }
        const float rayDistance = distance(u_global_cameraPos.xyz, sampleWS);
        if (rayDistance >= sceneDistance && rayDistance - sceneDistance <= thickness)
        {
            hitUV = uv;
            return true;
        }
    }
    return false;
}

void main()
{
    const vec3 viewWS = u_global_cameraPos.xyz - v_worldPos;
    const vec3 geometricNormalWS = normalize(v_normal);
    const float viewSign = gl_FrontFacing ? -1.0 : 1.0;
    SurfaceData surfaceData = hbgMakeSurfaceData(v_worldPos, geometricNormalWS, v_tangent, v_bitangent,
                                                 v_texcoord0, viewSign);
    surfaceData.lightLayerMask = u_object_lightLayerMask.x;

    surfaceData.normalWS = hbgRiverParticleNormalWS(v_worldPos, surfaceData.normalWS,
                                                    surfaceData.geometricNormalWS, v_tangent,
                                                    v_bitangent, viewSign);

    const vec3 N = normalize(surfaceData.normalWS);
    const vec3 V = normalize(viewWS);
    const float waterDistance = distance(u_global_cameraPos.xyz, v_worldPos);
    const vec3 L = normalize(u_pass_lightDirection.xyz);
    const float shadowVisibility = hbgSampleDirectionalShadow(v_worldPos, N);
    const vec3 sunRadiance = u_pass_lightColor.rgb * max(u_pass_lightDirection.w, 0.0) * shadowVisibility;
    const vec3 skyIrradiance = hbgOceanSkyIrradiance(v_worldPos, V, N, surfaceData.perceptualRoughness);
    const vec2 oceanCoord = v_worldPos.xz;
    const float seaRoughness = max(hbgOceanSlopeVariance(dFdx(oceanCoord), dFdy(oceanCoord)), 2.0e-5);
    vec3 ocean = hbgEvaluateOceanBrdf(V, N, L, sunRadiance, skyIrradiance,
                                      surfaceData.baseColor, seaRoughness);
    const float reflectionStrength = hbgSaturate(u_material_riverRefractionParams.y);
    if (reflectionStrength > 0.0)
    {
        vec3 reflectedDir = reflect(-V, N);
        reflectedDir.y = max(reflectedDir.y, 0.02);
        reflectedDir = normalize(reflectedDir);
        const float reflectionFresnel = pow(1.0 - hbgSaturate(dot(N, V)), 5.0);
        const float reflectionWeight = reflectionStrength * (0.08 + 0.92 * reflectionFresnel);
        vec2 reflectionUV;
        if (hbgRiverTraceReflection(v_worldPos, reflectedDir, waterDistance, reflectionUV))
        {
            const vec3 reflectedColor = max(texture2D(s_hdrColor, reflectionUV).rgb, vec3(0.0, 0.0, 0.0));
            ocean = mix(ocean, reflectedColor, reflectionWeight);
        }
        else
        {
            float probeWeight = 0.0;
            const vec3 probeReflection =
                max(hbgSampleProbeReflection(v_worldPos, reflectedDir, surfaceData.perceptualRoughness, probeWeight),
                    vec3(0.0, 0.0, 0.0));
            if (probeWeight > 0.0)
            {
                ocean = mix(ocean, probeReflection, reflectionWeight);
            }
        }
    }
    const float alpha = hbgSaturate(surfaceData.alpha);
    const float refractionStrength = hbgSaturate(u_material_riverParticleParams.w);
    if (refractionStrength > 0.0)
    {
        const vec4 clip = mul(u_global_viewProj, vec4(v_worldPos, 1.0));
        const vec2 screenUV = clip.xy / max(clip.w, 1.0e-6) * vec2(0.5, -0.5) + vec2(0.5, 0.5);
        const vec2 refractedUV = clamp(screenUV + N.xz * (refractionStrength * 0.05),
                                       vec2(0.001, 0.001), vec2(0.999, 0.999));
        const vec3 source = max(texture2D(s_hdrColor, screenUV).rgb, vec3(0.0, 0.0, 0.0));
        const vec3 refracted = max(texture2D(s_hdrColor, refractedUV).rgb, vec3(0.0, 0.0, 0.0));
        const vec3 bed = mix(source, refracted, alpha);
        const float bedDistance = hbgRiverBedDistance(screenUV);
        const float waterPath = bedDistance > 0.0 ? max(bedDistance - waterDistance, 0.0) : 0.0;
        const float absorption = max(u_material_riverRefractionParams.x, 0.0);
        const float transmittance = exp(-absorption * waterPath);
        const vec3 absorbedBed = bed * transmittance + surfaceData.baseColor.rgb * (1.0 - transmittance);
        const float fresnel = pow(1.0 - hbgSaturate(dot(N, V)), 5.0);
        const float waterOpacity = alpha * (0.70 + 0.25 * fresnel);
        gl_FragColor = vec4(mix(absorbedBed, ocean, waterOpacity), 1.0);
    }
    else
    {
        gl_FragColor = vec4(max(ocean, vec3(0.0, 0.0, 0.0)), alpha);
    }
}
