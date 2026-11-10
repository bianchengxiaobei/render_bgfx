#ifndef HBG_NORMAL_BUFFER_SC
#define HBG_NORMAL_BUFFER_SC

#include "common/common.sc"

vec3 hbgOctEncodeNormal(vec3 normalWS)
{
    vec3 n = normalWS;
    n *= 1.0 / max(dot(abs(n), vec3(1.0, 1.0, 1.0)), 1.0e-6);
    float t = hbgSaturate(-n.z);
    return vec3(n.xy + vec2(n.x >= 0.0 ? t : -t, n.y >= 0.0 ? t : -t), n.z);
}

vec4 hbgEncodeNormalBuffer(vec3 normalWS, float perceptualRoughness)
{
    const float seamThreshold = 1.0 / 1024.0;
    vec3 normal = normalWS;
    normal.z = (normal.z < 0.0 ? -1.0 : 1.0) * max(seamThreshold, abs(normal.z));

    const vec3 octNormal = hbgOctEncodeNormal(normal);
    const vec2 encoded = hbgSaturate(octNormal.xy * 0.5 + 0.5);
    const vec2 packed = floor(encoded * 4095.5);
    const float hiX = floor(packed.x / 256.0);
    const float hiY = floor(packed.y / 256.0);
    const float loX = packed.x - hiX * 256.0;
    const float loY = packed.y - hiY * 256.0;
    return vec4(vec3(loX, loY, hiX + hiY * 16.0) / 255.0, perceptualRoughness);
}

vec3 hbgDecodeNormalBuffer(vec4 normalBuffer)
{
    const vec2 packed = floor(normalBuffer.rg * 255.5);
    const float high = floor(normalBuffer.b * 255.5);
    const float loX = floor(normalBuffer.r * 255.5);
    const float loY = floor(normalBuffer.g * 255.5);
    const float hiX = floor(high / 16.0);
    const float hiY = high - hiX * 16.0;
    const vec2 oct = vec2(loX + hiX * 256.0, loY + hiY * 256.0) / 4095.0;
    const vec2 f = oct * 2.0 - 1.0;

    vec3 n = vec3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
    const float t = max(-n.z, 0.0);
    n.xy += vec2(n.x >= 0.0 ? -t : t, n.y >= 0.0 ? -t : t);
    return normalize(n);
}

#endif // HBG_NORMAL_BUFFER_SC
