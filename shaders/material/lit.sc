#ifndef HBG_LIT_SC
#define HBG_LIT_SC

#include "lighting/environment.sc"
#include "material/surface_data.sc"

struct LitResult
{
    vec3 diffuse;
    vec3 specular;
};

LitResult hbgEvaluateLight(SurfaceData surfaceData, vec3 viewWS, vec3 lightDirectionWS, vec3 radiance,
                           float visibility)
{
    LitResult result;
    result.diffuse = vec3(0.0, 0.0, 0.0);
    result.specular = vec3(0.0, 0.0, 0.0);

    vec3 L = normalize(lightDirectionWS);
    vec3 V = normalize(viewWS);
    vec3 N = normalize(surfaceData.normalWS);
    vec3 H = normalize(L + V);

    float NdotL = dot(N, L);
    float NdotV = max(dot(N, V), 1.0e-4);
    float NdotH = hbgSaturate(dot(N, H));
    float LdotH = hbgSaturate(dot(L, H));
    float LdotV = dot(L, V);

    if (NdotL <= 0.0 || visibility <= 0.0)
    {
        return result;
    }

    vec3 f0 = mix(vec3(0.04, 0.04, 0.04), surfaceData.baseColor, surfaceData.metallic);
    vec3 diffuseColor = surfaceData.baseColor * (1.0 - surfaceData.metallic);
    vec3 fresnel = hbgF_Schlick(f0, LdotH);
    float D = hbgD_GGX(NdotH, surfaceData.roughness);
    float Vis = hbgV_SmithJointGGX(NdotL, NdotV, surfaceData.roughness);

    float diffuseTerm = hbgDisneyDiffuse(NdotV, NdotL, LdotV, surfaceData.perceptualRoughness);
    float specularTerm = D * Vis;

    result.diffuse = diffuseColor * diffuseTerm * NdotL * radiance * visibility;
    result.specular = fresnel * specularTerm * NdotL * radiance * visibility;
    return result;
}

vec3 hbgEvaluateAmbient(SurfaceData surfaceData, vec3 viewWS, vec3 ambientColor, float ambientIntensity)
{
    vec3 N = normalize(surfaceData.normalWS);
    vec3 V = normalize(viewWS);
    vec3 f0 = mix(vec3(0.04, 0.04, 0.04), surfaceData.baseColor, surfaceData.metallic);
    float NdotV = max(dot(N, V), 1.0e-4);

    vec3 fresnel = hbgF_Schlick(f0, NdotV);
    vec3 skyColor = mix(vec3(0.25, 0.30, 0.40), vec3(0.75, 0.82, 1.0),
                        hbgSaturate(N.y * 0.5 + 0.5));
    vec3 result = (surfaceData.baseColor * (1.0 - surfaceData.metallic) * surfaceData.ambientOcclusion +
                   fresnel * skyColor * (1.0 - surfaceData.roughness * 0.5) *
                       surfaceData.ambientOcclusion) *
                  ambientColor * ambientIntensity;

    if (u_environment_skyParams.z > 0.5)
    {
        const float intensity = u_environment_skyParams.x;
        const float perceptualRoughness = surfaceData.perceptualRoughness;
        const vec3 reflection = reflect(-V, N);
        const vec2 ab = hbgEnvBRDFApprox(NdotV, perceptualRoughness);
        result = surfaceData.baseColor * (1.0 - surfaceData.metallic) * surfaceData.ambientOcclusion *
                 hbgSampleSH9(N) * intensity;
        if (u_environment_skyParams.w > 0.5)
        {
            result += hbgSampleEnvironment(reflection, perceptualRoughness) * intensity *
                      (f0 * ab.x + ab.y) * surfaceData.ambientOcclusion;
        }
    }
    return result;
}

#endif // HBG_LIT_SC
