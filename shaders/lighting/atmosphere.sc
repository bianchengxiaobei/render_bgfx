#pragma warning(disable: 4000)
#ifndef HBG_ATMOSPHERE_SC
#define HBG_ATMOSPHERE_SC

// Single-scattering raymarch based on Proland's precomputed atmosphere model:
// atmo/sources/proland/preprocess/atmo/common.glsl and
// atmo/examples/atmo/atmosphereShader.glsl. When the precomputed LUT is
// available the original transmittance/irradiance/inscatter parameterization
// is sampled; otherwise the analytic transmittance raymarch is used as a
// fallback.

#define HBG_ATMO_TRANSMITTANCE_W 256
#define HBG_ATMO_TRANSMITTANCE_H 64
#define HBG_ATMO_SKY_W 64
#define HBG_ATMO_SKY_H 16
#define HBG_ATMO_RES_R 32
#define HBG_ATMO_RES_MU 128
#define HBG_ATMO_RES_MU_S 32
#define HBG_ATMO_RES_NU 8
#define HBG_ATMO_MS_RES_R 16
#define HBG_ATMO_MS_RES_MU 32
#define HBG_ATMO_MS_RES_MU_S 16
#define HBG_ATMO_MS_RES_NU 4

SAMPLER2D(s_atmosphereTransmittance, 2);
SAMPLER2D(s_atmosphereIrradiance, 3);
SAMPLER3D(s_atmosphereInscatter, 4);
SAMPLER3D(s_atmosphereMultipleScattering, 5);

vec2 hbgAtmosphereTransmittanceUv(float radius, float mu)
{
    const float uR = sqrt(clamp((radius - u_atmosphere_params.x) /
                                    (u_atmosphere_params.y - u_atmosphere_params.x),
                                0.0, 1.0));
    const float uMu = atan((clamp(mu, -1.0, 1.0) + 0.15) / (1.0 + 0.15) * tan(1.5)) / 1.5;
    return vec2(uMu, uR);
}

vec2 hbgAtmosphereIrradianceUv(float radius, float muSun)
{
    const float uR = clamp((radius - u_atmosphere_params.x) /
                               (u_atmosphere_params.y - u_atmosphere_params.x),
                           0.0, 1.0);
    const float uMuSun = clamp((muSun + 0.2) / 1.2, 0.0, 1.0);
    return vec2(uMuSun, uR);
}

vec3 hbgAtmosphereTransmittanceLut(float radius, float mu)
{
    return texture2D(s_atmosphereTransmittance, hbgAtmosphereTransmittanceUv(radius, mu)).rgb;
}

vec3 hbgAtmosphereIrradianceLut(float radius, float muSun)
{
    return texture2D(s_atmosphereIrradiance, hbgAtmosphereIrradianceUv(radius, muSun)).rgb;
}

vec4 hbgAtmosphereTexture4D(float radius, float mu, float muSun, float nu)
{
    const float height = sqrt(max(u_atmosphere_params.y * u_atmosphere_params.y -
                                      u_atmosphere_params.x * u_atmosphere_params.x,
                                  0.0));
    const float rho = sqrt(max(radius * radius - u_atmosphere_params.x * u_atmosphere_params.x, 0.0));
    const float rmu = radius * mu;
    const float delta = rmu * rmu - radius * radius + u_atmosphere_params.x * u_atmosphere_params.x;
    const bool belowHorizon = rmu < 0.0 && delta > 0.0;
    const vec4 cst = belowHorizon
                         ? vec4(1.0, 0.0, 0.0, 0.5 - 0.5 / float(HBG_ATMO_RES_MU))
                         : vec4(-1.0, height * height, height, 0.5 + 0.5 / float(HBG_ATMO_RES_MU));
    const float uR = 0.5 / float(HBG_ATMO_RES_R) +
                     rho / max(height, 1.0e-6) * (1.0 - 1.0 / float(HBG_ATMO_RES_R));
    const float uMu = cst.w + (rmu * cst.x + sqrt(max(delta + cst.y, 0.0))) /
                                 max(rho + cst.z, 1.0e-6) *
                                 (0.5 - 1.0 / float(HBG_ATMO_RES_MU));
    const float uMuSun =
        0.5 / float(HBG_ATMO_RES_MU_S) +
        (atan(max(muSun, -0.1975) * tan(1.26 * 1.1)) / 1.1 + (1.0 - 0.26)) * 0.5 *
            (1.0 - 1.0 / float(HBG_ATMO_RES_MU_S));
    const float lerpNu = (clamp(nu, -1.0, 1.0) + 1.0) * 0.5 * float(HBG_ATMO_RES_NU - 1);
    const float nuIndex = min(floor(lerpNu), float(HBG_ATMO_RES_NU - 2));
    const float nuFraction = lerpNu - nuIndex;
    const vec4 low = texture3D(s_atmosphereInscatter,
                               vec3((nuIndex + uMuSun) / float(HBG_ATMO_RES_NU), uMu, uR));
    const vec4 high = texture3D(s_atmosphereInscatter,
                                vec3((nuIndex + 1.0 + uMuSun) / float(HBG_ATMO_RES_NU), uMu, uR));
    return low * (1.0 - nuFraction) + high * nuFraction;
}

vec4 hbgAtmosphereMultipleScattering4D(float radius, float mu, float muSun, float nu)
{
    const float height = sqrt(max(u_atmosphere_params.y * u_atmosphere_params.y -
                                      u_atmosphere_params.x * u_atmosphere_params.x,
                                  0.0));
    const float rho = sqrt(max(radius * radius - u_atmosphere_params.x * u_atmosphere_params.x, 0.0));
    const float rmu = radius * mu;
    const float delta = rmu * rmu - radius * radius + u_atmosphere_params.x * u_atmosphere_params.x;
    const bool belowHorizon = rmu < 0.0 && delta > 0.0;
    const vec4 cst = belowHorizon
                         ? vec4(1.0, 0.0, 0.0, 0.5 - 0.5 / float(HBG_ATMO_MS_RES_MU))
                         : vec4(-1.0, height * height, height, 0.5 + 0.5 / float(HBG_ATMO_MS_RES_MU));
    const float uR = 0.5 / float(HBG_ATMO_MS_RES_R) +
                     rho / max(height, 1.0e-6) * (1.0 - 1.0 / float(HBG_ATMO_MS_RES_R));
    const float uMu = cst.w + (rmu * cst.x + sqrt(max(delta + cst.y, 0.0))) /
                                 max(rho + cst.z, 1.0e-6) *
                                 (0.5 - 1.0 / float(HBG_ATMO_MS_RES_MU));
    const float uMuSun =
        0.5 / float(HBG_ATMO_MS_RES_MU_S) +
        (atan(max(muSun, -0.1975) * tan(1.26 * 1.1)) / 1.1 + (1.0 - 0.26)) * 0.5 *
            (1.0 - 1.0 / float(HBG_ATMO_MS_RES_MU_S));
    const float lerpNu = (clamp(nu, -1.0, 1.0) + 1.0) * 0.5 * float(HBG_ATMO_MS_RES_NU - 1);
    const float nuIndex = min(floor(lerpNu), float(HBG_ATMO_MS_RES_NU - 2));
    const float nuFraction = lerpNu - nuIndex;
    const vec4 low = texture3D(s_atmosphereMultipleScattering,
                               vec3((nuIndex + uMuSun) / float(HBG_ATMO_MS_RES_NU), uMu, uR));
    const vec4 high = texture3D(s_atmosphereMultipleScattering,
                                vec3((nuIndex + 1.0 + uMuSun) / float(HBG_ATMO_MS_RES_NU), uMu, uR));
    return low * (1.0 - nuFraction) + high * nuFraction;
}

vec3 hbgAtmosphereMieFromLut(vec4 rayMie)
{
    return rayMie.rgb * rayMie.w / max(rayMie.r, 1.0e-4) *
           (u_atmosphere_betaR.x / max(u_atmosphere_betaR.rgb, vec3(1.0e-6, 1.0e-6, 1.0e-6)));
}

float hbgAtmospherePhaseRayleigh(float nu)
{
    return (3.0 / (16.0 * HBG_PI)) * (1.0 + nu * nu);
}

float hbgAtmospherePhaseMie(float nu)
{
    const float g = u_atmosphere_params2.y;
    const float mieDenom = max(1.0 + g * g - 2.0 * g * nu, 1.0e-6);
    return (1.5 * (1.0 - g * g) / (4.0 * HBG_PI)) *
           pow(mieDenom, -1.5) * (1.0 + nu * nu) / (2.0 + g * g);
}

float hbgAtmosphereOpticalDepth(float scaleHeight, float radius, float mu, float distance)
{
    if (distance <= 0.0 || scaleHeight <= 0.0)
    {
        return 0.0;
    }

    const float a = sqrt((0.5 / scaleHeight) * radius);
    const vec2 a01 = a * vec2(mu, mu + distance / radius);
    const vec2 a01s = sign(a01);
    const vec2 a01sq = a01 * a01;
    const float x = a01s.y > a01s.x ? exp(a01sq.x) : 0.0;
    const vec2 y = a01s / (2.3193 * abs(a01) + sqrt(1.52 * a01sq + 4.0)) *
                   vec2(1.0, exp(-distance / scaleHeight * (distance / (2.0 * radius) + mu)));
    return sqrt((6.2831 * scaleHeight) * radius) *
           exp((u_atmosphere_params.x - radius) / scaleHeight) * (x + dot(y, vec2(1.0, -1.0)));
}

float hbgAtmosphereRayLimit(float radius, float mu)
{
    const float groundRadius = u_atmosphere_params.x;
    const float topRadius = u_atmosphere_params.y;
    const float mu2 = mu * mu;
    float result = -radius * mu + sqrt(max(radius * radius * (mu2 - 1.0) + topRadius * topRadius, 0.0));
    const float delta = radius * radius * (mu2 - 1.0) + groundRadius * groundRadius;
    if (delta >= 0.0)
    {
        const float inner = -radius * mu - sqrt(delta);
        if (inner >= 0.0)
        {
            result = min(result, inner);
        }
    }
    return result;
}

vec3 hbgAtmosphereTransmittance(float radius, float mu, float distance)
{
    const float rayleigh = hbgAtmosphereOpticalDepth(u_atmosphere_params.z, radius, mu, distance);
    const float mie = hbgAtmosphereOpticalDepth(u_atmosphere_params.w, radius, mu, distance);
    const vec3 betaMieExtinction = u_atmosphere_betaM.rgb * u_atmosphere_betaM.w;
    return exp(-u_atmosphere_betaR.rgb * rayleigh - betaMieExtinction * mie);
}

vec3 hbgAtmosphereSunTransmittance(float radius, float muSun)
{
    const float groundRadius = u_atmosphere_params.x;
    const float horizon = -sqrt(max(1.0 - (groundRadius * groundRadius) / (radius * radius), 0.0));
    if (muSun < horizon)
    {
        return vec3(0.0, 0.0, 0.0);
    }
    return hbgAtmosphereTransmittance(radius, muSun, hbgAtmosphereRayLimit(radius, muSun));
}

vec3 hbgAtmosphereCameraPosition()
{
    return vec3(0.0, u_atmosphere_params.x + max(u_global_cameraPos.y, 0.0), 0.0);
}

vec3 hbgAtmosphereSunDirection()
{
    return normalize(u_pass_lightDirection.xyz);
}

vec3 hbgAtmosphereScattering(vec3 camera, vec3 viewDirection, vec3 sunDirection, float maxDistance,
                             int stepCount, out vec3 transmittance)
{
    const float radius = max(length(camera), u_atmosphere_params.x);
    const vec3 view = normalize(viewDirection);
    const vec3 sun = normalize(sunDirection);
    const float mu = clamp(dot(camera, view) / radius, -1.0, 1.0);
    const float muSun = clamp(dot(camera, sun) / radius, -1.0, 1.0);
    const float nu = clamp(dot(view, sun), -1.0, 1.0);
    const float limitDistance = hbgAtmosphereRayLimit(radius, mu);
    const float distance = max(min(maxDistance, limitDistance), 0.0);
    transmittance = hbgAtmosphereTransmittance(radius, mu, distance);
    if (distance <= 0.0)
    {
        return vec3(0.0, 0.0, 0.0);
    }

    const int steps = clamp(stepCount, 1, 64);
    const float stepLength = distance / float(steps);
    const float groundRadius = u_atmosphere_params.x;
    vec3 rayleigh = vec3(0.0, 0.0, 0.0);
    vec3 mie = vec3(0.0, 0.0, 0.0);
    for (int index = 0; index < 64; ++index)
    {
        if (index >= steps)
        {
            break;
        }

        const float t = (float(index) + 0.5) * stepLength;
        const float sampleRadius =
            sqrt(max(radius * radius + t * t + 2.0 * radius * mu * t, groundRadius * groundRadius));
        const float sampleMuSun = clamp((nu * t + muSun * radius) / sampleRadius, -1.0, 1.0);
        const float sampleHorizon =
            -sqrt(max(1.0 - (groundRadius * groundRadius) / (sampleRadius * sampleRadius), 0.0));
        if (sampleMuSun < sampleHorizon)
        {
            continue;
        }

        const vec3 viewTransmittance = hbgAtmosphereTransmittance(radius, mu, t);
        const vec3 sunTransmittance = hbgAtmosphereSunTransmittance(sampleRadius, sampleMuSun);
        const float densityRayleigh = exp(-(sampleRadius - groundRadius) / u_atmosphere_params.z);
        const float densityMie = exp(-(sampleRadius - groundRadius) / u_atmosphere_params.w);
        rayleigh += viewTransmittance * sunTransmittance * (densityRayleigh * stepLength);
        mie += viewTransmittance * sunTransmittance * (densityMie * stepLength);
    }

    const float phaseRayleigh = (3.0 / (16.0 * HBG_PI)) * (1.0 + nu * nu);
    const float g = u_atmosphere_params2.y;
    const float mieDenom = max(1.0 + g * g - 2.0 * g * nu, 1.0e-6);
    const float phaseMie = (1.5 * (1.0 - g * g) / (4.0 * HBG_PI)) *
                           pow(mieDenom, -1.5) * (1.0 + nu * nu) / (2.0 + g * g);
    const vec3 result = rayleigh * u_atmosphere_betaR.rgb * phaseRayleigh +
                        mie * u_atmosphere_betaM.rgb * phaseMie;
    return max(result, vec3(0.0, 0.0, 0.0)) * u_atmosphere_params2.x;
}

vec3 hbgAtmosphereTransmittanceRatio(float radius, float mu, float radius1, float mu1)
{
    if (mu > 0.0)
    {
        const vec3 transmittance0 = hbgAtmosphereTransmittanceLut(radius, mu);
        const vec3 transmittance1 = hbgAtmosphereTransmittanceLut(radius1, mu1);
        return min(transmittance0 / max(transmittance1, vec3(1.0e-6, 1.0e-6, 1.0e-6)),
                   vec3(1.0, 1.0, 1.0));
    }
    const vec3 transmittance0 = hbgAtmosphereTransmittanceLut(radius, -mu);
    const vec3 transmittance1 = hbgAtmosphereTransmittanceLut(radius1, -mu1);
    return min(transmittance1 / max(transmittance0, vec3(1.0e-6, 1.0e-6, 1.0e-6)),
               vec3(1.0, 1.0, 1.0));
}

vec3 hbgAtmospherePrecomputedSky(vec3 camera, vec3 viewDirection)
{
    const vec3 view = normalize(viewDirection);
    const vec3 sun = hbgAtmosphereSunDirection();
    float radius = length(camera);
    float rMu = dot(camera, view);
    float mu = rMu / max(radius, 1.0e-6);
    const float topRadius = u_atmosphere_params.y;
    const float deltaSq = sqrt(max(rMu * rMu - radius * radius + topRadius * topRadius, 0.0));
    const float enterDistance = max(-rMu - deltaSq, 0.0);
    if (enterDistance > 0.0)
    {
        camera += enterDistance * view;
        rMu += enterDistance;
        radius = topRadius;
        mu = rMu / radius;
    }

    vec3 result = vec3(0.0, 0.0, 0.0);
    if (radius <= topRadius)
    {
        const float nu = clamp(dot(view, sun), -1.0, 1.0);
        const float muSun = clamp(dot(camera, sun) / max(radius, 1.0e-6), -1.0, 1.0);
        const vec4 inScatter = hbgAtmosphereTexture4D(radius, rMu / max(radius, 1.0e-6), muSun, nu);
        const vec4 multipleScattering =
            hbgAtmosphereMultipleScattering4D(radius, rMu / max(radius, 1.0e-6), muSun, nu);
        result = (inScatter.rgb * hbgAtmospherePhaseRayleigh(nu) +
                  hbgAtmosphereMieFromLut(inScatter) * hbgAtmospherePhaseMie(nu) +
                  multipleScattering.rgb) *
                 u_atmosphere_params2.x;
    }
    return result;
}

vec3 hbgAtmospherePrecomputedAerialTransmittance(vec3 camera, vec3 worldPosition)
{
    const vec3 delta = worldPosition - camera;
    float distance = length(delta);
    if (distance <= 1.0e-4)
    {
        return vec3(1.0, 1.0, 1.0);
    }

    const vec3 view = delta / distance;
    vec3 atmosphereCamera = hbgAtmosphereCameraPosition();
    vec3 surfacePoint = atmosphereCamera + delta;
    float radius = length(atmosphereCamera);
    float rMu = dot(atmosphereCamera, view);
    float mu = rMu / max(radius, 1.0e-6);
    const float topRadius = u_atmosphere_params.y;
    const float deltaSq = sqrt(max(rMu * rMu - radius * radius + topRadius * topRadius, 0.0));
    const float enterDistance = max(-rMu - deltaSq, 0.0);
    if (enterDistance > 0.0 && enterDistance < distance)
    {
        atmosphereCamera += enterDistance * view;
        rMu += enterDistance;
        radius = topRadius;
        mu = rMu / radius;
        distance -= enterDistance;
    }

    if (radius > topRadius)
    {
        return vec3(1.0, 1.0, 1.0);
    }

    if (radius < u_atmosphere_params.x + 600.0)
    {
        const float scale = (u_atmosphere_params.x + 600.0) / radius;
        radius *= scale;
        rMu *= scale;
        surfacePoint *= scale;
    }

    const float radius1 = length(surfacePoint);
    const float rMu1 = dot(surfacePoint, view);
    const float mu1 = rMu1 / max(radius1, 1.0e-6);
    return hbgAtmosphereTransmittanceRatio(radius, mu, radius1, mu1);
}

vec3 hbgAtmospherePrecomputedAerialPerspective(vec3 camera, vec3 worldPosition)
{
    const vec3 delta = worldPosition - camera;
    float distance = length(delta);
    if (distance <= 1.0e-4)
    {
        return vec3(0.0, 0.0, 0.0);
    }

    const vec3 view = delta / distance;
    const vec3 sun = hbgAtmosphereSunDirection();
    vec3 atmosphereCamera = hbgAtmosphereCameraPosition();
    vec3 surfacePoint = atmosphereCamera + delta;
    float radius = length(atmosphereCamera);
    float rMu = dot(atmosphereCamera, view);
    float mu = rMu / max(radius, 1.0e-6);
    const float topRadius = u_atmosphere_params.y;
    const float deltaSq = sqrt(max(rMu * rMu - radius * radius + topRadius * topRadius, 0.0));
    const float enterDistance = max(-rMu - deltaSq, 0.0);
    if (enterDistance > 0.0 && enterDistance < distance)
    {
        atmosphereCamera += enterDistance * view;
        rMu += enterDistance;
        radius = topRadius;
        mu = rMu / radius;
        distance -= enterDistance;
    }

    if (radius > topRadius)
    {
        return vec3(0.0, 0.0, 0.0);
    }

    const float nu = clamp(dot(view, sun), -1.0, 1.0);
    const float muSun = clamp(dot(atmosphereCamera, sun) / max(radius, 1.0e-6), -1.0, 1.0);
    if (radius < u_atmosphere_params.x + 600.0)
    {
        const float scale = (u_atmosphere_params.x + 600.0) / radius;
        radius *= scale;
        rMu *= scale;
        surfacePoint *= scale;
    }

    const float radius1 = length(surfacePoint);
    const float rMu1 = dot(surfacePoint, view);
    const float mu1 = rMu1 / max(radius1, 1.0e-6);
    const float muSun1 = clamp(dot(surfacePoint, sun) / max(radius1, 1.0e-6), -1.0, 1.0);
    const vec3 extinction = hbgAtmosphereTransmittanceRatio(radius, mu, radius1, mu1);
    vec4 inScatter = hbgAtmosphereTexture4D(radius, rMu / max(radius, 1.0e-6), muSun, nu);
    const vec4 inScatter1 = hbgAtmosphereTexture4D(radius1, mu1, muSun1, nu);
    inScatter = max(inScatter - inScatter1 * vec4(extinction.x, extinction.y, extinction.z, extinction.x),
                    vec4(0.0, 0.0, 0.0, 0.0));
    inScatter.w *= smoothstep(0.0, 0.02, muSun);
    const vec4 multipleScattering =
        hbgAtmosphereMultipleScattering4D(radius, rMu / max(radius, 1.0e-6), muSun, nu);
    const vec4 multipleScattering1 =
        hbgAtmosphereMultipleScattering4D(radius1, mu1, muSun1, nu);
    const vec3 multipleScatteringDifference =
        max(multipleScattering.rgb - multipleScattering1.rgb * extinction,
            vec3(0.0, 0.0, 0.0));
    return (inScatter.rgb * hbgAtmospherePhaseRayleigh(nu) +
            hbgAtmosphereMieFromLut(inScatter) * hbgAtmospherePhaseMie(nu) +
            multipleScatteringDifference) *
           u_atmosphere_params2.x;
}

vec3 hbgAtmosphereAerialTransmittance(vec3 camera, vec3 worldPosition)
{
    if (u_atmosphere_params4.x > 0.5)
    {
        return hbgAtmospherePrecomputedAerialTransmittance(camera, worldPosition);
    }
    const vec3 delta = worldPosition - camera;
    const float distance = length(delta);
    if (distance <= 1.0e-4)
    {
        return vec3(1.0, 1.0, 1.0);
    }
    const vec3 atmosphereCamera = hbgAtmosphereCameraPosition();
    const float radius = length(atmosphereCamera);
    const float mu = clamp(dot(atmosphereCamera, delta / distance) / max(radius, 1.0e-6), -1.0, 1.0);
    return hbgAtmosphereTransmittance(radius, mu, distance);
}

vec3 hbgAtmosphereSkyRadiance(vec3 camera, vec3 direction)
{
    if (u_atmosphere_params4.x > 0.5)
    {
        return hbgAtmospherePrecomputedSky(camera, direction);
    }
    vec3 transmittance = vec3(1.0, 1.0, 1.0);
    return hbgAtmosphereScattering(camera, direction, hbgAtmosphereSunDirection(), 1.0e30,
                                   int(u_atmosphere_params2.z + 0.5), transmittance);
}

vec3 hbgAtmosphereAerialPerspective(vec3 camera, vec3 worldPosition)
{
    if (u_atmosphere_params4.x > 0.5)
    {
        return hbgAtmospherePrecomputedAerialPerspective(camera, worldPosition);
    }
    const vec3 delta = worldPosition - camera;
    const float distance = length(delta);
    if (distance <= 1.0e-4)
    {
        return vec3(0.0, 0.0, 0.0);
    }
    const vec3 atmosphereCamera = hbgAtmosphereCameraPosition();
    vec3 transmittance = vec3(1.0, 1.0, 1.0);
    return hbgAtmosphereScattering(atmosphereCamera, delta / distance, hbgAtmosphereSunDirection(), distance,
                                   int(u_atmosphere_params3.y + 0.5), transmittance);
}
#endif // HBG_ATMOSPHERE_SC


