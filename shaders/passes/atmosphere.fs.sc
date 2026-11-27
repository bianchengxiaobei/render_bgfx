$input v_texcoord0

#include "common/common.sc"
#include "lighting/atmosphere.sc"

SAMPLER2D(s_hdrColor, 0);
SAMPLER2D(s_depthSource, 1);

void main()
{
    const vec2 uv = v_texcoord0;
    const vec2 sourceUv = vec2(uv.x, 1.0 - uv.y);
    const vec3 source = texture2D(s_hdrColor, sourceUv).rgb;
    const float rawDepth = texture2D(s_depthSource, uv).r;
    if (rawDepth >= 0.999999 || u_atmosphere_params3.x < 0.5)
    {
        gl_FragColor = vec4(source, 1.0);
        return;
    }

    const vec2 ndc = uv * vec2(2.0, 2.0) - vec2(1.0, 1.0);
    vec4 world = mul(u_global_invViewProj, vec4(ndc, rawDepth, 1.0));
    world.xyz /= max(world.w, 1.0e-6);

    const vec3 transmittance = hbgAtmosphereAerialTransmittance(u_global_cameraPos.xyz, world.xyz);
    const vec3 inScattering = hbgAtmosphereAerialPerspective(u_global_cameraPos.xyz, world.xyz);
    gl_FragColor = vec4(max(source * transmittance + inScattering, vec3(0.0, 0.0, 0.0)), 1.0);
}
