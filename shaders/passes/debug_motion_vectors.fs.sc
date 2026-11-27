$input v_texcoord0

#include "common/common.sc"

SAMPLER2D(s_motionVectors, 0);

float hbgDebugAtan2(float y, float x)
{
    if (abs(x) < 1.0e-6)
    {
        return y >= 0.0 ? HBG_PI * 0.5 : -HBG_PI * 0.5;
    }
    float angle = atan(y / x);
    if (x < 0.0)
    {
        angle += y >= 0.0 ? HBG_PI : -HBG_PI;
    }
    return angle;
}

void main()
{
    const vec2 motion = texture2D(s_motionVectors, v_texcoord0).xy * 2.0;
    const float speed = length(motion);
    if (speed < 1.0e-5)
    {
        gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    const float phi = hbgDebugAtan2(motion.x, motion.y);
    const float hue = (phi / HBG_PI + 1.0) * 0.5;
    vec3 wheel = vec3(abs(hue * 6.0 - 3.0) - 1.0, 2.0 - abs(hue * 6.0 - 2.0),
                      2.0 - abs(hue * 6.0 - 4.0));
    wheel = hbgSaturate(wheel);

    const float maxSpeed = 60.0 / 0.15;
    const float absoluteLength = hbgSaturate(speed * maxSpeed);
    const vec3 color = wheel * mix(0.03, 0.50, absoluteLength);
    gl_FragColor = vec4(color, 1.0);
}
