$input v_texcoord0

#include "common/common.sc"

SAMPLER2D(s_baseColorMap, 0);

void main()
{
#ifdef ALPHA_CLIP
    const float alpha = texture2D(s_baseColorMap, v_texcoord0).a * u_material_baseColorFactor.a;
    if (alpha < u_material_metallicRoughness.w)
    {
        discard;
    }
#endif
    gl_FragColor = vec4(1.0, 1.0, 1.0, 1.0);
}
