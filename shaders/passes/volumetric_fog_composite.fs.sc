$input v_texcoord0

#include "common/common.sc"

SAMPLER2D(s_fogSource, 0);
SAMPLER2D(s_fogLowRes, 1);
SAMPLER2D(s_depthSource, 2);

float hbgFogViewDepth(vec2 depthUv, float rawDepth)
{
    if (rawDepth >= 0.999999)
    {
        return 1.0e6;
    }
    const vec4 clip = vec4(depthUv * 2.0 - 1.0, rawDepth, 1.0);
    vec4 world = mul(u_global_invViewProj, clip);
    world.xyz /= max(world.w, 1.0e-6);
    return max(-mul(u_global_view, vec4(world.xyz, 1.0)).z, 0.0);
}

void hbgAccumulateFogTap(vec2 lowPixel, vec2 lowSize, float bilinear, float centerViewDepth,
                         inout vec3 inScattering, inout float transmittance, inout float totalWeight)
{
    const vec2 tapUv = (lowPixel + vec2(0.5, 0.5)) / lowSize;
    const vec2 depthUv = vec2(tapUv.x, 1.0 - tapUv.y);
    const float tapDepth = texture2D(s_depthSource, depthUv).r;
    const float tapViewDepth = hbgFogViewDepth(depthUv, tapDepth);
    const float depthTolerance = max(centerViewDepth * 0.02, 0.05);
    const float depthWeight = 1.0 - hbgSaturate(abs(tapViewDepth - centerViewDepth) / depthTolerance);
    const float weight = bilinear * depthWeight;
    if (weight <= 0.0)
    {
        return;
    }

    const vec4 fog = texture2D(s_fogLowRes, tapUv);
    inScattering += max(fog.rgb, vec3(0.0, 0.0, 0.0)) * weight;
    transmittance += clamp(fog.a, 0.0, 1.0) * weight;
    totalWeight += weight;
}

void main()
{
    const vec2 uv = v_texcoord0;
    const vec2 sourceUv = vec2(uv.x, 1.0 - uv.y);
    const vec3 sourceColor = texture2D(s_fogSource, sourceUv).rgb;
    const float rawDepth = texture2D(s_depthSource, uv).r;
    const float centerViewDepth = hbgFogViewDepth(uv, rawDepth);

    const vec2 lowSize = max(vec2(u_fog_params4.x, u_fog_params4.y), vec2(1.0, 1.0));
    const vec2 lowPosition = sourceUv * lowSize - vec2(0.5, 0.5);
    const vec2 base = floor(lowPosition);
    const vec2 fraction = lowPosition - base;
    const vec2 lowMax = lowSize - vec2(1.0, 1.0);

    vec3 inScattering = vec3(0.0, 0.0, 0.0);
    float transmittance = 0.0;
    float totalWeight = 0.0;
    hbgAccumulateFogTap(clamp(base + vec2(0.0, 0.0), vec2(0.0, 0.0), lowMax), lowSize,
                        (1.0 - fraction.x) * (1.0 - fraction.y), centerViewDepth, inScattering,
                        transmittance, totalWeight);
    hbgAccumulateFogTap(clamp(base + vec2(1.0, 0.0), vec2(0.0, 0.0), lowMax), lowSize,
                        fraction.x * (1.0 - fraction.y), centerViewDepth, inScattering, transmittance,
                        totalWeight);
    hbgAccumulateFogTap(clamp(base + vec2(0.0, 1.0), vec2(0.0, 0.0), lowMax), lowSize,
                        (1.0 - fraction.x) * fraction.y, centerViewDepth, inScattering, transmittance,
                        totalWeight);
    hbgAccumulateFogTap(clamp(base + vec2(1.0, 1.0), vec2(0.0, 0.0), lowMax), lowSize,
                        fraction.x * fraction.y, centerViewDepth, inScattering, transmittance, totalWeight);

    if (totalWeight <= 1.0e-4)
    {
        const vec4 fog = texture2D(s_fogLowRes, sourceUv);
        inScattering = max(fog.rgb, vec3(0.0, 0.0, 0.0));
        transmittance = clamp(fog.a, 0.0, 1.0);
        totalWeight = 1.0;
    }

    inScattering /= totalWeight;
    transmittance = clamp(transmittance / totalWeight, 0.0, 1.0);
    gl_FragColor = vec4(max(sourceColor * transmittance + inScattering, vec3(0.0, 0.0, 0.0)), 1.0);
}
