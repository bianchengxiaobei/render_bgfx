$input v_texcoord0

#include "lighting/environment.sc"

void main()
{
    const vec2 ndc = v_texcoord0 * vec2(2.0, 2.0) - vec2(1.0, 1.0);
    const vec4 world = mul(u_global_nonJitteredInvViewProj, vec4(ndc, 1.0, 1.0));
    const vec3 direction = normalize(world.xyz / world.w - u_global_cameraPos.xyz);
    const vec3 color = hbgSampleEnvironment(direction, 0.0) * u_environment_skyParams.x;
    gl_FragColor = vec4(color, 1.0);
}
