#ifndef HBG_OCEAN_BRDF_SC
#define HBG_OCEAN_BRDF_SC

#include "common/common.sc"
#include "lighting/environment.sc"

#ifdef HBG_OCEAN_SLOPE_VARIANCE_TEXTURE
#ifndef HBG_OCEAN_SLOPE_VARIANCE_STAGE
#define HBG_OCEAN_SLOPE_VARIANCE_STAGE 15
#endif
SAMPLER3D(s_slopeVariance, HBG_OCEAN_SLOPE_VARIANCE_STAGE);
#endif

#ifdef HBG_SKY_MAP_ARRAY
SAMPLER2DARRAY(s_skyMap, 7);
#endif

#define HBG_OCEAN_SLOPE_VARIANCE_N 10
#define HBG_OCEAN_SKY_MAP_WIDTH 32
#define HBG_OCEAN_SKY_MAP_HEIGHT 16
#define HBG_OCEAN_SKY_MAP_TAU_EXTENT 1.1
#define HBG_OCEAN_SKY_MAP_MIP_COUNT 2

float hbgOceanSlopeVarianceTexel(int index)
{
    const vec4 texel = u_pass_slopeVarianceLut[index >> 2];
    const int component = index & 3;
    if (component == 0)
    {
        return texel.x;
    }
    if (component == 1)
    {
        return texel.y;
    }
    if (component == 2)
    {
        return texel.z;
    }
    return texel.w;
}

float hbgOceanSampleSlopeVarianceUniform(vec3 uvw)
{
    const int n = HBG_OCEAN_SLOPE_VARIANCE_N;
    const vec3 p = clamp(uvw, vec3(0.0, 0.0, 0.0), vec3(1.0, 1.0, 1.0)) * float(n - 1);
    const int x0 = int(floor(p.x));
    const int y0 = int(floor(p.y));
    const int z0 = int(floor(p.z));
    const int x1 = min(x0 + 1, n - 1);
    const int y1 = min(y0 + 1, n - 1);
    const int z1 = min(z0 + 1, n - 1);
    const vec3 f = p - floor(p);

    const float c000 = hbgOceanSlopeVarianceTexel(x0 + n * (y0 + n * z0));
    const float c100 = hbgOceanSlopeVarianceTexel(x1 + n * (y0 + n * z0));
    const float c010 = hbgOceanSlopeVarianceTexel(x0 + n * (y1 + n * z0));
    const float c110 = hbgOceanSlopeVarianceTexel(x1 + n * (y1 + n * z0));
    const float c001 = hbgOceanSlopeVarianceTexel(x0 + n * (y0 + n * z1));
    const float c101 = hbgOceanSlopeVarianceTexel(x1 + n * (y0 + n * z1));
    const float c011 = hbgOceanSlopeVarianceTexel(x0 + n * (y1 + n * z1));
    const float c111 = hbgOceanSlopeVarianceTexel(x1 + n * (y1 + n * z1));

    return mix(mix(mix(c000, c100, f.x), mix(c010, c110, f.x), f.y),
               mix(mix(c001, c101, f.x), mix(c011, c111, f.x), f.y), f.z);
}

float hbgOceanSampleSlopeVarianceLut(vec3 uvw)
{
#ifdef HBG_OCEAN_SLOPE_VARIANCE_TEXTURE
    float result;
    if (u_pass_slopeVarianceParams.y > 0.5)
    {
        result = texture3D(s_slopeVariance, clamp(uvw, vec3(0.0, 0.0, 0.0), vec3(1.0, 1.0, 1.0))).x;
    }
    else
    {
        result = hbgOceanSampleSlopeVarianceUniform(uvw);
    }
    return result;
#else
    return hbgOceanSampleSlopeVarianceUniform(uvw);
#endif
}

float hbgOceanSlopeVariance(vec2 oceanDx, vec2 oceanDy)
{
    if (u_pass_slopeVarianceParams.x < 0.5)
    {
        return u_object_lightLayerMask.z;
    }

    const float Jxx = oceanDx.x;
    const float Jxy = oceanDy.x;
    const float Jyx = oceanDx.y;
    const float Jyy = oceanDy.y;
    const float A = Jxx * Jxx + Jyx * Jyx;
    const float B = Jxx * Jxy + Jyx * Jyy;
    const float C = Jxy * Jxy + Jyy * Jyy;
    const float SCALE = 10.0;
    const float ua = pow(max(A / SCALE, 0.0), 0.25);
    const float ub = 0.5 + 0.5 * B / sqrt(max(A * C, 1.0e-20));
    const float uc = pow(max(C / SCALE, 0.0), 0.25);
    return hbgOceanSampleSlopeVarianceLut(vec3(ua, ub, uc));
}

float hbgOceanMeanFresnel(float cosThetaV, float sigmaV)
{
    return pow(max(1.0 - cosThetaV, 0.0), 5.0 * exp(-2.69 * sigmaV)) /
           (1.0 + 22.7 * pow(sigmaV, 1.5));
}

float hbgOceanReflectedSunRadiance(vec3 V, vec3 N, vec3 L, float sigmaSq)
{
    const vec3 H = normalize(L + V);
    const float hn = dot(H, N);
    const float p = exp(-2.0 * ((1.0 - hn * hn) / sigmaSq) / (1.0 + hn)) /
                    (4.0 * HBG_PI * sigmaSq);

    const float c = 1.0 - dot(V, H);
    const float c2 = c * c;
    const float fresnel = 0.02 + 0.98 * c2 * c2 * c;

    const float zL = max(dot(L, N), 0.01);
    const float zV = max(dot(V, N), 0.01);
    return fresnel * p * sqrt(abs(zL / zV));
}

vec3 hbgOceanSkyELutFetch(int x, int y)
{
    const int width = int(max(u_pass_atmosphereSkyEParams.y, 1.0));
    return u_pass_atmosphereSkyELut[y * width + x].rgb;
}

vec3 hbgOceanSampleSkyELut(float radius, float muSun)
{
    const float width = max(u_pass_atmosphereSkyEParams.y, 1.0);
    const float height = max(u_pass_atmosphereSkyEParams.z, 1.0);
    const float groundRadius = u_pass_atmosphereSkyEParams.w;
    const float topRadius = max(u_pass_atmosphereSkyEPlanet.x, groundRadius + 1.0);
    const float uR = clamp((radius - groundRadius) / (topRadius - groundRadius), 0.0, 1.0);
    const float uMu = clamp((muSun + 0.2) / 1.2, 0.0, 1.0);
    const vec2 p = vec2(uMu * width, uR * height) - vec2(0.5, 0.5);
    const int x0 = clamp(int(floor(p.x)), 0, int(width) - 1);
    const int y0 = clamp(int(floor(p.y)), 0, int(height) - 1);
    const int x1 = min(x0 + 1, int(width) - 1);
    const int y1 = min(y0 + 1, int(height) - 1);
    const float fx = clamp(p.x - float(x0), 0.0, 1.0);
    const float fy = clamp(p.y - float(y0), 0.0, 1.0);
    const vec3 c00 = hbgOceanSkyELutFetch(x0, y0);
    const vec3 c10 = hbgOceanSkyELutFetch(x1, y0);
    const vec3 c01 = hbgOceanSkyELutFetch(x0, y1);
    const vec3 c11 = hbgOceanSkyELutFetch(x1, y1);
    return max(mix(mix(c00, c10, fx), mix(c01, c11, fx), fy), vec3(0.0, 0.0, 0.0));
}

vec3 hbgOceanSkyIrradiance(vec3 worldPosition, vec3 V, vec3 N, float perceptualRoughness)
{
    vec3 result = vec3(0.0, 0.0, 0.0);
    if (u_pass_atmosphereSkyE.w > 0.5) {
        if (u_pass_atmosphereSkyEParams.x > 0.5) {
            const vec3 earthPosition = worldPosition - u_pass_atmosphereSkyEPlanet.yzw;
            const float radius = max(length(earthPosition), 1.0);
            const vec3 vertical = earthPosition / radius;
            const vec3 sun = normalize(u_pass_lightDirection.xyz);
            const float muSun = clamp(dot(vertical, sun), -1.0, 1.0);
            const vec3 base = hbgOceanSampleSkyELut(radius, muSun);
            result = base * (1.0 + dot(vertical, N));
        } else {
            result = u_pass_atmosphereSkyE.rgb * (1.0 + dot(vec3(0.0, 1.0, 0.0), N));
        }
    } else {
        const float skyIntensity = max(u_environment_skyParams.x, 0.0);
        const vec3 shIrradiance = max(hbgSampleSH9(N), vec3(0.0, 0.0, 0.0)) * skyIntensity;
        const vec3 ambient = max(u_pass_ambientColor.rgb, vec3(0.0, 0.0, 0.0)) *
                             max(u_pass_ambientColor.w, 0.0);
        const vec3 reflectedSky =
            hbgSampleEnvironment(reflect(-V, N), perceptualRoughness) * skyIntensity;
        result = shIrradiance + ambient + max(reflectedSky, vec3(0.0, 0.0, 0.0)) * 6.0;
    }
    return max(result, vec3(0.0, 0.0, 0.0));
}

vec3 hbgOceanSampleSkySh(vec3 direction)
{
    const vec4 vA = vec4(direction, 1.0);
    vec3 result;
    result.r = dot(u_pass_atmosphereSkyShAr, vA);
    result.g = dot(u_pass_atmosphereSkyShAg, vA);
    result.b = dot(u_pass_atmosphereSkyShAb, vA);

    const vec4 vB = direction.xyzz * direction.yzzx;
    vec3 quadratic;
    quadratic.r = dot(u_pass_atmosphereSkyShBr, vB);
    quadratic.g = dot(u_pass_atmosphereSkyShBg, vB);
    quadratic.b = dot(u_pass_atmosphereSkyShBb, vB);
    const float vC = direction.x * direction.x - direction.y * direction.y;
    result += quadratic + u_pass_atmosphereSkyShC.xyz * vC;
    return max(result, vec3(0.0, 0.0, 0.0));
}

#ifdef HBG_SKY_MAP_ARRAY
vec2 hbgOceanSkyMapArraySunXZ()
{
    const vec2 sunXZ = vec2(u_pass_lightDirection.x, u_pass_lightDirection.z);
    const float sunLength = length(sunXZ);
    return sunLength > 1.0e-5 ? sunXZ / sunLength : vec2(1.0, 0.0);
}

vec2 hbgOceanSkyMapArrayRotateTau(vec2 tau, vec2 sunXZ)
{
    return vec2(sunXZ.x * tau.x + sunXZ.y * tau.y,
                -sunXZ.y * tau.x + sunXZ.x * tau.y);
}

vec3 hbgOceanSampleSkyMapArray(vec2 tau, float lod)
{
    const float maxLayer = 28.0;
    const vec3 sun = normalize(u_pass_lightDirection.xyz);
    const float l = acos(clamp(sun.y, 0.0, 1.0)) * (maxLayer / (HBG_PI * 0.5));
    const float layer0 = clamp(floor(l), 0.0, maxLayer);
    const float layer1 = min(layer0 + 1.0, maxLayer);
    const float blend = clamp(l - layer0, 0.0, 1.0);
    const vec2 uv = clamp(tau * (0.5 / HBG_OCEAN_SKY_MAP_TAU_EXTENT) + vec2(0.5, 0.5),
                          vec2(0.0, 0.0), vec2(1.0, 1.0));
    const float mip = clamp(lod, 0.0, 5.0);
    const vec3 c0 = texture2DArrayLod(s_skyMap, vec3(uv, layer0), mip).rgb;
    const vec3 c1 = texture2DArrayLod(s_skyMap, vec3(uv, layer1), mip).rgb;
    return mix(c0, c1, blend);
}
#else
vec3 hbgOceanSkyMapFetch(int offset, int width, int x, int y)
{
    return u_atmospheresky_map[offset + y * width + x].rgb;
}

vec3 hbgOceanSkyMapSampleLevel(int width, int height, int offset, vec2 tau)
{
    const vec2 uv = tau * (0.5 / HBG_OCEAN_SKY_MAP_TAU_EXTENT) + vec2(0.5, 0.5);
    const vec2 p = clamp(uv, vec2(0.0, 0.0), vec2(1.0, 1.0)) *
                       vec2(float(width), float(height)) -
                   vec2(0.5, 0.5);
    const int x0 = clamp(int(floor(p.x)), 0, width - 1);
    const int y0 = clamp(int(floor(p.y)), 0, height - 1);
    const int x1 = min(x0 + 1, width - 1);
    const int y1 = min(y0 + 1, height - 1);
    const float fx = clamp(p.x - float(x0), 0.0, 1.0);
    const float fy = clamp(p.y - float(y0), 0.0, 1.0);
    const vec3 c00 = hbgOceanSkyMapFetch(offset, width, x0, y0);
    const vec3 c10 = hbgOceanSkyMapFetch(offset, width, x1, y0);
    const vec3 c01 = hbgOceanSkyMapFetch(offset, width, x0, y1);
    const vec3 c11 = hbgOceanSkyMapFetch(offset, width, x1, y1);
    return mix(mix(c00, c10, fx), mix(c01, c11, fx), fy);
}

vec3 hbgOceanSampleSkyMap(vec2 tau, float lod)
{
    const float blend = clamp(lod, 0.0, 1.0);
    const vec3 base = hbgOceanSkyMapSampleLevel(32, 16, 0, tau);
    const vec3 filtered = hbgOceanSkyMapSampleLevel(16, 8, 512, tau);
    return mix(base, filtered, blend);
}

#endif

vec2 hbgOceanSkyU(vec2 zeta, vec3 V)
{
    const vec3 F = normalize(vec3(-zeta.x, 1.0, -zeta.y));
    const vec3 R = 2.0 * dot(F, V) * F - V;
    return -R.xz / max(1.0 + R.y, 1.0e-4);
}

vec3 hbgOceanReflectedSkyRadiance(vec3 V, vec3 N, vec3 skyIrradiance, float sigmaSq)
{
    vec3 result = max(skyIrradiance, vec3(0.0, 0.0, 0.0)) / HBG_PI;
    if (u_pass_atmosphereSkyE.w > 0.5) {
        const vec3 reflection = reflect(-V, N);
        const float horizon = smoothstep(-0.04, 0.04, reflection.y);
        const vec3 sampleDirection = normalize(vec3(reflection.x, max(reflection.y, 1.0e-3), reflection.z));
        vec3 directional = hbgOceanSampleSkySh(sampleDirection);
        if (u_atmospheresky_mapParams.x > 0.5) {
            const vec2 zeta0 = clamp(-N.xz / max(N.y, 1.0e-3), vec2(-8.0, -8.0), vec2(8.0, 8.0));
            const vec2 tau0 = hbgOceanSkyU(zeta0, V);
            const float sigma = sqrt(max(sigmaSq, 2.0e-5));
            const float n = 1.0 / HBG_OCEAN_SKY_MAP_TAU_EXTENT;
            const vec2 JX = (hbgOceanSkyU(zeta0 + vec2(0.01, 0.0), V) - tau0) / 0.01 * n * sigma;
            const vec2 JY = (hbgOceanSkyU(zeta0 + vec2(0.0, 0.01), V) - tau0) / 0.01 * n * sigma;
#ifdef HBG_SKY_MAP_ARRAY
            const vec2 mapSunXZ = hbgOceanSkyMapArraySunXZ();
            const vec2 mapTau = hbgOceanSkyMapArrayRotateTau(tau0, mapSunXZ);
            const vec2 mapDX = hbgOceanSkyMapArrayRotateTau(JX, mapSunXZ);
            const vec2 mapDY = hbgOceanSkyMapArrayRotateTau(JY, mapSunXZ);
#else
            const vec2 mapTau = tau0;
            const vec2 mapDX = JX;
            const vec2 mapDY = JY;
#endif
            const vec2 duvdx = mapDX * (0.5 / HBG_OCEAN_SKY_MAP_TAU_EXTENT);
            const vec2 duvdy = mapDY * (0.5 / HBG_OCEAN_SKY_MAP_TAU_EXTENT);
            const float texelFootprint =
                max(length(duvdx * vec2(32.0, 16.0)), length(duvdy * vec2(32.0, 16.0)));
            const float lod = max(log2(max(texelFootprint, 1.0)), 0.0);
#ifdef HBG_SKY_MAP_ARRAY
            directional = hbgOceanSampleSkyMapArray(mapTau, lod);
#else
            directional = hbgOceanSampleSkyMap(mapTau, lod);
#endif
        }
        const vec3 average = max(u_pass_atmosphereSkyE.rgb, vec3(0.0, 0.0, 0.0)) / HBG_PI;
        result = mix(average, directional, horizon) *
                 (1.0 + max(dot(vec3(0.0, 1.0, 0.0), N), 0.0));
    }
    return result;
}

vec3 hbgEvaluateOceanBrdf(vec3 V, vec3 N, vec3 L, vec3 sunRadiance, vec3 skyIrradiance,
                          vec3 seaColor, float sigmaSq)
{
    const float sigmaV = sqrt(max(sigmaSq, 2.0e-5));
    const float fresnel = hbgOceanMeanFresnel(max(dot(V, N), 0.0), sigmaV);
    const vec3 sunTransmittance =
        u_pass_atmosphereSunTransmittance.w > 0.5 ? u_pass_atmosphereSunTransmittance.rgb
                                                  : vec3(1.0, 1.0, 1.0);
    const vec3 sun = hbgOceanReflectedSunRadiance(V, N, L, max(sigmaSq, 2.0e-5)) *
                     max(sunRadiance * sunTransmittance, vec3(0.0, 0.0, 0.0));
    const vec3 sky = u_pass_atmosphereSkyE.w > 0.5
                         ? hbgOceanReflectedSkyRadiance(V, N, skyIrradiance, sigmaSq) *
                               (0.02 + 0.98 * fresnel)
                         : skyIrradiance * fresnel / HBG_PI;
    const vec3 sea = (1.0 - fresnel) * seaColor * skyIrradiance / HBG_PI;
    return sun + sky + sea;
}

#endif // HBG_OCEAN_BRDF_SC
