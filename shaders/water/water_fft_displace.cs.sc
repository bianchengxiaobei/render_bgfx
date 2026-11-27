#include "bgfx_compute.sh"
#include "water/water_fft_common.sc"

SAMPLER2DARRAY(s_waterFftFinal, 0);
BUFFER_RAW_RW(s_waterVertices, 1);

uniform mat4 u_wfInvViewProj;
uniform vec4 u_wfCamera;
uniform vec4 u_wfGrid;
uniform vec4 u_wfDisplace;
uniform vec4 u_wfUv;
uniform vec4 u_wfSphere;
uniform vec4 u_wfGridSizes;

vec3 hbgWaterBasePosition(int column, int row)
{
    const int width = int(u_wfGrid.x + 0.5);
    const int gridHeight = int(u_wfGrid.y + 0.5);
    const float overscan = u_wfGrid.w;
    const float ndcX = 2.0 * overscan * float(column) / float(max(width - 1, 1)) - overscan;
    const float ndcY = 2.0 * overscan * float(row) / float(max(gridHeight - 1, 1)) - overscan;
    const vec4 world = mul(u_wfInvViewProj, vec4(ndcX, ndcY, 1.0, 1.0));
    vec3 ray = vec3(0.0, 0.0, -1.0);
    if (abs(world.w) > 1.0e-8)
    {
        ray = normalize(world.xyz / world.w - u_wfCamera.xyz);
    }

    const float baseHeight = u_wfGrid.z;
    const float sphereRadius = u_wfSphere.x;
    if (sphereRadius > 0.0)
    {
        const float altitude = u_wfCamera.y - baseHeight;
        if (altitude > 0.0)
        {
            const float altitudeRatio = altitude / sphereRadius;
            const float onePlus = 1.0 + altitudeRatio;
            const float horizonDown =
                sqrt(max(altitudeRatio * (2.0 + altitudeRatio), 0.0)) / onePlus;
            if (ray.y > -horizonDown)
            {
                vec3 horizontal = vec3(ray.x, 0.0, ray.z);
                const float horizontalLength = length(horizontal);
                horizontal = horizontalLength > 1.0e-6 ? horizontal / horizontalLength : vec3(0.0, 0.0, -1.0);
                ray = vec3(horizontal.x / onePlus, -horizonDown, horizontal.z / onePlus);
            }
            const float b = ray.y * onePlus;
            const float c = altitudeRatio * (2.0 + altitudeRatio);
            const float denominator = -b + sqrt(max(b * b - c, 0.0));
            const float distance = sphereRadius * c / max(denominator, 1.0e-12);
            return u_wfCamera.xyz + ray * distance;
        }
    }

    const float maxDistance = u_wfDisplace.z;
    if (ray.y < -1.0e-5)
    {
        const float distance = (baseHeight - u_wfCamera.y) / ray.y;
        if (distance > 0.0 && distance < maxDistance)
        {
            vec3 base = u_wfCamera.xyz + ray * distance;
            base.y = baseHeight;
            return base;
        }
    }

    vec3 horizontal = vec3(ray.x, 0.0, ray.z);
    const float horizontalLength = length(horizontal);
    horizontal = horizontalLength > 1.0e-6 ? horizontal / horizontalLength : vec3(0.0, 0.0, -1.0);
    vec3 base = u_wfCamera.xyz + horizontal * maxDistance;
    base.y = baseHeight;
    return base;
}

float hbgWaterLod(float footprint, float gridSize)
{
    const float texelSize = gridSize / u_wfDisplace.w;
    return footprint > texelSize && texelSize > 0.0 ? log2(footprint / texelSize) : 0.0;
}

NUM_THREADS(64, 1, 1)

void main()
{
    const uint index = gl_GlobalInvocationID.x;
    const int width = int(u_wfGrid.x + 0.5);
    const int gridHeight = int(u_wfGrid.y + 0.5);
    if (width < 2 || gridHeight < 2 || index >= uint(width * gridHeight))
    {
        return;
    }

    const int column = int(index % uint(width));
    const int row = int(index / uint(width));
    const vec3 base = hbgWaterBasePosition(column, row);
    const vec3 baseLeft = hbgWaterBasePosition(max(column - 1, 0), row);
    const vec3 baseRight = hbgWaterBasePosition(min(column + 1, width - 1), row);
    const vec3 baseDown = hbgWaterBasePosition(column, max(row - 1, 0));
    const vec3 baseUp = hbgWaterBasePosition(column, min(row + 1, gridHeight - 1));
    const float footprint = max(length(baseRight - baseLeft), length(baseUp - baseDown));

    const vec4 gridSizes = max(u_wfGridSizes, vec4(1.0e-4, 1.0e-4, 1.0e-4, 1.0e-4));
    const vec2 baseXz = base.xz;
    const vec2 uv0 = baseXz / gridSizes.x;
    const vec2 uv1 = baseXz / gridSizes.y;
    const vec2 uv2 = baseXz / gridSizes.z;
    const vec2 uv3 = baseXz / gridSizes.w;
    const float lod0 = hbgWaterLod(footprint, gridSizes.x);
    const float lod1 = hbgWaterLod(footprint, gridSizes.y);
    const float lod2 = hbgWaterLod(footprint, gridSizes.z);
    const float lod3 = hbgWaterLod(footprint, gridSizes.w);
    const vec4 height0 = texture2DArrayLod(s_waterFftFinal, vec3(uv0, 0.0), lod0);
    const vec4 height1 = texture2DArrayLod(s_waterFftFinal, vec3(uv1, 0.0), lod1);
    const vec4 height2 = texture2DArrayLod(s_waterFftFinal, vec3(uv2, 0.0), lod2);
    const vec4 height3 = texture2DArrayLod(s_waterFftFinal, vec3(uv3, 0.0), lod3);
    const vec4 slope0 = texture2DArrayLod(s_waterFftFinal, vec3(uv0, 1.0), lod0);
    const vec4 slope1 = texture2DArrayLod(s_waterFftFinal, vec3(uv1, 1.0), lod1);
    const vec4 slope2 = texture2DArrayLod(s_waterFftFinal, vec3(uv2, 2.0), lod2);
    const vec4 slope3 = texture2DArrayLod(s_waterFftFinal, vec3(uv3, 2.0), lod3);
    const vec4 choppy0 = texture2DArrayLod(s_waterFftFinal, vec3(uv0, 3.0), lod0);
    const vec4 choppy1 = texture2DArrayLod(s_waterFftFinal, vec3(uv1, 3.0), lod1);
    const vec4 choppy2 = texture2DArrayLod(s_waterFftFinal, vec3(uv2, 4.0), lod2);
    const vec4 choppy3 = texture2DArrayLod(s_waterFftFinal, vec3(uv3, 4.0), lod3);

    const float waveHeight = height0.x + height1.y + height2.z + height3.w;
    const float slopeX = slope0.x + slope1.z + slope2.x + slope3.z;
    const float slopeZ = slope0.y + slope1.w + slope2.y + slope3.w;
    const float choppyX = choppy0.x + choppy1.z + choppy2.x + choppy3.z;
    const float choppyZ = choppy0.y + choppy1.w + choppy2.y + choppy3.w;
    const float sphereRadius = u_wfSphere.x;
    vec2 sphereSlope = vec2(0.0, 0.0);
    if (sphereRadius > 0.0)
    {
        const float centerY = u_wfGrid.z - sphereRadius;
        const float vertical = base.y - centerY;
        if (vertical > 1.0e-3)
        {
            sphereSlope = vec2(base.x - u_wfCamera.x, base.z - u_wfCamera.z) / vertical;
        }
    }

    vec3 position = base;
    position.x += choppyX * u_wfDisplace.y;
    position.y = (sphereRadius > 0.0 ? base.y : u_wfGrid.z) + waveHeight * u_wfDisplace.x;
    position.z += choppyZ * u_wfDisplace.y;
    const vec3 normalRaw = vec3(-slopeX + sphereSlope.x, 1.0, -slopeZ + sphereSlope.y);
    const float normalLength = length(normalRaw);
    const vec3 normal = normalLength > 1.0e-6 ? normalRaw / normalLength : vec3(0.0, 1.0, 0.0);
    const vec2 uv = base.xz * u_wfUv.xy + u_wfUv.zw;

    const uint offset = index * 18u;
    rawStoreFloat(s_waterVertices, offset + 0u, position.x);
    rawStoreFloat(s_waterVertices, offset + 1u, position.y);
    rawStoreFloat(s_waterVertices, offset + 2u, position.z);
    rawStoreFloat(s_waterVertices, offset + 3u, normal.x);
    rawStoreFloat(s_waterVertices, offset + 4u, normal.y);
    rawStoreFloat(s_waterVertices, offset + 5u, normal.z);
    rawStoreFloat(s_waterVertices, offset + 6u, 1.0);
    rawStoreFloat(s_waterVertices, offset + 7u, 0.0);
    rawStoreFloat(s_waterVertices, offset + 8u, 0.0);
    rawStoreFloat(s_waterVertices, offset + 9u, -1.0);
    rawStoreFloat(s_waterVertices, offset + 10u, uv.x);
    rawStoreFloat(s_waterVertices, offset + 11u, uv.y);
    rawStoreFloat(s_waterVertices, offset + 12u, 0.0);
    rawStoreFloat(s_waterVertices, offset + 13u, 0.0);
    rawStoreFloat(s_waterVertices, offset + 14u, 1.0);
    rawStoreFloat(s_waterVertices, offset + 15u, 1.0);
    rawStoreFloat(s_waterVertices, offset + 16u, 1.0);
    rawStoreFloat(s_waterVertices, offset + 17u, 1.0);
}
