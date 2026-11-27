$input a_position
$output v_screen

#include "common/common.sc"

uniform vec4 u_riverParticleParams;

void main()
{
    gl_Position = vec4(a_position.xy, 0.0, 1.0);
    v_screen = vec2((a_position.x * 0.5 + 0.5) * u_riverParticleParams.x,
                    (0.5 - a_position.y * 0.5) * u_riverParticleParams.y);
}
