$input v_texcoord0

#include "lighting/environment.sc"
#include "lighting/atmosphere.sc"

SAMPLER2D(s_atmosphereSunGlare, 0);

vec3 hbgAtmosphereSunGlare(vec3 camera, vec3 direction)
{
    if (u_atmosphere_params4.y < 0.5)
    {
        return vec3(0.0, 0.0, 0.0);
    }

    const vec3 sun = normalize(u_pass_lightDirection.xyz);
    if (dot(direction, sun) <= 0.0)
    {
        return vec3(0.0, 0.0, 0.0);
    }

    const vec3 reference = abs(sun.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(0.0, 0.0, 1.0);
    const vec3 right = normalize(cross(reference, sun));
    const vec3 up = cross(sun, right);
    const vec2 relative = vec2(dot(direction, right), dot(direction, up));
    const vec2 uv = vec2(0.5, 0.5) + relative * 4.0;
    if (abs(uv.x - 0.5) > 0.5 || abs(uv.y - 0.5) > 0.5)
    {
        return vec3(0.0, 0.0, 0.0);
    }

    const float edgeDistance = length(uv - vec2(0.5, 0.5));
    const float glare = texture2D(s_atmosphereSunGlare, uv).r *
                        (1.0 - smoothstep(0.40, 0.49, edgeDistance));
    if (glare <= 0.0)
    {
        return vec3(0.0, 0.0, 0.0);
    }

    const float radius = max(length(camera), u_atmosphere_params.x);
    const float muSun = clamp(dot(camera, sun) / radius, -1.0, 1.0);
    const float horizon =
        -sqrt(max(1.0 - (u_atmosphere_params.x * u_atmosphere_params.x) / (radius * radius), 0.0));
    if (muSun < horizon)
    {
        return vec3(0.0, 0.0, 0.0);
    }

    const vec3 transmittance = u_atmosphere_params4.x > 0.5
                                   ? hbgAtmosphereTransmittanceLut(radius, muSun)
                                   : hbgAtmosphereSunTransmittance(radius, muSun);
    return pow(max(glare, 0.0), 2.2) * transmittance * u_atmosphere_params2.x;
}

void main()
{
    const vec2 ndc = v_texcoord0 * vec2(2.0, 2.0) - vec2(1.0, 1.0);
    const vec4 world = mul(u_global_nonJitteredInvViewProj, vec4(ndc, 1.0, 1.0));
    const vec3 direction = normalize(world.xyz / world.w - u_global_cameraPos.xyz);
    vec3 color = hbgSampleEnvironment(direction, 0.0) * u_environment_skyParams.x;
    if (u_atmosphere_params2.w > 0.5)
    {
        const vec3 camera = hbgAtmosphereCameraPosition();
        color = hbgAtmosphereSkyRadiance(camera, direction) + hbgAtmosphereSunGlare(camera, direction);
    }
    gl_FragColor = vec4(color, 1.0);
}
