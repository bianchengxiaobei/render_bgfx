$input v_texcoord0

#include "common/common.sc"

SAMPLER2D(s_hdrColor, 0);

// Ported from HDRP/Core Color.hlsl AcesTonemap approximation (ACES RRT+ODT).
// Matrices use explicit rows so D3D and Vulkan shader compilers agree.

vec3 hbgMulRows(vec3 r0, vec3 r1, vec3 r2, vec3 value)
{
    return vec3(dot(r0, value), dot(r1, value), dot(r2, value));
}

float hbgRgb2Saturation(vec3 rgb)
{
    const float tiny = 1.0e-4;
    float minimum = min(min(rgb.r, rgb.g), rgb.b);
    float maximum = max(max(rgb.r, rgb.g), rgb.b);
    return (max(maximum, tiny) - max(minimum, tiny)) / max(maximum, 1.0e-2);
}

float hbgRgb2Yc(vec3 rgb)
{
    const float ycRadiusWeight = 1.75;
    float k = rgb.b * (rgb.b - rgb.g) + rgb.g * (rgb.g - rgb.r) + rgb.r * (rgb.r - rgb.b);
    float chroma = sqrt(max(k, 0.0));
    return (rgb.r + rgb.g + rgb.b + ycRadiusWeight * chroma) / 3.0;
}

float hbgAtan2(float y, float x)
{
    if (abs(x) < 1.0e-6) {
        return y >= 0.0 ? HBG_PI * 0.5 : -HBG_PI * 0.5;
    }
    float angle = atan(y / x);
    if (x < 0.0) {
        angle += y >= 0.0 ? HBG_PI : -HBG_PI;
    }
    return angle;
}

float hbgRgb2Hue(vec3 rgb)
{
    float hue;
    if (rgb.r == rgb.g && rgb.g == rgb.b) {
        hue = 0.0;
    } else {
        hue = (180.0 / HBG_PI) * hbgAtan2(sqrt(3.0) * (rgb.g - rgb.b), 2.0 * rgb.r - rgb.g - rgb.b);
    }
    if (hue < 0.0) {
        hue += 360.0;
    }
    return hue;
}

float hbgCenterHue(float hue, float centerHue)
{
    float centered = hue - centerHue;
    if (centered < -180.0) {
        centered += 360.0;
    } else if (centered > 180.0) {
        centered -= 360.0;
    }
    return centered;
}

float hbgSigmoidShaper(float x)
{
    float t = max(1.0 - abs(x / 2.0), 0.0);
    float sign = x >= 0.0 ? 1.0 : -1.0;
    return (1.0 + sign * (1.0 - t * t)) / 2.0;
}

float hbgGlowFwd(float ycIn, float glowGainIn, float glowMid)
{
    if (ycIn <= 2.0 / 3.0 * glowMid) {
        return glowGainIn;
    }
    if (ycIn >= 2.0 * glowMid) {
        return 0.0;
    }
    return glowGainIn * (glowMid / ycIn - 0.5);
}

vec3 hbgXyzToXyY(vec3 xyz)
{
    float divisor = max(dot(xyz, vec3(1.0, 1.0, 1.0)), 1.0e-4);
    return vec3(xyz.x / divisor, xyz.y / divisor, xyz.y);
}

vec3 hbgXyYToXyz(vec3 xyY)
{
    float m = xyY.z / max(xyY.y, 1.0e-4);
    vec3 xyz = vec3(xyY.x, xyY.z, 1.0 - xyY.x - xyY.y);
    xyz.x *= m;
    xyz.z *= m;
    return xyz;
}

vec3 hbgDarkSurroundToDimSurround(vec3 linearCV)
{
    const float dimSurroundGamma = 0.9811;
    const vec3 kAp1ToXyzR0 = vec3( 0.6624541811,  0.1340042065,  0.1561876870);
    const vec3 kAp1ToXyzR1 = vec3( 0.2722287168,  0.6740817658,  0.0536895174);
    const vec3 kAp1ToXyzR2 = vec3(-0.0055746495,  0.0040607335,  1.0103391003);
    const vec3 kXyzToAp1R0 = vec3( 1.6410233797, -0.3248032942, -0.2364246952);
    const vec3 kXyzToAp1R1 = vec3(-0.6636628587,  1.6153315917,  0.0167563477);
    const vec3 kXyzToAp1R2 = vec3( 0.0117218943, -0.0082844420,  0.9883948585);
    vec3 xyz = hbgMulRows(kAp1ToXyzR0, kAp1ToXyzR1, kAp1ToXyzR2, linearCV);
    vec3 xyY = hbgXyzToXyY(xyz);
    xyY.z = clamp(xyY.z, 0.0, 1.0e12);
    xyY.z = pow(xyY.z, dimSurroundGamma);
    xyz = hbgXyYToXyz(xyY);
    return hbgMulRows(kXyzToAp1R0, kXyzToAp1R1, kXyzToAp1R2, xyz);
}

vec3 hbgAcesTonemap(vec3 srgbLinear)
{
    const float rrtGlowGain = 0.05;
    const float rrtGlowMid = 0.08;
    const float rrtRedScale = 0.82;
    const float rrtRedPivot = 0.03;
    const float rrtRedWidth = 135.0;
    const float rrtSatFactor = 0.96;
    const float odtSatFactor = 0.93;
    const float a = 0.0245786;
    const float b = 0.000090537;
    const float c = 0.983729;
    const float d = 0.4329510;
    const float e = 0.238081;
    const vec3 kSrgbToAp0R0 = vec3(0.4397010, 0.3829780, 0.1773350);
    const vec3 kSrgbToAp0R1 = vec3(0.0897923, 0.8134230, 0.0967616);
    const vec3 kSrgbToAp0R2 = vec3(0.0175440, 0.1115440, 0.8707040);
    const vec3 kAp0ToAp1R0 = vec3( 1.4514393161, -0.2365107469, -0.2149285693);
    const vec3 kAp0ToAp1R1 = vec3(-0.0765537734,  1.1762296998, -0.0996759264);
    const vec3 kAp0ToAp1R2 = vec3( 0.0083161484, -0.0060324498,  0.9977163014);
    const vec3 kAp1ToXyzR0 = vec3( 0.6624541811,  0.1340042065,  0.1561876870);
    const vec3 kAp1ToXyzR1 = vec3( 0.2722287168,  0.6740817658,  0.0536895174);
    const vec3 kAp1ToXyzR2 = vec3(-0.0055746495,  0.0040607335,  1.0103391003);
    const vec3 kXyzToRec709R0 = vec3( 3.2409699419, -1.5373831776, -0.4986107603);
    const vec3 kXyzToRec709R1 = vec3(-0.9692436363,  1.8759675015,  0.0415550574);
    const vec3 kXyzToRec709R2 = vec3( 0.0556300797, -0.2039769589,  1.0569715142);
    const vec3 kD60ToD65R0 = vec3( 0.98722400, -0.00611327,  0.01595330);
    const vec3 kD60ToD65R1 = vec3(-0.00759836,  1.00186000,  0.00533020);
    const vec3 kD60ToD65R2 = vec3( 0.00307257, -0.00509595,  1.08168000);
    const vec3 kAp1Rgb2Y = vec3(0.272229, 0.674082, 0.0536895);

    vec3 aces = hbgMulRows(kSrgbToAp0R0, kSrgbToAp0R1, kSrgbToAp0R2, srgbLinear);

    float saturation = hbgRgb2Saturation(aces);
    float ycIn = hbgRgb2Yc(aces);
    float s = hbgSigmoidShaper((saturation - 0.4) / 0.2);
    aces *= 1.0 + hbgGlowFwd(ycIn, rrtGlowGain * s, rrtGlowMid);

    float hue = hbgRgb2Hue(aces);
    float centeredHue = hbgCenterHue(hue, 0.0);
    float hueWeight = hbgSaturate(1.0 - abs(2.0 * centeredHue / rrtRedWidth));
    hueWeight *= hueWeight;
    aces.r += hueWeight * saturation * (rrtRedPivot - aces.r) * (1.0 - rrtRedScale);

    vec3 acescg = max(hbgMulRows(kAp0ToAp1R0, kAp0ToAp1R1, kAp0ToAp1R2, aces), vec3(0.0, 0.0, 0.0));
    acescg = mix(vec3(dot(acescg, kAp1Rgb2Y), dot(acescg, kAp1Rgb2Y), dot(acescg, kAp1Rgb2Y)), acescg, vec3(rrtSatFactor, rrtSatFactor, rrtSatFactor));

    vec3 rgbPost = (acescg * (acescg + vec3(a, a, a)) - vec3(b, b, b)) /
                   (acescg * (vec3(c, c, c) * acescg + vec3(d, d, d)) + vec3(e, e, e));

    vec3 linearCV = hbgDarkSurroundToDimSurround(rgbPost);
    linearCV = mix(vec3(dot(linearCV, kAp1Rgb2Y), dot(linearCV, kAp1Rgb2Y), dot(linearCV, kAp1Rgb2Y)), linearCV, vec3(odtSatFactor, odtSatFactor, odtSatFactor));

    vec3 xyz = hbgMulRows(kAp1ToXyzR0, kAp1ToXyzR1, kAp1ToXyzR2, linearCV);
    xyz = hbgMulRows(kD60ToD65R0, kD60ToD65R1, kD60ToD65R2, xyz);
    return hbgMulRows(kXyzToRec709R0, kXyzToRec709R1, kXyzToRec709R2, xyz);
}

float hbgComputeAutoExposureEv100()
{
    const int kGrid = 4;
    float evSum = 0.0;
    float weightSum = 0.0;
    for (int y = 0; y < kGrid; ++y) {
        for (int x = 0; x < kGrid; ++x) {
            vec2 uv = (vec2(float(x), float(y)) + vec2(0.5, 0.5)) / float(kGrid);
            vec3 color = max(texture2D(s_hdrColor, uv).rgb, vec3(0.0, 0.0, 0.0));
            float luminance = dot(color, vec3(0.2126729, 0.7151522, 0.0721750));
            if (!(luminance == luminance)) {
                luminance = 1.0e-4;
            }
            luminance = min(max(luminance, 1.0e-4), 1.0e4);
            float ev100 = log2(luminance * 100.0 / 12.5);

            float diagonal = 0.5 * (float(kGrid) + float(kGrid));
            vec2 pixel = vec2(float(x), float(y));
            float weight = 1.0 - hbgSaturate(length(pixel - vec2(1.5, 1.5)) / diagonal);
            evSum += ev100 * weight;
            weightSum += weight;
        }
    }

    float averageEv = evSum / max(weightSum, 1.0e-6);
    return clamp(averageEv - u_postprocess_exposureParams.z, u_postprocess_autoExposureParams.x,
                 u_postprocess_autoExposureParams.y);
}

vec3 hbgNeutralCurve(vec3 x)
{
    const float a = 0.2;
    const float b = 0.29;
    const float c = 0.24;
    const float d = 0.272;
    const float e = 0.02;
    const float f = 0.3;
    return ((x * (a * x + c * b) + d * e) / (x * (a * x + b) + d * f)) - e / f;
}

vec3 hbgNeutralTonemap(vec3 x)
{
    x = max(x, vec3(0.0, 0.0, 0.0));
    const float whiteLevel = 5.3;
    vec3 whiteScale = vec3(1.0, 1.0, 1.0) / hbgNeutralCurve(vec3(whiteLevel, whiteLevel, whiteLevel));
    x = hbgNeutralCurve(x * whiteScale);
    return x * whiteScale;
}

void main()
{
    vec3 hdr = max(texture2D(s_hdrColor, v_texcoord0).rgb, vec3(0.0, 0.0, 0.0));
    float exposure = 1.0;
    if (u_postprocess_exposureParams.x < 0.5) {
        exposure = u_postprocess_exposureParams.y;
    } else if (u_postprocess_exposureParams.x < 1.5) {
        const float ev100 = hbgComputeAutoExposureEv100();
        exposure = 1.0 / max(u_postprocess_exposureParams.w * exp2(ev100), 1.0e-6);
    }
    hdr *= max(exposure, 0.0);

    float tonemapMode = u_postprocess_tonemapParams.x;
    vec3 mapped = hdr;
    if (tonemapMode > 1.5) {
        mapped = hbgAcesTonemap(hdr);
    } else if (tonemapMode > 0.5) {
        mapped = hbgNeutralTonemap(hdr);
    }

    gl_FragColor = vec4(hbgLinearToSrgb(max(mapped, vec3(0.0, 0.0, 0.0))), 1.0);
}

