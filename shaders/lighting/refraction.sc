#ifndef HBG_REFRACTION_SC
#define HBG_REFRACTION_SC

#include "common/common.sc"

float hbgRefractionFeature(float features)
{
    return mod(floor(features / 2.0), 2.0);
}

// HDRP-style screen-space refraction. The source color is copied after opaque
// and pre-refraction draws. Rough surfaces use HDRP ColorPyramidPS Gaussian
// weights directly in screen space (a v1 approximation of the color pyramid).
vec3 hbgApplyScreenSpaceRefraction(vec3 color, vec3 worldPos, vec3 viewWS, vec3 normalWS,
                                   vec2 screenUV, float transmission, float thickness, float ior,
                                   float enabled, float perceptualRoughness)
{
#ifdef HBG_CONTACT_SHADOW_TEXTURE
    if (enabled < 0.5 || transmission <= 0.0)
    {
        return color;
    }

    const vec3 V = normalize(viewWS);
    const vec3 N = normalize(normalWS);

    // Refraction.hlsl RefractionModelBox: planar approximation with the
    // supplied thickness and index of refraction.
    vec3 R = refract(-V, N, 1.0 / max(ior, 1.0));
    if (dot(R, R) < 1.0e-8)
    {
        R = -V;
    }

    const float opticalThickness = max(thickness, 0.01);
    const vec4 currentClip = mul(u_global_viewProj, vec4(worldPos, 1.0));
    const vec4 refractedClip = mul(u_global_viewProj, vec4(worldPos + R * opticalThickness, 1.0));
    if (currentClip.w <= 1.0e-4 || refractedClip.w <= 1.0e-4)
    {
        return color;
    }

    const vec2 currentNDC = currentClip.xy / currentClip.w;
    const vec2 refractedNDC = refractedClip.xy / refractedClip.w;
    vec2 offset = (refractedNDC - currentNDC) * 0.5;
    offset = clamp(offset, vec2(-0.1, -0.1), vec2(0.1, 0.1));

    // The source copy stores the pre-refraction linear depth in alpha. It is
    // used for the HDRP refractionOffsetMultiplier depth rejection: when the
    // offset texel is in front of the refracting surface, fall back to the
    // center texel instead of sampling an occluder through the surface.
    const float maxMip = max(floor(log2(max(u_global_screenSize.x, u_global_screenSize.y))), 0.0);
    const float roughnessEnabled = step(0.5, u_pass_litVariantParams.w);
    const float mipLevel = roughnessEnabled * pow(hbgSaturate(perceptualRoughness), 1.3) * maxMip;

    const vec2 sampleUV = clamp(screenUV + offset, vec2(0.0, 0.0), vec2(1.0, 1.0));
    const vec4 centerSample = texture2DLod(s_contactShadow, screenUV, 0.0);
    if (centerSample.a <= 0.0)
    {
        return color;
    }
    const vec4 offsetSample = texture2DLod(s_contactShadow, sampleUV, 0.0);
    float depthReject = 1.0;
    if (offsetSample.a < 0.999999)
    {
        vec4 sampleWorld = mul(u_global_invViewProj, vec4(sampleUV * 2.0 - 1.0, offsetSample.a, 1.0));
        sampleWorld /= max(sampleWorld.w, 1.0e-4);
        const float sampleLinear = max(-mul(u_global_view, vec4(sampleWorld.xyz, 1.0)).z, 0.0);
        const float currentLinear = max(-mul(u_global_view, vec4(worldPos, 1.0)).z, 0.0);
        depthReject = (offsetSample.a > 0.0 && sampleLinear > currentLinear + 1.0e-4) ? 1.0 : 0.0;
    }
    // HDRP Lit.hlsl: reduce the offset for rough refraction, then apply the
    // depth-rejection multiplier before sampling the color pyramid.
    const float refractionOffsetMultiplier = max(0.0, 1.0 - mipLevel * 0.08);
    const vec2 resolvedUV = mix(screenUV, sampleUV, depthReject * refractionOffsetMultiplier);

    // Fade the offset near the source border instead of introducing a hard
    // clamp seam. This is the v1 equivalent of HDRP's EdgeOfScreenFade.
    const vec2 border = smoothstep(vec2(0.0, 0.0), vec2(0.02, 0.02), resolvedUV) *
                        smoothstep(vec2(0.0, 0.0), vec2(0.02, 0.02),
                                   vec2(1.0, 1.0) - resolvedUV);
    const float edgeWeight = border.x * border.y;

    // HDRP ColorPyramidPS weights applied directly in screen space. This v1
    // approximates the Gaussian color pyramid (the HDRP path samples a mip
    // selected from perceptual roughness) without allocating a mip chain.
    const float w0 = 0.27343750;
    const float w1 = 0.21875000;
    const float w2 = 0.10937500;
    const float w3 = 0.03125000;
    const float w4 = 0.00390625;
    const float blurRadius = hbgSaturate(perceptualRoughness) * 0.02 * roughnessEnabled;
    const vec2 d1 = vec2(blurRadius, blurRadius) * (1.0 + (w2 / (w1 + w2)));
    const vec2 d2 = vec2(blurRadius, blurRadius) * (3.0 + (w4 / (w3 + w4)));
    const vec2 uvM2 = clamp(resolvedUV - d2, vec2(0.0, 0.0), vec2(1.0, 1.0));
    const vec2 uvM1 = clamp(resolvedUV - d1, vec2(0.0, 0.0), vec2(1.0, 1.0));
    const vec2 uvP1 = clamp(resolvedUV + d1, vec2(0.0, 0.0), vec2(1.0, 1.0));
    const vec2 uvP2 = clamp(resolvedUV + d2, vec2(0.0, 0.0), vec2(1.0, 1.0));
    const vec3 refracted =
        texture2DLod(s_contactShadow, uvM2, 0.0).rgb * (w3 + w4) +
        texture2DLod(s_contactShadow, uvM1, 0.0).rgb * (w1 + w2) +
        texture2DLod(s_contactShadow, resolvedUV, 0.0).rgb * w0 +
        texture2DLod(s_contactShadow, uvP1, 0.0).rgb * (w1 + w2) +
        texture2DLod(s_contactShadow, uvP2, 0.0).rgb * (w3 + w4);

    // Approximate HDRP's (1 - specularFGD) transmittance with a Schlick
    // Fresnel term.
    const float NdotV = hbgSaturate(abs(dot(N, V)));
    const float fresnel = pow(1.0 - NdotV, 5.0);
    const float weight = hbgSaturate(transmission) * (1.0 - fresnel) * edgeWeight;
    return mix(color, refracted, weight);
#else
    return color;
#endif
}

#endif // HBG_REFRACTION_SC
