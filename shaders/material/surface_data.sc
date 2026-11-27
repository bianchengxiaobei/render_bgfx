#ifndef HBG_SURFACE_DATA_SC
#define HBG_SURFACE_DATA_SC

#include "common/common.sc"
#include "common/packing.sc"
#include "common/brdf.sc"
#include "material/surface_data_types.sc"

SAMPLER2D(s_baseColorMap, 0);
SAMPLER2D(s_metallicRoughnessMap, 1);
SAMPLER2D(s_normalMap, 2);
SAMPLER2D(s_occlusionMap, 3);
#ifdef TERRAIN_TILES
SAMPLER2DARRAY(s_terrainTiles, 4);
SAMPLER2D(s_terrainTileMap, 5);
uniform vec4 u_terrainQuadInfo;
uniform vec4 u_terrainCamera;
uniform vec4 u_terrainMapInfo;
#else
SAMPLER2D(s_emissiveMap, 4);
#endif
#ifdef WATER_FFT
SAMPLER2DARRAY(s_waterFFT, 15);
#endif

#ifdef TERRAIN_TILES
// Port of Proland's textureQuadtree.glsl: compute the tile level from the
// world position and camera, then resolve (level, tx, ty) through the RG8
// tileMap.  A missing entry falls back to the direct per-object layer.
float hbgTerrainTileMapLayer(vec2 worldXZ, out vec2 atlasUV)
{
    atlasUV = vec2(0.0, 0.0);
    if (u_terrainMapInfo.x < 0.5)
    {
        return -1.0;
    }

    const float rootSize = max(u_terrainQuadInfo.x, 1.0e-4);
    const float splitDistance = max(u_terrainQuadInfo.y, 0.0);
    const float maxLevel = max(u_terrainCamera.w, 0.0);
    const vec2 p = worldXZ - u_object_terrainWorld.xy;
    const vec3 delta = abs(vec3(p, 0.0) - u_terrainCamera.xyz);
    const vec3 scaled = max(delta, vec3(1.0e-4, 1.0e-4, 1.0e-4)) / max(splitDistance * rootSize, 1.0e-4);
    const vec3 levelGuess = 1.0 + floor(-log2(scaled));
    float level = clamp(min(min(levelGuess.x, levelGuess.y), maxLevel), 0.0, maxLevel);
    float tileSize = rootSize / exp2(level);
    const vec2 tileXY = floor(p / tileSize) * tileSize;
    const vec4 dv = abs(vec4(tileXY - u_terrainCamera.xy,
                             tileXY + vec2(tileSize, tileSize) - u_terrainCamera.xy));
    const float distance = max(u_terrainCamera.z,
                               max(min(dv.x, dv.z), min(dv.y, dv.w)));
    if (level < maxLevel && distance < splitDistance * tileSize)
    {
        level += 1.0;
        tileSize *= 0.5;
    }

    const vec2 tile = floor(p / tileSize);
    const vec2 tileUV = mod(p / tileSize, 1.0);
    const float k = u_terrainQuadInfo.z;
    float mapKey;
    if (k > 4.0)
    {
        const float side = pow(2.0, level);
        const float treeBase = (pow(2.0, 2.0 * level) - 1.0) / 3.0;
        mapKey = mod(tile.x + tile.y * side + treeBase, 4093.0);
    }
    else
    {
        const float K = max(u_terrainQuadInfo.w, 2.0);
        const vec2 cameraTileXY = 2.0 * floor(u_terrainCamera.xy / tileSize * 0.5);
        const vec2 dXY = tile - cameraTileXY + vec2(k, k);
        if (dXY.x < 0.0 || dXY.y < 0.0 || dXY.x >= K || dXY.y >= K)
        {
            return -1.0;
        }
        mapKey = (level * K + dXY.y) * K + dXY.x;
    }
    const vec2 entry = texture2D(s_terrainTileMap,
                                 vec2((mapKey + 0.5) / 4096.0, 0.5)).rg * 255.0;
    if (entry.y < 0.5)
    {
        return -1.0;
    }

    atlasUV = (u_terrainMapInfo.w + tileUV * u_terrainMapInfo.z) /
              max(u_terrainMapInfo.y, 1.0);
    return entry.x + (entry.y - 1.0) * 256.0;
}
#endif

SurfaceData hbgMakeSurfaceData(vec3 worldPos, vec3 geometricNormalWS, vec3 tangentWS, vec3 bitangentWS,
                               vec2 uv, float viewSign
#ifdef TERRAIN_INSTANCED
                               , vec4 instancedTerrainTile
#endif
                               )
{
    SurfaceData surfaceData;
    surfaceData.lightLayerMask = 65535.0;
    const float materialFeatureBits = floor(u_material_advanced2.z + 0.5);
    surfaceData.waterMask = materialFeatureBits >= 256.0 && materialFeatureBits < 512.0 ? 1.0 : 0.0;

    vec4 baseColor;
    vec2 normalSample = vec2(0.5, 0.5);
    bool terrainTileActive = false;
#ifdef TERRAIN_TILES
#ifdef TERRAIN_INSTANCED
    const vec4 terrainTile = instancedTerrainTile;
#else
    const vec4 terrainTile = u_object_terrainTile;
#endif
    terrainTileActive = terrainTile.w > 0.0;
#endif
#ifdef TERRAIN_TILES
    if (terrainTileActive)
    {
        vec2 tileUV = hbgSaturate((worldPos.xz - terrainTile.yz) /
                                  max(terrainTile.w, 1.0e-4));
        float tileLayer = terrainTile.x;
        vec2 mappedUV = vec2(0.0, 0.0);
        const float mappedLayer = hbgTerrainTileMapLayer(worldPos.xz, mappedUV);
        if (mappedLayer >= 0.0)
        {
            tileUV = mappedUV;
            tileLayer = mappedLayer;
        }
        baseColor = texture2DArray(s_terrainTiles, vec3(tileUV, tileLayer)) *
                    u_material_baseColorFactor;
        normalSample = texture2DArray(s_terrainTiles, vec3(tileUV, tileLayer + 1.0)).xy;
    }
    else
#endif
    {
        baseColor = texture2D(s_baseColorMap, uv) * u_material_baseColorFactor;
        normalSample = texture2D(s_normalMap, uv).xy;
    }
    surfaceData.baseColor = baseColor.rgb;
    surfaceData.alpha = baseColor.a;

    float metallic = u_material_metallicRoughness.x;
    float roughness = u_material_metallicRoughness.y;

#ifdef HAS_MASK_MAP
    vec4 mask = texture2D(s_metallicRoughnessMap, uv);
    // glTF metallicRoughness: G = roughness, B = metallic.
    metallic *= mask.b;
    roughness *= mask.g;
    float occlusion = texture2D(s_occlusionMap, uv).r;
    float occlusionStrength = u_material_emissiveFactor.w;
    surfaceData.ambientOcclusion = mix(1.0, occlusion, occlusionStrength);
#else
    surfaceData.ambientOcclusion = 1.0;
#endif
    surfaceData.materialAmbientOcclusion = surfaceData.ambientOcclusion;

    surfaceData.metallic = hbgSaturate(metallic);
    surfaceData.perceptualRoughness = hbgSaturate(roughness);
    surfaceData.roughness = max(surfaceData.perceptualRoughness * surfaceData.perceptualRoughness, 0.001);

    vec3 normalWS = geometricNormalWS;
#ifdef HAS_NORMAL_MAP
    vec3 normalTS;
    if (u_material_riverRefractionParams.z > 0.5)
    {
        const vec2 slopes = normalSample;
        normalTS = hbgUnpackSlopeNormal(slopes, max(u_material_riverParticleParams.y, 0.0));
    }
    else
    {
        normalTS = hbgUnpackNormal(normalSample, u_material_metallicRoughness.z);
    }
    vec3 tangent = tangentWS;
    vec3 bitangent = bitangentWS;
    if (dot(tangent, tangent) < 1.0e-8 || dot(bitangent, bitangent) < 1.0e-8)
    {
        // Derivative fallback for meshes without a valid tangent stream. The
        // asset layer normally generates MikkTSpace tangents, but imported
        // meshes without UVs can still reach this path.
        vec3 dp1 = dFdx(worldPos);
        vec3 dp2 = dFdy(worldPos);
        vec2 duv1 = dFdx(uv);
        vec2 duv2 = dFdy(uv);
        float determinant = duv1.x * duv2.y - duv1.y * duv2.x;
        if (abs(determinant) > 1.0e-8)
        {
            tangent = normalize((dp1 * duv2.y - dp2 * duv1.y) / determinant);
            bitangent = cross(geometricNormalWS, tangent);
        }
    }
    mat3 tbn = mtxFromCols(tangent, bitangent, geometricNormalWS);
    normalWS = normalize(mul(tbn, normalTS));
    if (surfaceData.waterMask > 0.5)
    {
        // River wave tiles store the bank mask in alpha: 0 at the banks and
        // 1 in the center. Use it to soften bank roughness.
        const float bankMask = hbgSaturate(texture2D(s_normalMap, uv).a);
        surfaceData.perceptualRoughness = mix(0.60, surfaceData.perceptualRoughness, bankMask);
        surfaceData.roughness = max(surfaceData.perceptualRoughness * surfaceData.perceptualRoughness, 0.001);
    }
#else
    normalWS = normalize(geometricNormalWS);
#endif

#ifdef DOUBLE_SIDED
    if (viewSign < 0.0)
    {
        normalWS = -normalWS;
    }
#endif

#ifdef WATER_FFT
    if (surfaceData.waterMask > 0.5)
    {
        const vec4 gridSizes = max(u_material_waterFftGridSizes,
                                   vec4(1.0e-4, 1.0e-4, 1.0e-4, 1.0e-4));
        const vec2 fftCoord = worldPos.xz;
        const vec4 slope0 = texture2DArray(s_waterFFT, vec3(fftCoord / gridSizes.x, 1.0));
        const vec4 slope1 = texture2DArray(s_waterFFT, vec3(fftCoord / gridSizes.y, 1.0));
        const vec4 slope2 = texture2DArray(s_waterFFT, vec3(fftCoord / gridSizes.z, 2.0));
        const vec4 slope3 = texture2DArray(s_waterFFT, vec3(fftCoord / gridSizes.w, 2.0));
        vec2 slopes = vec2(slope0.x + slope1.z + slope2.x + slope3.z,
                           slope0.y + slope1.w + slope2.y + slope3.w);
        if (u_material_waterSphere.x > 0.0)
        {
            const float centerY = u_material_waterSphere.y - u_material_waterSphere.x;
            const float vertical = worldPos.y - centerY;
            if (vertical > 1.0e-3)
            {
                slopes -= vec2(worldPos.x - u_global_cameraPos.x,
                               worldPos.z - u_global_cameraPos.z) / vertical;
            }
        }
        normalWS = normalize(vec3(-slopes.x, 1.0, -slopes.y));
        geometricNormalWS = normalWS;
    }
#endif

    surfaceData.normalWS = normalWS;
    surfaceData.geometricNormalWS = normalize(geometricNormalWS);

#ifdef TERRAIN_TILES
    vec3 emissive = vec3(0.0, 0.0, 0.0);
#else
    vec3 emissive = texture2D(s_emissiveMap, uv).rgb * u_material_emissiveFactor.rgb;
#endif
    surfaceData.emissive = max(emissive, vec3(0.0, 0.0, 0.0));
    surfaceData.contactShadowMask = 0.0;
    surfaceData.contactShadowFade = 0.0;
#ifdef HBG_ADVANCED_MATERIAL
    surfaceData.tangentWS = vec3(0.0, 0.0, 0.0);
    surfaceData.clearCoatMask = 0.0;
    surfaceData.clearCoatRoughness = 0.0;
    surfaceData.clearCoatNormalScale = 1.0;
    surfaceData.transmissionMask = 0.0;
    surfaceData.thickness = 0.0;
    surfaceData.ior = 1.5;
    surfaceData.anisotropy = 0.0;
    surfaceData.anisotropyRotation = 0.0;
    surfaceData.iridescenceMask = 0.0;
    surfaceData.iridescenceThickness = 0.0;
    surfaceData.subsurfaceMask = 0.0;
#endif

    return surfaceData;
}

#endif // HBG_SURFACE_DATA_SC

