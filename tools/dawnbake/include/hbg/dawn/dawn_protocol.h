#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace hbg::dawn {

inline constexpr int32_t kExportVersion1000 = 1000;
inline constexpr int32_t kExportVersion1001 = 1001;
inline constexpr int32_t kExportVersion1002 = 1002;
inline constexpr int32_t kExportVersion = kExportVersion1002;

inline constexpr size_t kMaxLightProfileSize = 256;
inline constexpr size_t kLightProfileBytes = kMaxLightProfileSize * kMaxLightProfileSize;

// FLightInfo.Flags
inline constexpr uint32_t kLightFlagLightingChannelMask = 7u << 0;
inline constexpr uint32_t kLightFlagTransmission = 1u << 3;
inline constexpr uint32_t kLightFlagNonInverseSquareFalloff = 1u << 4;
inline constexpr uint32_t kLightFlagStationary = 1u << 5;
inline constexpr uint32_t kLightFlagTypeMask = 7u << 6;
inline constexpr uint32_t kLightFlagSky = 0u << 6;
inline constexpr uint32_t kLightFlagDirectional = 1u << 6;
inline constexpr uint32_t kLightFlagPoint = 2u << 6;
inline constexpr uint32_t kLightFlagSpot = 3u << 6;
inline constexpr uint32_t kLightFlagRect = 4u << 6;
inline constexpr uint32_t kLightFlagDisc = 5u << 6;
inline constexpr uint32_t kLightFlagCastShadow = 1u << 9;
inline constexpr uint32_t kLightFlagTwoSide = 1u << 10;
inline constexpr uint32_t kLightFlagDistanceFieldShadow = 1u << 11;
inline constexpr uint32_t kLightFlagDirectLighting = 1u << 12;
inline constexpr uint32_t kLightFlagIndirectLighting = 1u << 13;
inline constexpr uint32_t kLightFlagDirectLightingProbe = 1u << 14;
inline constexpr uint32_t kLightFlagIndirectLightingProbe = 1u << 15;
inline constexpr uint32_t kLightFlagHasStaticLighting = 1u << 16;
inline constexpr uint32_t kLightFlagBakeryFalloff = 1u << 17;
inline constexpr uint32_t kLightFlagBakeryFakeFalloff = 1u << 18;

// ELightmapFlags
inline constexpr uint32_t kLightmapFlagDirect = 1u << 0;
inline constexpr uint32_t kLightmapFlagIndirect = 1u << 1;
inline constexpr uint32_t kLightmapFlagDirection = 1u << 2;
inline constexpr uint32_t kLightmapFlagBilinearFilter = 1u << 10;
inline constexpr uint32_t kLightmapFlagInteriorMask = 1u << 11;
inline constexpr uint32_t kLightmapFlagPadding = 1u << 20;
inline constexpr uint32_t kLightmapFlagOverrideParams = 1u << 21;
inline constexpr uint32_t kLightmapFlagQuantized = 1u << 22;
inline constexpr uint32_t kLightmapFlagBmStatic = 1u << 23;
inline constexpr uint32_t kLightmapFlagBmPrtHq = 1u << 24;
inline constexpr uint32_t kLightmapFlagBmPrtLq = 1u << 25;

// EMeshInstanceFlags
inline constexpr uint32_t kMeshInstanceFlagShadowCast = 1u << 0;
inline constexpr uint32_t kMeshInstanceFlagTwoSide = 1u << 1;
inline constexpr uint32_t kMeshInstanceFlagTransient = 1u << 2;
inline constexpr uint32_t kMeshInstanceFlagUseVertexNormal = 1u << 3;
inline constexpr uint32_t kMeshInstanceFlagReverseWinding = 1u << 4;
inline constexpr uint32_t kMeshInstanceFlagShareLod = 1u << 5;
inline constexpr uint32_t kMeshInstanceFlagLandscape = 1u << 10;
inline constexpr uint32_t kMeshInstanceFlagSpline = 1u << 11;
inline constexpr uint32_t kMeshInstanceFlagUseTwoSideLighting = 1u << 12;
inline constexpr uint32_t kMeshInstanceFlagUseEmissionLighting = 1u << 13;
inline constexpr uint32_t kMeshInstanceFlagIsOutdoor = 1u << 20;
inline constexpr uint32_t kMeshInstanceFlagIsForceVolumetric = 1u << 21;

// EMaterialInfoFlags
inline constexpr uint32_t kMaterialFlagTwoSide = 1u << 0;
inline constexpr uint32_t kMaterialFlagSubsurface = 1u << 1;
inline constexpr uint32_t kMaterialFlagShadowAsMask = 1u << 2;
inline constexpr uint32_t kMaterialFlagSurfaceDomain = 1u << 3;

// ETextureFormat.UBYTE4
inline constexpr uint32_t kTextureFormatUByte4 = 19;

struct Float2 {
    float x = 0.0f;
    float y = 0.0f;
};
static_assert(sizeof(Float2) == 8);

struct Float3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};
static_assert(sizeof(Float3) == 12);

struct Float4 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;
};
static_assert(sizeof(Float4) == 16);

struct Int2 {
    int32_t x = 0;
    int32_t y = 0;
};
static_assert(sizeof(Int2) == 8);

struct Int3 {
    int32_t x = 0;
    int32_t y = 0;
    int32_t z = 0;
};
static_assert(sizeof(Int3) == 12);

struct Int4 {
    int32_t x = 0;
    int32_t y = 0;
    int32_t z = 0;
    int32_t w = 0;
};
static_assert(sizeof(Int4) == 16);

struct UInt2 {
    uint32_t x = 0;
    uint32_t y = 0;
};
static_assert(sizeof(UInt2) == 8);

struct UInt3 {
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t z = 0;
};
static_assert(sizeof(UInt3) == 12);

struct UShort2 {
    uint16_t x = 0;
    uint16_t y = 0;
};
static_assert(sizeof(UShort2) == 4);

struct UShort3 {
    uint16_t x = 0;
    uint16_t y = 0;
    uint16_t z = 0;
};
static_assert(sizeof(UShort3) == 6);

struct UByte4 {
    uint8_t x = 0;
    uint8_t y = 0;
    uint8_t z = 0;
    uint8_t w = 0;
};
static_assert(sizeof(UByte4) == 4);

struct Matrix4x4 {
    float mm[16] = {};
};
static_assert(sizeof(Matrix4x4) == 64);

struct Guid {
    uint32_t a = 0;
    uint32_t b = 0;
    uint32_t c = 0;
    uint32_t d = 0;
};
static_assert(sizeof(Guid) == 16);

struct ShaHash {
    uint8_t hash[20] = {};
};
static_assert(sizeof(ShaHash) == 20);

struct LightProfile {
    uint8_t profile[kLightProfileBytes] = {};
};
static_assert(sizeof(LightProfile) == kLightProfileBytes);

struct SceneBounds {
    Float3 origin;
    Float3 sceneCenterPosition;
    Float3 boxExtent;
    float sphereRadius = 0.0f;
};
static_assert(sizeof(SceneBounds) == 40);
static_assert(offsetof(SceneBounds, sceneCenterPosition) == 12);
static_assert(offsetof(SceneBounds, boxExtent) == 24);
static_assert(offsetof(SceneBounds, sphereRadius) == 36);

struct SceneBoxBounds {
    Float3 min;
    Float3 max;
};
static_assert(sizeof(SceneBoxBounds) == 24);

struct Texture2DInfoHeader {
    uint32_t sizeX = 0;
    uint32_t sizeY = 0;
    uint32_t wrapMode = 0;
    uint32_t format = 0;
};
static_assert(sizeof(Texture2DInfoHeader) == 16);

struct MaterialColor {
    uint8_t x = 0;
    uint8_t y = 0;
    uint8_t z = 0;
    uint8_t w = 0;
};
static_assert(sizeof(MaterialColor) == 4);

struct MaterialInfoHeader {
    ShaHash hash;
    int32_t shadingModelId = 0;
    uint32_t blendMode = 0;
    uint32_t bTwoSided = 0;
    uint32_t flags = 0;
    float opacityMaskClipValue = 0.0f;
    uint32_t indexInSceneInfo = 0;
    float diffuseBoost = 1.0f;
    float emissiveBoost = 1.0f;
};
static_assert(sizeof(MaterialInfoHeader) == 52);
static_assert(offsetof(MaterialInfoHeader, shadingModelId) == 20);
static_assert(offsetof(MaterialInfoHeader, diffuseBoost) == 44);

struct LightInfo {
    Float3 position;
    Float3 normal;
    Float3 dPdu;
    Float3 dPdv;
    Float3 color;
    Float3 dimensions;
    Float2 shaping;
    float attenuation = 1.0f;
    float falloffExponent = 0.0f;
    uint32_t flags = 0;
    int32_t iesTextureSlice = -1;
    Float3 indirectColor;
    Float3 boundMin;
    Float3 boundMax;
    int32_t lightTextureMode = 0;
    float shadowTranslucency = 0.0f;
    uint32_t bEnablePenumbra = 0;
    Float4 penumbraTint{1.0f, 1.0f, 1.0f, 1.0f};
    float penumbraLuma = 1.0f;
    uint8_t padding[12] = {};
};
static_assert(sizeof(LightInfo) == 176);
static_assert(offsetof(LightInfo, attenuation) == 80);
static_assert(offsetof(LightInfo, indirectColor) == 96);
static_assert(offsetof(LightInfo, penumbraTint) == 144);

struct LightFullInfo {
    LightInfo lightData;
    Guid lightGuid;
    float lightPower = 0.0f;
    uint8_t padding[4] = {};
};
static_assert(sizeof(LightFullInfo) == 200);

struct SkyLightInfoHeader {
    Float3 color;
    Int3 mipDimensions;
    Float3 envColor;
    Matrix4x4 transform;
    float indirectLightingIntensity = 1.0f;
    float shadowTranslucency = 0.0f;
    uint32_t flags = 0;
};
static_assert(sizeof(SkyLightInfoHeader) == 112);
static_assert(offsetof(SkyLightInfoHeader, transform) == 36);
static_assert(offsetof(SkyLightInfoHeader, indirectLightingIntensity) == 100);

struct MeshElementInfo {
    uint32_t startIndex = 0;
    uint32_t primitiveCount = 0;
    uint32_t materialIndex = 0;
    uint32_t flags = 0;
};
static_assert(sizeof(MeshElementInfo) == 16);

struct MeshInfoHeader {
    Guid guid;
    uint32_t lodIndex = 0;
    uint32_t numTriangles = 0;
    uint32_t numVertices = 0;
    uint32_t numElements = 0;
    int32_t lightmapUVIndex = 0;
    float lightmapUVDensity = 0.0f;
    float lightmapTexelDensity = 0.0f;
    uint32_t indexInSceneInfo = 0;
};
static_assert(sizeof(MeshInfoHeader) == 48);
static_assert(offsetof(MeshInfoHeader, lightmapUVIndex) == 32);

struct MeshInstanceInfoHeader {
    Guid levelGuid;
    Guid guid;
    Matrix4x4 transform;
    Matrix4x4 inverseTransform;
    Matrix4x4 inverseTransposeTransform;
    uint32_t lightmapId = 0;
    SceneBoxBounds boundingBox;
    uint32_t meshIndex = 0;
    uint32_t texcoordIndex = 0;
    uint32_t flags = 0;
    float diffuseBoost = 1.0f;
    float emissiveBoost = 1.0f;
    float rayOffset = 0.0f;
    float fullyOccludedSamplesFraction = 1.0f;
    float meshLightAttenuationRadius = 0.0f;
    uint32_t meshInstanceVisibilityMask = 0xFFFFFFFFu;
    uint32_t meshInstanceVisibilityMaskInHlodScene = 0xFFFFFFFFu;
    int32_t lodLevelInHlodScene = 0;
    UInt2 uv2MapSize;
    UInt2 uv2Offset;
};
static_assert(sizeof(MeshInstanceInfoHeader) == 312);
static_assert(offsetof(MeshInstanceInfoHeader, lightmapId) == 224);
static_assert(offsetof(MeshInstanceInfoHeader, boundingBox) == 228);
static_assert(offsetof(MeshInstanceInfoHeader, uv2MapSize) == 296);
static_assert(offsetof(MeshInstanceInfoHeader, uv2Offset) == 304);

struct LandscapeInfoHeader {
    Guid levelGuid;
    Guid guid;
    Matrix4x4 transform;
    SceneBoxBounds boundingBox;
    uint32_t flags = 0;
    float diffuseBoost = 1.0f;
    float emissiveBoost = 1.0f;
    float rayOffset = 0.0f;
    float fullyOccludedSamplesFraction = 1.0f;
    int32_t numTriangles = 0;
    int32_t componentSizeQuads = 0;
    float lightMapRatio = 1.0f;
    int32_t expandQuadsX = 0;
    int32_t expandQuadsY = 0;
    uint32_t meshInstanceVisibilityMask = 0xFFFFFFFFu;
    uint32_t meshInstanceVisibilityMaskInHlodScene = 0xFFFFFFFFu;
    uint32_t indexInSceneInfo = 0;
};
static_assert(sizeof(LandscapeInfoHeader) == 172);

struct SceneInfoHeader {
    SceneBounds bounds;
    SceneBounds importanceBounds;
    uint32_t numLights = 0;
    uint32_t numMeshes = 0;
    uint32_t numMaterials = 0;
    uint32_t numLandscapes = 0;
    uint32_t numLightBvhLights = 0;
    uint32_t numMeshInstances = 0;
    uint32_t numMeshGuids = 0;
};
static_assert(sizeof(SceneInfoHeader) == 108);

struct SceneDetailVolume {
    SceneBoxBounds bounds;
    Guid levelGuid;
};
static_assert(sizeof(SceneDetailVolume) == 40);

template <typename T>
struct Texture2D {
    Texture2DInfoHeader header;
    std::vector<T> colors;
};

struct MaterialInfo {
    MaterialInfoHeader header;
    Texture2D<MaterialColor> diffuseTexture;
    Texture2D<MaterialColor> transmissionTexture;
    Texture2D<MaterialColor> emissiveTexture;
    Texture2D<MaterialColor> normalTexture;
    Texture2D<MaterialColor> specularTexture;
};

struct DensityVolume {
    std::vector<Float4> planes;
    Int2 allowedMipLevelRange;
};

struct SkyLightInfo {
    SkyLightInfoHeader skyData;
    std::vector<Float4> envCube;
};

struct MeshInfo {
    MeshInfoHeader header;
    std::vector<uint16_t> index16;
    std::vector<MeshElementInfo> elements;
    std::vector<Float4> vertices;
    std::vector<Float4> normals;
    std::vector<Float4> tangents;
    std::vector<Float4> bitangents;
    std::vector<std::vector<Float2>> texcoords;
    std::vector<uint32_t> index32;
};

struct MeshInstanceInfo {
    MeshInstanceInfoHeader header;
    std::vector<uint32_t> materialOverrides;
    std::vector<uint32_t> relevantLights;
};

struct LandscapeInfo {
    LandscapeInfoHeader header;
    std::vector<uint32_t> materialOverrides;
    std::vector<uint32_t> relevantLights;
    std::vector<UByte4> heightMap;
    std::vector<uint8_t> holeMask;
};

struct ScenePartitionInfo {
    Guid scenePartitionGuid;
    std::vector<int32_t> materialIndices;
    std::vector<int32_t> meshIndices;
    std::vector<int32_t> meshInstanceIndices;
    std::vector<int32_t> extendMeshInstanceIndices;
    std::vector<int32_t> landscapeIndices;
    std::vector<int32_t> lightIndices;
    std::vector<int32_t> globalToLocalMaterialIndices;
    std::vector<int32_t> globalToLocalMeshIndices;
    std::vector<int32_t> detailVolumeLightProbeJobIndices;
    std::vector<int32_t> importantVolumeLightProbeJobIndices;
    std::vector<int32_t> customLightProbeJobIndices;
    std::vector<int32_t> volumetricLightProbeJobIndices;
    std::vector<int32_t> customVolumetricLightProbeJobIndices;
};

struct SceneNoPartitionInfo {
    std::vector<int32_t> meshIndices;
    std::vector<Guid> meshGuid;
    std::vector<uint32_t> meshLodIndex;
    std::vector<int32_t> materialIndices;
    std::vector<ShaHash> materialHash;
    std::vector<int32_t> landscapeIndices;
    std::vector<Guid> landscapeGuid;
    std::vector<int32_t> lightIndices;
    std::vector<LightInfo> lights;
    std::vector<int32_t> meshInstanceIndices;
    std::vector<MeshInstanceInfo> meshInstances;
};

struct SceneInfo {
    SceneInfoHeader header;
    std::vector<SceneBoxBounds> importanceVolumes;
    std::vector<DensityVolume> densityVolumes;
    std::vector<SceneDetailVolume> detailVolumes;
    SkyLightInfo skyLight;
    std::vector<LightInfo> lights;
    std::vector<Guid> lightGuids;
    std::vector<float> lightPowers;
    std::vector<Texture2D<float>> iesTextureDatas;
    std::vector<Texture2D<Float4>> cookieTextureDatas;
    std::vector<MaterialInfo> materials;
    std::vector<MeshInfo> meshes;
    std::vector<MeshInstanceInfo> meshInstances;
    std::vector<Guid> meshGuids;
    std::vector<LandscapeInfo> landscapes;
    std::vector<ScenePartitionInfo> scenePartitions;
    SceneNoPartitionInfo sceneNoPartitionInfo;
};

// ---------------------------------------------------------------------------
// Baking job parameters
// ---------------------------------------------------------------------------

struct Lightmap2DBakingParams {
    int32_t denoiserMode = 1;
    int32_t iterationNum = 64;
    int32_t superSampleFactor = 1;
    int32_t maxDepth = 3;
    float penumbraShadowFraction = 0.3f;
    float skyOcclusionCompensateScale = 1.0f;
    int32_t maxLightmapResolution = 64;
    uint8_t bGpuRaster = 0;
    uint8_t bUseLightmapDirection = 1;
    uint8_t bSkyOcclusionDenoise = 0;
    uint8_t padding = 0;
    int32_t optixSmoothLevel = 0;
};
static_assert(sizeof(Lightmap2DBakingParams) == 36);

struct DawnDenoiserParams {
    uint8_t bEnableDawnLightmapDenoiser = 0;
    uint8_t bDenoiserSoftShadow = 0;
    uint16_t padding = 0;
    int32_t lightLeakFixMode = 0;
    int32_t directSmoothFactor = 0;
    int32_t indirectSmoothFactor = 0;
};
static_assert(sizeof(DawnDenoiserParams) == 16);

struct ArtifactBakingParams {
    int32_t bSeamFixed = 0;
    int32_t seamFixLevel = 0;
    float seamBlendScale = 0.0f;
    uint8_t bFillUnmappedTexel = 0;
    uint8_t padding[3] = {};
};
static_assert(sizeof(ArtifactBakingParams) == 16);

struct AmbientOcclusionBakingParams {
    uint32_t bDebugAmbientOcclusion = 0;
    uint32_t bUseAmbientOcclusion = 0;
    float maxAmbientOcclusion = 0.0f;
    float directAmbientOcclusionFactor = 0.0f;
    float indirectAmbientOcclusionFactor = 0.0f;
    float fullyOccludedSamplesFraction = 0.0f;
    float occlusionExponent = 0.0f;
};
static_assert(sizeof(AmbientOcclusionBakingParams) == 28);

struct LightProbeBakingParams {
    int32_t iterationNumForLightProbe = 0;
    int32_t maxDepthForLightProbe = 0;
    uint8_t bEnableDistanceMap = 0;
    uint8_t padding0[3] = {};
    int32_t minProbeDistance = 0;
    uint8_t bSamplingOptimization = 0;
    uint8_t padding1[3] = {};
    float misSamplingFraction = 0.0f;
    float misSamplingFractionForSampling = 0.0f;
    int32_t probeResolution = 0;
    int32_t startIterationForSave = 0;
    int32_t buildIteration = 0;
    int32_t baseInitialSpp = 0;
    float skyOcclusionCompensateScale = 0.0f;
};
static_assert(sizeof(LightProbeBakingParams) == 48);

struct LightBvhParams {
    uint32_t directLightSampleMode = 0;
    float maxPathIntensity = 0.0f;
    uint32_t lightGridResolution = 0;
    uint32_t maxLightCountPerGrid = 0;
    float virtualLightDirectIntensity = 0.0f;
    float virtualLightIndirectIntensity = 0.0f;
    uint32_t virtualLightSampleMode = 0;
};
static_assert(sizeof(LightBvhParams) == 28);

struct SdfShadowBakingParams {
    int32_t maxUpsamplingFactor = 0;
    float shadowRayFactor = 0.0f;
    int32_t denoiserMode = 0;
    int32_t shadowMaskSmoothLevel = 0;
};
static_assert(sizeof(SdfShadowBakingParams) == 16);

struct RayTracingBakingParams {
    float minRayOffset = 0.0f;
    float maxRayOffset = 0.0f;
    float minNormalOffset = 0.0f;
    float normalOffsetSampleRadiusScale = 0.0f;
    float tangentOffsetSampleRadiusScale = 0.0f;
};
static_assert(sizeof(RayTracingBakingParams) == 20);

struct RasterizationBakingParams {
    float rasterizationBias = 0.0f;
    float smallestTexelRadius = 0.0f;
    uint8_t bUseConservativeTexelRasterization = 0;
    uint8_t bUseMaxWeight = 0;
    uint8_t padding[2] = {};
};
static_assert(sizeof(RasterizationBakingParams) == 12);

struct ViewerParams {
    uint8_t bEnableDebugViewer = 0;
    uint8_t bEnableRealtimeViewer = 0;
    uint8_t bLaunchAfterBaked = 0;
    uint8_t bEnableLightmapViewer = 0;
};
static_assert(sizeof(ViewerParams) == 4);

struct Lightmap2DOverrideBakingParams {
    Lightmap2DBakingParams lightmapBakingParams;
    ArtifactBakingParams artifactBakingParams;
    DawnDenoiserParams denoiserParameters;
    AmbientOcclusionBakingParams aoBakingParams;
    uint8_t bPrecomputedRadianceTransferForSurface = 0;
    uint8_t padding[3] = {};
};
static_assert(sizeof(Lightmap2DOverrideBakingParams) == 100);

struct BakingJobParameters {
    uint32_t bUseFastVoxelization = 0;
    Lightmap2DBakingParams lightmap2DParameter;
    SdfShadowBakingParams sdfShadowParameter;
    AmbientOcclusionBakingParams ambientOcclusionParameter;
    LightProbeBakingParams lightProbeParameter;
    RayTracingBakingParams rayTracingParameter;
    ArtifactBakingParams artifactParameter;
    RasterizationBakingParams rasterizationParameter;
    DawnDenoiserParams denoiserParameters;
    ViewerParams debugViewerParameters;
    LightBvhParams lightBvhParameter;
    uint8_t bPrtHqEnabled = 0;
    uint8_t bPrtLqEnabled = 0;
    uint8_t padding[2] = {};
};
static_assert(sizeof(BakingJobParameters) == 232);
static_assert(offsetof(BakingJobParameters, lightmap2DParameter) == 4);
static_assert(offsetof(BakingJobParameters, lightBvhParameter) == 200);
static_assert(offsetof(BakingJobParameters, bPrtHqEnabled) == 228);

struct AdaptiveVolumetricLightmapParams {
    Int3 topLevelGridSize;
    Float3 volumeMin;
    Float3 volumeSize;
    float detailCellSize = 0.0f;
    int32_t brickSize = 0;
    float surfaceLightmapMinTexelsPerVoxelAxis = 0.0f;
    float voxelizationCellExpansionForSurfaceGeometry = 0.0f;
    float voxelizationCellExpansionForVolumeGeometry = 0.0f;
    int32_t maxRefinementLevels = 0;
    float windowingTargetLaplacian = 0.0f;
    float reduceShRingingPercent = 0.0f;
    float killDarkMaxLumPercent = 0.0f;
    float killDarkValueAddPercent = 0.0f;
    float minBrickError = 0.0f;
    float voxelizationCellExpansionForLights = 0.0f;
    float lightBrightnessSubdivideThreshold = 0.0f;
    uint8_t bCullBricksBelowLandscape = 0;
    uint8_t padding[3] = {};
};
static_assert(sizeof(AdaptiveVolumetricLightmapParams) == 92);

struct SparseVolumetricSamplesParams {
    int32_t numSurfaceSampleLayers = 0;
    float surfaceLightSampleSpacing = 0.0f;
    float firstSurfaceSampleLayerHeight = 0.0f;
    float surfaceSampleLayerHeightSpacing = 0.0f;
    float detailVolumeSampleSpacing = 0.0f;
    float volumeLightSampleSpacing = 0.0f;
    int32_t maxVolumeSamples = 0;
    int32_t maxSurfaceLightSamples = 0;
    uint8_t bUseMaxSurfaceSampleNum = 0;
    uint8_t padding[3] = {};
};
static_assert(sizeof(SparseVolumetricSamplesParams) == 36);

struct ShadowSettingParams {
    int32_t approximateHighResTexelsPerMaxTransitionDistance = 0;
    float maxTransitionDistanceWorldSpace = 0.0f;
    int32_t minDistanceFieldUpsampleFactor = 0;
    uint8_t bAllowSignedDistanceFieldShadows = 0;
    uint8_t padding0[3] = {};
    int32_t staticShadowDepthMapMaxSamples = 0;
    int32_t staticShadowDepthMapSuperSampleFactor = 0;
    float staticShadowDepthMapTransitionSampleDistanceX = 0.0f;
    float staticShadowDepthMapTransitionSampleDistanceY = 0.0f;
};
static_assert(sizeof(ShadowSettingParams) == 32);

struct PrecomputedVisibilityParameterHeader {
    int32_t numCellDistributionBuckets = 0;
    int32_t minMeshSamples = 0;
    int32_t maxMeshSamples = 0;
    float meshBoundsScale = 0.0f;
    float cellSize = 0.0f;
    float playAreaHeight = 0.0f;
    uint8_t bPlaceCellsOnOpaqueOnly = 0;
    uint8_t bPlaceCellsOnlyAlongCameraTracks = 0;
    uint8_t padding[2] = {};
};
static_assert(sizeof(PrecomputedVisibilityParameterHeader) == 28);

struct PrecomputedVisibilityParameters {
    PrecomputedVisibilityParameterHeader header;
    std::vector<Float3> cameraTrackPositions;
};

struct BakingDebugInput100 {
    Guid meshId;
    Guid lightmapGroupGuid;
    Float3 position;
    int32_t sizeX = 0;
    int32_t sizeY = 0;
    int32_t localX = 0;
    int32_t localY = 0;
    uint32_t dawnLogLevel = 0;
    uint8_t bEnableMemManager = 0;
    uint8_t padding[3] = {};
};
static_assert(sizeof(BakingDebugInput100) == 68);

struct BakingDebugInput {
    BakingDebugInput100 debugInput100;
    Float3 cameraPosition;
    Float3 lookatPosition;
};

struct PartialJobsTotalNum {
    uint32_t numLightmap2DJobs = 0;
    uint32_t numSdfShadowJobs = 0;
    uint32_t numTextureMappingJobs = 0;
    uint32_t numLightmapOcclusionJobs = 0;
    uint32_t numDetailVolumeLightProbeJobs = 0;
    uint32_t numImportanceVolumeLightProbeJobs = 0;
    uint32_t numCustomLightProbeJobs = 0;
    uint32_t numVolumetricLightmapTaskGuids = 0;
    uint32_t numCustomVolumetricLightmapJobs = 0;
    uint32_t numMappingGroupJobs = 0;
    uint32_t numMappingSplitJobs = 0;
    uint32_t numSdfShadowGroupJobs = 0;
    uint32_t numLightmapOcclusionGroupJobs = 0;
};
static_assert(sizeof(PartialJobsTotalNum) == 52);

struct Lightmap2DInput {
    Guid jobId;
    Guid meshId;
    uint32_t flags = 0;
    uint32_t isHlodActorMapping = 0;
    uint32_t scenePartitionIndex = 0;
    int32_t padding = 0;
    Int2 size;
    uint16_t lightmapUVIndex = 0;
    uint8_t padding1[2] = {};
    Lightmap2DOverrideBakingParams overrideBakingParams;
};
static_assert(sizeof(Lightmap2DInput) == 160);
static_assert(offsetof(Lightmap2DInput, overrideBakingParams) == 60);

struct Lightmap2DSample {
    Float4 shVectorR;
    Float4 shVectorG;
    Float4 shVectorB;
    Float4 incidentLighting;
    float shCorrection = 0.0f;
    Float3 skyOcclusion;
    float aoMaterialMask = 0.0f;
    Float4 prtHqShVectorR;
    Float4 prtHqShVectorG;
    Float4 prtHqShVectorB;
    Float4 prtLqShVectorR;
    Float4 prtLqShVectorG;
    Float4 prtLqShVectorB;
    uint8_t bIsMapped = 0;
    uint8_t padding[11] = {};
};
static_assert(sizeof(Lightmap2DSample) == 192);
static_assert(offsetof(Lightmap2DSample, incidentLighting) == 48);
static_assert(offsetof(Lightmap2DSample, shCorrection) == 64);
static_assert(offsetof(Lightmap2DSample, skyOcclusion) == 68);
static_assert(offsetof(Lightmap2DSample, aoMaterialMask) == 80);
static_assert(offsetof(Lightmap2DSample, prtHqShVectorR) == 84);
static_assert(offsetof(Lightmap2DSample, bIsMapped) == 180);

struct Lightmap2DOutputHeader {
    Guid jobId;
    Guid meshId;
    Int2 size;
    double executionTime = 0.0;
};
static_assert(sizeof(Lightmap2DOutputHeader) == 48);
static_assert(offsetof(Lightmap2DOutputHeader, executionTime) == 40);

struct MappingGroupInput {
    Guid jobId;
    int32_t groupedSize = 0;
    int32_t scenePartitionIndex = 0;
    std::vector<Int3> mappingOffsets;
};

struct Lightmap2DOutput {
    Lightmap2DOutputHeader header;
    int32_t jobInputIndex = 0;
    uint8_t bHasPrtHqTextures = 0;
    uint8_t bHasPrtLqTextures = 0;
    uint8_t bHasStaticTextures = 0;
    std::vector<Guid> lights;
    std::vector<Lightmap2DSample> lightmapData;
};

struct BakingJobInputs {
    BakingJobParameters bakingParameters;
    AdaptiveVolumetricLightmapParams adaptiveVolumetricLightmapParameter;
    SparseVolumetricSamplesParams sparseVolumetricSamplesParameter;
    ShadowSettingParams shadowSettingParameter;
    PrecomputedVisibilityParameters precomputedVisibilityParameter;
    BakingDebugInput debugInput;
    std::vector<Lightmap2DInput> lightmap2DJobs;
    std::vector<MappingGroupInput> mappingGroupJobs;

    // Arrays unused by the v1 lightmap path. They are serialized as counts and
    // must stay zero; non-zero data from a real Dawn job is rejected by the reader.
    uint32_t sdfShadowJobsCount = 0;
    uint32_t sdfShadowGroupJobsCount = 0;
    uint32_t shadowDepthMapJobsCount = 0;
    uint32_t customLightProbeJobsCount = 0;
    uint32_t importanceVolumeLightProbeJobsCount = 0;
    uint32_t detailVolumeLightProbeJobsCount = 0;
    uint32_t visibilityBucketGuidsCount = 0;
    uint32_t volumetricLightmapTaskGuidsCount = 0;
    uint32_t customVolumetricLightmapJobsCount = 0;
    uint32_t mappingGroupJobsCount = 0;
    uint32_t mappingSplitJobsCount = 0;
    uint32_t seamFixGroupJobsCount = 0;
    uint32_t scenePartitionTasksCount = 0;

    PartialJobsTotalNum partialJobsTotalNum;

    uint32_t lightmapOcclusionJobsCount = 0;
    uint32_t occlusionGroupJobsCount = 0;
    uint32_t textureMappingJobsCount = 0;
};

}  // namespace hbg::dawn
