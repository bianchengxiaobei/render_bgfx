$input a_position, i_data0, i_data1, i_data2, i_data3
$output v_currentClip, v_prevClip

#include "common/common.sc"

void main()
{
    const mat4 model = mtxFromCols(i_data0, i_data1, i_data2, i_data3);
    vec4 worldPosition;

    if (u_material_advanced1.y > 500.0) {
        const vec3 base = mul(model, vec4(0.0, 0.0, 0.0, 1.0)).xyz;
        const float scale = max(length(mul(model, vec4(1.0, 0.0, 0.0, 0.0)).xyz), 1.0e-4);
        vec3 toCamera = u_global_cameraPos.xyz - base;
        toCamera.y = 0.0;
        toCamera = dot(toCamera, toCamera) > 1.0e-6 ? normalize(toCamera) : vec3(0.0, 0.0, 1.0);
        const vec3 right = normalize(cross(vec3(0.0, 1.0, 0.0), toCamera));
        float side = a_position.x < 0.0 ? -1.0 : 1.0;
        if (abs(a_position.x) < 1.0e-4) {
            side = a_position.z < 0.0 ? 1.0 : -1.0;
        }
        const float height = clamp(a_position.y, 0.0, 1.0);
        worldPosition = vec4(base + right * (side * 0.32 * scale) +
                                 vec3(0.0, 1.0, 0.0) * (height * scale), 1.0);
        v_currentClip = mul(u_global_nonJitteredViewProj, worldPosition);
        v_prevClip = v_currentClip;
    } else {
        worldPosition = mul(model, vec4(a_position, 1.0));
        v_currentClip = mul(u_global_nonJitteredViewProj, worldPosition);
        v_prevClip = mul(u_global_prevViewProj, worldPosition);
    }
    gl_Position = mul(u_global_viewProj, worldPosition);
}
