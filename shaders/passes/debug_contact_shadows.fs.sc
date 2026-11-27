$input v_texcoord0

#include "common/common.sc"

SAMPLER2D(s_contactShadow, 0);

void main()
{
    const vec2 uv = u_contact_params4.y > 0.5 ? vec2(v_texcoord0.x, 1.0 - v_texcoord0.y) : v_texcoord0;
    const vec4 contactShadow = texture2D(s_contactShadow, uv);
    gl_FragColor = vec4(contactShadow.b, contactShadow.a, 0.0, 1.0);
}
