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

// Proland's river wave tiles store raw signed slopes (nx, ny) and the
// terrain shader builds the normal as normalize(vec3(slopes * waveSlopeFactor, 1)).
// Unlike a tangent-space normal map, this keeps z=1 and lets steep waves
// stay representable without the unit-sphere clamp.
vec3 hbgUnpackSlopeNormal(vec2 slopes, float scale)
{
    return normalize(vec3(slopes * scale, 1.0));
}

vec4 hbgPackNormal(vec3 normal)
{
    return vec4(normal * 0.5 + 0.5, 1.0);
}

#endif // HBG_PACKING_SC
