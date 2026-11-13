#ifndef HBG_CONTACT_SHADOW_SC
#define HBG_CONTACT_SHADOW_SC

#include "common/common.sc"

float hbgContactShadowVisibility(float pixelMask, float pixelFade, float lightMask)
{
    float result = 1.0;
    if (u_pass_lightColor.w >= 0.5 && u_contact_params3.y > 0.0)
    {
        const int mask = int(lightMask + 0.5);
        if (mask != 0 && (int(pixelMask + 0.5) & mask) != 0)
        {
            result = 1.0 - hbgSaturate(pixelFade) * hbgSaturate(u_contact_params3.y);
        }
    }
    return result;
}
#ifdef HBG_CONTACT_SHADOW_TEXTURE
SAMPLER2D(s_contactShadow, 11);

vec2 hbgDecodeContactShadow(vec4 contactShadowData)
{
    return vec2(floor(hbgSaturate(contactShadowData.b) * 255.0 + 0.5),
                hbgSaturate(contactShadowData.a));
}

vec2 hbgContactShadowFourTap(vec2 screenUV)
{
    const vec2 contactSize = max(ceil(u_global_screenSize.xy * 0.5), vec2(1.0, 1.0));
    const vec2 texel = vec2(1.0, 1.0) / contactSize;
    const vec2 base = (floor(screenUV / texel - vec2(0.5, 0.5)) + vec2(0.5, 0.5)) * texel;
    const vec4 c0 = texture2D(s_contactShadow, base);
    const vec4 c1 = texture2D(s_contactShadow, base + vec2(texel.x, 0.0));
    const vec4 c2 = texture2D(s_contactShadow, base + vec2(0.0, texel.y));
    const vec4 c3 = texture2D(s_contactShadow, base + texel);

    const uint mask = (uint(floor(c0.b * 255.0 + 0.5)) | uint(floor(c1.b * 255.0 + 0.5)) |
                       uint(floor(c2.b * 255.0 + 0.5)) | uint(floor(c3.b * 255.0 + 0.5)));
    const float fade = (c0.a + c1.a + c2.a + c3.a) * 0.25;
    return vec2(float(mask), fade);
}

vec2 hbgSampleContactShadow(vec2 screenUV)
{
    vec2 result = vec2(0.0, 0.0);
    if (u_contact_params3.w > 0.5)
    {
        result = hbgContactShadowFourTap(screenUV);
    }
    else
    {
        result = hbgDecodeContactShadow(texture2D(s_contactShadow, screenUV));
    }
    return result;
}
#endif // HBG_CONTACT_SHADOW_TEXTURE

#endif // HBG_CONTACT_SHADOW_SC
