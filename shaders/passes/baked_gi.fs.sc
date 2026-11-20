$input v_texcoord0, v_texcoord1, v_worldPos, v_normal, v_tangent, v_bitangent

#include "common/common.sc"
#include "common/packing.sc"
#include "lighting/baked_gi.sc"

SAMPLER2D(s_baseColorMap, 1);
SAMPLER2D(s_normalMap, 2);

void main()
{
    const float lightmapMask = u_object_lightmapParams.x;
    if (lightmapMask < 0.5)
    {
        discard;
    }

    const vec4 baseColor = texture2D(s_baseColorMap, v_texcoord0) * u_material_baseColorFactor;
#ifdef ALPHA_CLIP
    if (baseColor.a < u_material_metallicRoughness.w)
    {
        discard;
    }
#endif

    vec3 normalWS = normalize(v_normal);
#ifdef DOUBLE_SIDED
    const float viewSign = gl_FrontFacing ? -1.0 : 1.0;
#endif
#ifdef HAS_NORMAL_MAP
    const vec3 normalTS = hbgUnpackNormal(texture2D(s_normalMap, v_texcoord0).xy,
                                          u_material_metallicRoughness.z);
    vec3 tangent = v_tangent;
    vec3 bitangent = v_bitangent;
    if (dot(tangent, tangent) < 1.0e-8 || dot(bitangent, bitangent) < 1.0e-8)
    {
        const vec3 dp1 = dFdx(v_worldPos);
        const vec3 dp2 = dFdy(v_worldPos);
        const vec2 duv1 = dFdx(v_texcoord0);
        const vec2 duv2 = dFdy(v_texcoord0);
        const float determinant = duv1.x * duv2.y - duv1.y * duv2.x;
        if (abs(determinant) > 1.0e-8)
        {
            tangent = normalize((dp1 * duv2.y - dp2 * duv1.y) / determinant);
            bitangent = cross(normalWS, tangent);
        }
    }
    normalWS = normalize(mul(normalTS, mtxFromCols(tangent, bitangent, normalWS)));
#endif

#ifdef DOUBLE_SIDED
    if (viewSign < 0.0)
    {
        normalWS = -normalWS;
    }
#endif

    const vec3 baked = hbgSampleBakedGI(v_texcoord1, normalWS) * baseColor.rgb * lightmapMask;
    gl_FragColor = vec4(baked, lightmapMask);
}
