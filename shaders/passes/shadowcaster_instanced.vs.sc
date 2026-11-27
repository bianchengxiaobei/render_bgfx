$input a_position, a_texcoord0, i_data0, i_data1, i_data2, i_data3
$output v_texcoord0

#include "common/common.sc"
#include "material/vegetation_impostor.sc"

void main()
{
    mat4 model = mtxFromCols(i_data0, i_data1, i_data2, i_data3);
    vec4 worldPosition;
    vec2 texcoord = a_texcoord0;

    if (u_material_advanced1.y > 500.0) {
        const vec3 base = mul(model, vec4(0.0, 0.0, 0.0, 1.0)).xyz;
        const float scale = max(length(mul(model, vec4(1.0, 0.0, 0.0, 0.0)).xyz), 1.0e-4);
        vec3 toLight = -u_pass_lightDirection.xyz;
        toLight.y = 0.0;
        toLight = dot(toLight, toLight) > 1.0e-6 ? normalize(toLight) : vec3(0.0, 0.0, 1.0);
        const vec3 right = normalize(cross(vec3(0.0, 1.0, 0.0), toLight));
        const vec3 up = vec3(0.0, 1.0, 0.0);
        float side = a_position.x < 0.0 ? -1.0 : 1.0;
        if (abs(a_position.x) < 1.0e-4) {
            side = a_position.z < 0.0 ? 1.0 : -1.0;
        }
        const float height = clamp(a_position.y, 0.0, 1.0);
        worldPosition = vec4(base + right * (side * 0.32 * scale) + up * (height * scale), 1.0);
        texcoord = vec2((side + 1.0) * 0.5, 1.0 - height);

        const vec3 viewDirection = normalize(-u_pass_lightDirection.xyz);
        ivec3 viewIds;
        vec3 viewWeights;
        hbgTreeFindViews(int(u_material_advanced1.y - 1000.0 + 0.5),
                         hbgTreeLocalViewDirection(viewDirection), viewIds, viewWeights);
        const int layer = hbgTreeDominantView(viewIds, viewWeights);
        const float species = floor(u_material_advanced1.y - 1000.0 + 0.5);
        const float view = clamp(float(layer) - species * 181.0, 0.0, 180.0);
        const float compactView = min(floor(view * 16.0 / 181.0), 15.0);
        const float treeLayer = species * 16.0 + compactView;
        const vec2 treeLocal = clamp(texcoord, vec2(HBG_TREE_ATLAS_TEXEL, HBG_TREE_ATLAS_TEXEL),
                                     vec2(1.0 - HBG_TREE_ATLAS_TEXEL, 1.0 - HBG_TREE_ATLAS_TEXEL));
        const float treeCol = mod(treeLayer, 8.0);
        const float treeRow = floor(treeLayer / 8.0);
        texcoord = (vec2(treeCol, treeRow) + treeLocal) / vec2(8.0, 4.0);
    } else {
        worldPosition = mul(model, vec4(a_position, 1.0));
    }

    v_texcoord0 = texcoord;
    gl_Position = mul(u_pass_shadowViewProj, worldPosition);
}
