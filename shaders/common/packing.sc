#ifndef HBG_PACKING_SC
#define HBG_PACKING_SC

#include "common/common.sc"

vec3 hbgUnpackNormal(vec2 encoded, float scale)
{
    vec2 xy = encoded * 2.0 - 1.0;
    xy *= scale;
    float z = sqrt(max(1.0 - dot(xy, xy), 0.0));
    return vec3(xy, z);
}

vec4 hbgPackNormal(vec3 normal)
{
    return vec4(normal * 0.5 + 0.5, 1.0);
}

#endif // HBG_PACKING_SC
