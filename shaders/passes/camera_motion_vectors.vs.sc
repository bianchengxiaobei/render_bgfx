$input a_position
$output v_texcoord0

#include "common/common.sc"

void main()
{
    // Far-plane fullscreen triangle: it only passes where the prepass depth is 1.
    gl_Position = vec4(a_position.xy, 1.0, 1.0);
    v_texcoord0 = a_position.xy * vec2(0.5, 0.5) + vec2(0.5, 0.5);
}
