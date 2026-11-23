vec3 a_position : POSITION;
vec3 a_texcoord2 : TEXCOORD2;

vec4 v_currentClip : TEXCOORD0 = vec4(0.0, 0.0, 0.0, 1.0);
vec4 v_prevClip    : TEXCOORD1 = vec4(0.0, 0.0, 0.0, 1.0);
