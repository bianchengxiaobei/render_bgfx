vec3 a_position : POSITION;
vec2 a_texcoord0 : TEXCOORD0;
vec3 a_texcoord2 : TEXCOORD2;

vec4 v_currentClip : TEXCOORD0 = vec4(0.0, 0.0, 0.0, 1.0);
vec4 v_prevClip : TEXCOORD1 = vec4(0.0, 0.0, 0.0, 1.0);
vec2 v_texcoord0 : TEXCOORD2 = vec2(0.0, 0.0);
