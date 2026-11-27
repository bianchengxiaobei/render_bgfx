#ifndef HBG_VEGETATION_IMPOSTOR_SC
#define HBG_VEGETATION_IMPOSTOR_SC

#include "common/common.sc"

const int HBG_TREE_VIEW_N = 9;
const int HBG_TREE_VIEW_COUNT = 181;
const float HBG_TREE_ATLAS_TEXEL = 1.0 / 128.0;
const float HBG_TREE_ATLAS_COLUMNS = 8.0;
const float HBG_TREE_ATLAS_ROWS = 4.0;
const float HBG_TREE_MATERIAL_TAG = 1000.0;

int hbgTreeViewNumber(int i, int j) {
    return i * ((2 * HBG_TREE_VIEW_N + 1) - abs(i)) + j +
           (HBG_TREE_VIEW_N * (HBG_TREE_VIEW_N + 1));
}

void hbgTreeFindViews(int species, vec3 cdir, out ivec3 ids, out vec3 weights) {
    vec3 VDIR = vec3(cdir.xy, max(cdir.z, 0.01));
    float a = abs(VDIR.x) > abs(VDIR.y) ? VDIR.y / VDIR.x : -VDIR.x / VDIR.y;
    float nxx = 9.0 * (1.0 - a) * acos(VDIR.z) / HBG_PI;
    float nyy = 9.0 * (1.0 + a) * acos(VDIR.z) / HBG_PI;
    int i = int(floor(nxx));
    int j = int(floor(nyy));
    float ti = nxx - float(i);
    float tj = nyy - float(j);
    float alpha = 1.0 - ti - tj;
    bool b = alpha > 0.0;
    ivec3 ii = ivec3(b ? i : i + 1, i + 1, i);
    ivec3 jj = ivec3(b ? j : j + 1, j, j + 1);
    weights = vec3(abs(alpha), b ? ti : 1.0 - tj, b ? tj : 1.0 - ti);
    if (abs(VDIR.y) >= abs(VDIR.x)) {
        ivec3 tmp = ii;
        ii = -jj;
        jj = tmp;
    }
    int side = VDIR.x + VDIR.y < 0.0 ? -1 : 1;
    ii *= side;
    jj *= side;
    int speciesOffset = species * 181;
    ids = ivec3(speciesOffset, speciesOffset, speciesOffset) +
          ivec3(hbgTreeViewNumber(ii.x, jj.x), hbgTreeViewNumber(ii.y, jj.y),
                hbgTreeViewNumber(ii.z, jj.z));
}

int hbgTreeDominantView(ivec3 ids, vec3 weights) {
    if (weights.x >= weights.y && weights.x >= weights.z) {
        return ids.x;
    }
    return weights.y >= weights.z ? ids.y : ids.z;
}

vec3 hbgTreeLocalViewDirection(vec3 worldDirection) {
    return normalize(vec3(worldDirection.x, worldDirection.z, worldDirection.y));
}

#endif // HBG_VEGETATION_IMPOSTOR_SC
