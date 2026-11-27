#include "render/postprocess/color_grading_lut.h"

#include <bx/math.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace hbg::render {
namespace {

constexpr float kLogCCut = 0.011361f;
constexpr float kLogCA = 5.555556f;
constexpr float kLogCB = 0.047996f;
constexpr float kLogCC = 0.244161f;
constexpr float kLogCD = 0.386036f;
constexpr float kLogCE = 5.301883f;
constexpr float kLogCF = 0.092819f;
constexpr float kAcesccMidGray = 0.4135884f;
constexpr float kLog10Of2 = 0.30102999566398119521f;

float saturate(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

Vec3 saturate(const Vec3& value) {
    return Vec3(saturate(value.x), saturate(value.y), saturate(value.z));
}

Vec3 maxZero(const Vec3& value) {
    return Vec3(std::max(value.x, 0.0f), std::max(value.y, 0.0f), std::max(value.z, 0.0f));
}

Vec3 multiply(const Vec3& lhs, const Vec3& rhs) {
    return Vec3(lhs.x * rhs.x, lhs.y * rhs.y, lhs.z * rhs.z);
}

Vec3 add(const Vec3& lhs, const Vec3& rhs) {
    return Vec3(lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z);
}

Vec3 subtract(const Vec3& lhs, const Vec3& rhs) {
    return Vec3(lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z);
}

Vec3 lerp(const Vec3& lhs, const Vec3& rhs, float t) {
    return add(lhs, (rhs - lhs) * t);
}

Vec3 positivePow(const Vec3& value, const Vec3& exponent) {
    return Vec3(std::copysign(std::pow(std::abs(value.x), exponent.x), value.x),
                std::copysign(std::pow(std::abs(value.y), exponent.y), value.y),
                std::copysign(std::pow(std::abs(value.z), exponent.z), value.z));
}

Vec3 positivePow(const Vec3& value, float exponent) {
    return positivePow(value, Vec3(exponent, exponent, exponent));
}

Vec3 mulRows(const Vec3& row0, const Vec3& row1, const Vec3& row2, const Vec3& value) {
    return Vec3(dot(row0, value), dot(row1, value), dot(row2, value));
}

float luminance(const Vec3& value) {
    return dot(value, Vec3(0.2126729f, 0.7151522f, 0.0721750f));
}

float smoothstep(float edge0, float edge1, float value) {
    const float t = saturate((value - edge0) / std::max(edge1 - edge0, 1.0e-6f));
    return t * t * (3.0f - 2.0f * t);
}

float rgb2Saturation(const Vec3& rgb) {
    constexpr float tiny = 1.0e-4f;
    const float minimum = std::min({rgb.x, rgb.y, rgb.z});
    const float maximum = std::max({rgb.x, rgb.y, rgb.z});
    return (std::max(maximum, tiny) - std::max(minimum, tiny)) / std::max(maximum, 1.0e-2f);
}

float rgb2Yc(const Vec3& rgb) {
    constexpr float ycRadiusWeight = 1.75f;
    const float k = rgb.z * (rgb.z - rgb.y) + rgb.y * (rgb.y - rgb.x) + rgb.x * (rgb.x - rgb.z);
    const float chroma = std::sqrt(std::max(k, 0.0f));
    return (rgb.x + rgb.y + rgb.z + ycRadiusWeight * chroma) / 3.0f;
}

float rgb2Hue(const Vec3& rgb) {
    if (rgb.x == rgb.y && rgb.y == rgb.z) {
        return 0.0f;
    }
    float hue = (180.0f / 3.14159265358979323846f) *
                std::atan2(std::sqrt(3.0f) * (rgb.y - rgb.z), 2.0f * rgb.x - rgb.y - rgb.z);
    if (hue < 0.0f) {
        hue += 360.0f;
    }
    return hue;
}

float centerHue(float hue, float center) {
    const float centered = hue - center;
    if (centered < -180.0f) {
        return centered + 360.0f;
    }
    if (centered > 180.0f) {
        return centered - 360.0f;
    }
    return centered;
}

float sigmoidShaper(float x) {
    const float t = std::max(1.0f - std::abs(x / 2.0f), 0.0f);
    const float sign = x >= 0.0f ? 1.0f : -1.0f;
    return (1.0f + sign * (1.0f - t * t)) / 2.0f;
}

float glowFwd(float ycIn, float glowGainIn, float glowMid) {
    if (ycIn <= 2.0f / 3.0f * glowMid) {
        return glowGainIn;
    }
    if (ycIn >= 2.0f * glowMid) {
        return 0.0f;
    }
    return glowGainIn * (glowMid / ycIn - 0.5f);
}

Vec3 xyzToXyY(const Vec3& xyz) {
    const float divisor = std::max(dot(xyz, Vec3(1.0f, 1.0f, 1.0f)), 1.0e-4f);
    return Vec3(xyz.x / divisor, xyz.y / divisor, xyz.y);
}

Vec3 xyYToXyz(const Vec3& xyY) {
    const float m = xyY.z / std::max(xyY.y, 1.0e-4f);
    Vec3 xyz(xyY.x, xyY.z, 1.0f - xyY.x - xyY.y);
    xyz.x *= m;
    xyz.z *= m;
    return xyz;
}

Vec3 darkSurroundToDimSurround(const Vec3& linearCV) {
    constexpr float dimSurroundGamma = 0.9811f;
    const Vec3 ap1ToXyzR0(0.6624541811f, 0.1340042065f, 0.1561876870f);
    const Vec3 ap1ToXyzR1(0.2722287168f, 0.6740817658f, 0.0536895174f);
    const Vec3 ap1ToXyzR2(-0.0055746495f, 0.0040607335f, 1.0103391003f);
    const Vec3 xyzToAp1R0(1.6410233797f, -0.3248032942f, -0.2364246952f);
    const Vec3 xyzToAp1R1(-0.6636628587f, 1.6153315917f, 0.0167563477f);
    const Vec3 xyzToAp1R2(0.0117218943f, -0.0082844420f, 0.9883948585f);

    Vec3 xyz = mulRows(ap1ToXyzR0, ap1ToXyzR1, ap1ToXyzR2, linearCV);
    Vec3 xyY = xyzToXyY(xyz);
    xyY.z = std::clamp(xyY.z, 0.0f, 1.0e12f);
    xyY.z = std::pow(xyY.z, dimSurroundGamma);
    xyz = xyYToXyz(xyY);
    return mulRows(xyzToAp1R0, xyzToAp1R1, xyzToAp1R2, xyz);
}

Vec3 acesTonemap(const Vec3& srgbLinear) {
    constexpr float rrtGlowGain = 0.05f;
    constexpr float rrtGlowMid = 0.08f;
    constexpr float rrtRedScale = 0.82f;
    constexpr float rrtRedPivot = 0.03f;
    constexpr float rrtRedWidth = 135.0f;
    constexpr float rrtSatFactor = 0.96f;
    constexpr float odtSatFactor = 0.93f;
    constexpr float a = 0.0245786f;
    constexpr float b = 0.000090537f;
    constexpr float c = 0.983729f;
    constexpr float d = 0.4329510f;
    constexpr float e = 0.238081f;
    const Vec3 srgbToAp0R0(0.4397010f, 0.3829780f, 0.1773350f);
    const Vec3 srgbToAp0R1(0.0897923f, 0.8134230f, 0.0967616f);
    const Vec3 srgbToAp0R2(0.0175440f, 0.1115440f, 0.8707040f);
    const Vec3 ap0ToAp1R0(1.4514393161f, -0.2365107469f, -0.2149285693f);
    const Vec3 ap0ToAp1R1(-0.0765537734f, 1.1762296998f, -0.0996759264f);
    const Vec3 ap0ToAp1R2(0.0083161484f, -0.0060324498f, 0.9977163014f);
    const Vec3 ap1ToXyzR0(0.6624541811f, 0.1340042065f, 0.1561876870f);
    const Vec3 ap1ToXyzR1(0.2722287168f, 0.6740817658f, 0.0536895174f);
    const Vec3 ap1ToXyzR2(-0.0055746495f, 0.0040607335f, 1.0103391003f);
    const Vec3 xyzToRec709R0(3.2409699419f, -1.5373831776f, -0.4986107603f);
    const Vec3 xyzToRec709R1(-0.9692436363f, 1.8759675015f, 0.0415550574f);
    const Vec3 xyzToRec709R2(0.0556300797f, -0.2039769589f, 1.0569715142f);
    const Vec3 d60ToD65R0(0.98722400f, -0.00611327f, 0.01595330f);
    const Vec3 d60ToD65R1(-0.00759836f, 1.00186000f, 0.00533020f);
    const Vec3 d60ToD65R2(0.00307257f, -0.00509595f, 1.08168000f);
    const Vec3 ap1Rgb2Y(0.272229f, 0.674082f, 0.0536895f);

    Vec3 aces = mulRows(srgbToAp0R0, srgbToAp0R1, srgbToAp0R2, srgbLinear);
    const float saturation = rgb2Saturation(aces);
    const float ycIn = rgb2Yc(aces);
    const float s = sigmoidShaper((saturation - 0.4f) / 0.2f);
    aces = aces * (1.0f + glowFwd(ycIn, rrtGlowGain * s, rrtGlowMid));

    const float hue = rgb2Hue(aces);
    const float centeredHue = centerHue(hue, 0.0f);
    float hueWeight = saturate(1.0f - std::abs(2.0f * centeredHue / rrtRedWidth));
    hueWeight *= hueWeight;
    aces.x += hueWeight * saturation * (rrtRedPivot - aces.x) * (1.0f - rrtRedScale);

    Vec3 acescg = maxZero(mulRows(ap0ToAp1R0, ap0ToAp1R1, ap0ToAp1R2, aces));
    const float acescgLuma = dot(acescg, ap1Rgb2Y);
    acescg = lerp(Vec3(acescgLuma, acescgLuma, acescgLuma), acescg, rrtSatFactor);

    const Vec3 rgbPost = Vec3(
        (acescg.x * (acescg.x + a) - b) / (acescg.x * (c * acescg.x + d) + e),
        (acescg.y * (acescg.y + a) - b) / (acescg.y * (c * acescg.y + d) + e),
        (acescg.z * (acescg.z + a) - b) / (acescg.z * (c * acescg.z + d) + e));

    Vec3 linearCV = darkSurroundToDimSurround(rgbPost);
    const float linearCvLuma = dot(linearCV, ap1Rgb2Y);
    linearCV = lerp(Vec3(linearCvLuma, linearCvLuma, linearCvLuma), linearCV, odtSatFactor);

    Vec3 xyz = mulRows(ap1ToXyzR0, ap1ToXyzR1, ap1ToXyzR2, linearCV);
    xyz = mulRows(d60ToD65R0, d60ToD65R1, d60ToD65R2, xyz);
    return mulRows(xyzToRec709R0, xyzToRec709R1, xyzToRec709R2, xyz);
}

Vec3 neutralCurve(const Vec3& x) {
    constexpr float a = 0.2f;
    constexpr float b = 0.29f;
    constexpr float c = 0.24f;
    constexpr float d = 0.272f;
    constexpr float e = 0.02f;
    constexpr float f = 0.3f;
    return Vec3(((x.x * (a * x.x + c * b) + d * e) / (x.x * (a * x.x + b) + d * f)) - e / f,
                ((x.y * (a * x.y + c * b) + d * e) / (x.y * (a * x.y + b) + d * f)) - e / f,
                ((x.z * (a * x.z + c * b) + d * e) / (x.z * (a * x.z + b) + d * f)) - e / f);
}

Vec3 neutralTonemap(const Vec3& input) {
    const Vec3 x = maxZero(input);
    constexpr float whiteLevel = 5.3f;
    const Vec3 whiteCurve = neutralCurve(Vec3(whiteLevel, whiteLevel, whiteLevel));
    const Vec3 whiteScale(1.0f / whiteCurve.x, 1.0f / whiteCurve.y, 1.0f / whiteCurve.z);
    return multiply(neutralCurve(multiply(x, whiteScale)), whiteScale);
}

Vec3 linearToLms(const Vec3& value) {
    const Vec3 r0(0.390405f, 0.549941f, 0.00892632f);
    const Vec3 r1(0.0708416f, 0.963172f, 0.00135775f);
    const Vec3 r2(0.0231082f, 0.128021f, 0.936245f);
    return mulRows(r0, r1, r2, value);
}

Vec3 lmsToLinear(const Vec3& value) {
    const Vec3 r0(2.85847f, -1.62879f, -0.0248910f);
    const Vec3 r1(-0.210182f, 1.15820f, 0.000324281f);
    const Vec3 r2(-0.0418120f, -0.118169f, 1.06867f);
    return mulRows(r0, r1, r2, value);
}

Vec3 colorBalanceCoefficients(float temperature, float tint) {
    const float t1 = temperature / 65.0f;
    const float t2 = tint / 65.0f;
    const float x = 0.31271f - t1 * (t1 < 0.0f ? 0.1f : 0.05f);
    const float y = 2.87f * x - 3.0f * x * x - 0.27509507f + t2 * 0.05f;

    const float Y = 1.0f;
    const float X = Y * x / y;
    const float Z = Y * (1.0f - x - y) / y;
    const Vec3 w2(0.7328f * X + 0.4296f * Y - 0.1624f * Z,
                  -0.7036f * X + 1.6975f * Y + 0.0061f * Z,
                  0.0030f * X + 0.0136f * Y + 0.9834f * Z);
    const Vec3 w1(0.949237f, 1.03542f, 1.08728f);
    return Vec3(w1.x / w2.x, w1.y / w2.y, w1.z / w2.z);
}

Vec3 softLight(const Vec3& base, const Vec3& blend) {
    const Vec3 r1(2.0f * base.x * blend.x + base.x * base.x * (1.0f - 2.0f * blend.x),
                  2.0f * base.y * blend.y + base.y * base.y * (1.0f - 2.0f * blend.y),
                  2.0f * base.z * blend.z + base.z * base.z * (1.0f - 2.0f * blend.z));
    const Vec3 r2(std::sqrt(base.x) * (2.0f * blend.x - 1.0f) + 2.0f * base.x * (1.0f - blend.x),
                  std::sqrt(base.y) * (2.0f * blend.y - 1.0f) + 2.0f * base.y * (1.0f - blend.y),
                  std::sqrt(base.z) * (2.0f * blend.z - 1.0f) + 2.0f * base.z * (1.0f - blend.z));
    return Vec3(blend.x >= 0.5f ? r2.x : r1.x, blend.y >= 0.5f ? r2.y : r1.y,
                blend.z >= 0.5f ? r2.z : r1.z);
}

Vec3 rgbToHsv(const Vec3& c) {
    const bool greenDominant = c.y >= c.z;
    const float p0 = greenDominant ? c.y : c.z;
    const float p1 = greenDominant ? c.z : c.y;
    const float p2 = greenDominant ? 0.0f : -1.0f;
    const float p3 = greenDominant ? -1.0f / 3.0f : 2.0f / 3.0f;

    const bool redDominant = c.x >= p0;
    const float q0 = redDominant ? c.x : p0;
    const float q1 = p1;
    const float q2 = redDominant ? p2 : p3;
    const float q3 = redDominant ? p0 : c.x;
    const float d = q0 - std::min(q3, q1);
    constexpr float e = 1.0e-4f;
    return Vec3(std::abs(q2 + (q3 - q1) / (6.0f * d + e)), d / (q0 + e), q0);
}

Vec3 hsvToRgb(const Vec3& c) {
    auto frac = [](float value) { return value - std::floor(value); };
    const Vec3 p(std::abs(frac(c.x + 0.0f) * 6.0f - 3.0f), std::abs(frac(c.x + 2.0f / 3.0f) * 6.0f - 3.0f),
                 std::abs(frac(c.x + 1.0f / 3.0f) * 6.0f - 3.0f));
    const Vec3 low(1.0f, 1.0f, 1.0f);
    const Vec3 high = saturate(subtract(p, low));
    return lerp(low, high, c.y) * c.z;
}

float rotateHue(float value, float low, float high) {
    if (value < low) {
        return value + high;
    }
    if (value > high) {
        return value - high;
    }
    return value;
}

} // namespace

uint32_t colorGradingLutStripIndex(uint32_t x, uint32_t y, uint32_t z) {
    return ((z * kColorGradingLutSize + y) * kColorGradingLutSize + x) * 4u;
}

uint32_t colorGradingLutUploadBytes() {
    return kColorGradingLutWidth * kColorGradingLutHeight * 4u * static_cast<uint32_t>(sizeof(uint16_t));
}

Vec3 linearToLogC(const Vec3& linear) {
    auto channel = [](float value) {
        return kLogCC * (std::log2(std::max(kLogCA * value + kLogCB, 0.0f)) * kLog10Of2) + kLogCD;
    };
    return Vec3(channel(linear.x), channel(linear.y), channel(linear.z));
}

Vec3 logCToLinear(const Vec3& logC) {
    auto channel = [](float value) {
        return (std::exp2((value - kLogCD) / kLogCC / kLog10Of2) - kLogCB) / kLogCA;
    };
    return Vec3(channel(logC.x), channel(logC.y), channel(logC.z));
}

Vec3 tonemapColor(const Vec3& linear, TonemappingMode mode) {
    switch (mode) {
    case TonemappingMode::Neutral:
        return maxZero(neutralTonemap(linear));
    case TonemappingMode::ACES:
        return maxZero(acesTonemap(maxZero(linear)));
    case TonemappingMode::None:
    default:
        return maxZero(linear);
    }
}

Vec3 colorGrade(const Vec3& linear, const ColorGradingSettings& settings) {
    if (!settings.enabled) {
        return linear;
    }

    Vec3 color = linear;
    color = lmsToLinear(multiply(linearToLms(color), colorBalanceCoefficients(settings.temperature, settings.tint)));

    const float contrast = settings.contrast / 100.0f + 1.0f;
    if (std::abs(contrast - 1.0f) > 1.0e-6f) {
        Vec3 logColor = linearToLogC(color);
        logColor = add((logColor - Vec3(kAcesccMidGray, kAcesccMidGray, kAcesccMidGray)) * contrast,
                       Vec3(kAcesccMidGray, kAcesccMidGray, kAcesccMidGray));
        color = logCToLinear(logColor);
    }

    color = maxZero(multiply(color, settings.colorFilter));

    const float lumaForSplit = saturate(luminance(saturate(positivePow(color, 1.0f / 2.2f))) + settings.splitBalance);
    const Vec3 splitShadows = lerp(Vec3(0.5f, 0.5f, 0.5f), settings.splitShadows, 1.0f - lumaForSplit);
    const Vec3 splitHighlights = lerp(Vec3(0.5f, 0.5f, 0.5f), settings.splitHighlights, lumaForSplit);
    Vec3 gamma = positivePow(color, 1.0f / 2.2f);
    gamma = softLight(gamma, splitShadows);
    gamma = softLight(gamma, splitHighlights);
    color = positivePow(gamma, 2.2f);

    color = Vec3(dot(color, settings.channelMixerRed), dot(color, settings.channelMixerGreen),
                 dot(color, settings.channelMixerBlue));

    const float luma = luminance(color);
    const float shadowsFactor = 1.0f - smoothstep(settings.shadowsMidtonesHighlightsLimits.x,
                                                  settings.shadowsMidtonesHighlightsLimits.y, luma);
    const float highlightsFactor = smoothstep(settings.shadowsMidtonesHighlightsLimits.z,
                                              settings.shadowsMidtonesHighlightsLimits.w, luma);
    const float midtonesFactor = 1.0f - shadowsFactor - highlightsFactor;
    color = add(add(multiply(color, settings.shadows) * shadowsFactor, multiply(color, settings.midtones) * midtonesFactor),
                multiply(color, settings.highlights) * highlightsFactor);

    color = add(multiply(color, settings.gain), settings.lift);
    color = positivePow(color, settings.gamma);

    Vec3 hsv = rgbToHsv(maxZero(color));
    hsv.x = rotateHue(hsv.x + settings.hueShift / 360.0f, 0.0f, 1.0f);
    color = hsvToRgb(hsv);

    const float saturationLuma = luminance(color);
    const float saturation = settings.saturation / 100.0f + 1.0f;
    color = Vec3(saturationLuma, saturationLuma, saturationLuma) + (color - Vec3(saturationLuma, saturationLuma, saturationLuma)) * saturation;
    return color;
}

Vec3 evaluateColorGradingLut(const Vec3& logColor, const ColorGradingSettings& settings, TonemappingMode mode) {
    const Vec3 graded = colorGrade(logCToLinear(logColor), settings);
    return maxZero(tonemapColor(graded, mode));
}

bool buildColorGradingLut(const ColorGradingSettings& settings, TonemappingMode mode, ColorGradingLut& lut) {
    lut.valid = false;
    lut.size = kColorGradingLutSize;
    lut.texels.assign(static_cast<std::size_t>(kColorGradingLutWidth) * kColorGradingLutHeight * 4u, 0);
    const float denominator = static_cast<float>(kColorGradingLutSize - 1);

    for (uint32_t z = 0; z < kColorGradingLutSize; ++z) {
        for (uint32_t y = 0; y < kColorGradingLutSize; ++y) {
            for (uint32_t x = 0; x < kColorGradingLutSize; ++x) {
                const Vec3 logColor(static_cast<float>(x) / denominator, static_cast<float>(y) / denominator,
                                    static_cast<float>(z) / denominator);
                const Vec3 rgb = evaluateColorGradingLut(logColor, settings, mode);
                const std::size_t index = colorGradingLutStripIndex(x, y, z);
                lut.texels[index + 0] = bx::halfFromFloat(rgb.x);
                lut.texels[index + 1] = bx::halfFromFloat(rgb.y);
                lut.texels[index + 2] = bx::halfFromFloat(rgb.z);
                lut.texels[index + 3] = bx::halfFromFloat(1.0f);
            }
        }
    }

    lut.valid = true;
    return true;
}

Vec3 ColorGradingLut::sample(Vec3 logColor) const {
    if (!valid || texels.empty() || size < 2) {
        return {};
    }

    const float denominator = static_cast<float>(size - 1);
    logColor = saturate(logColor);
    const float xf = logColor.x * denominator;
    const float yf = logColor.y * denominator;
    const float zf = logColor.z * denominator;
    const uint32_t x0 = static_cast<uint32_t>(std::floor(xf));
    const uint32_t y0 = static_cast<uint32_t>(std::floor(yf));
    const uint32_t z0 = static_cast<uint32_t>(std::floor(zf));
    const uint32_t x1 = std::min(x0 + 1, size - 1);
    const uint32_t y1 = std::min(y0 + 1, size - 1);
    const uint32_t z1 = std::min(z0 + 1, size - 1);
    const float fx = xf - static_cast<float>(x0);
    const float fy = yf - static_cast<float>(y0);
    const float fz = zf - static_cast<float>(z0);

    auto load = [this](uint32_t x, uint32_t y, uint32_t z) {
        const std::size_t index = colorGradingLutStripIndex(x, y, z);
        return Vec3(bx::halfToFloat(texels[index + 0]), bx::halfToFloat(texels[index + 1]),
                    bx::halfToFloat(texels[index + 2]));
    };
    auto bilinear = [&load, fx, fy](uint32_t x0, uint32_t x1, uint32_t y0, uint32_t y1, uint32_t z) {
        const Vec3 c00 = load(x0, y0, z);
        const Vec3 c10 = load(x1, y0, z);
        const Vec3 c01 = load(x0, y1, z);
        const Vec3 c11 = load(x1, y1, z);
        return lerp(lerp(c00, c10, fx), lerp(c01, c11, fx), fy);
    };
    return lerp(bilinear(x0, x1, y0, y1, z0), bilinear(x0, x1, y0, y1, z1), fz);
}

} // namespace hbg::render
