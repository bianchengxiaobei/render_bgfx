#pragma once

#include "sg/compile.h"

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace sg::detail {

constexpr std::uint32_t kGraphFeatureSceneColor = 1u << 0;
constexpr std::uint32_t kGraphFeatureSceneDepth = 1u << 1;
constexpr std::uint32_t kGraphFeatureFog = 1u << 2;
constexpr std::uint32_t kGraphFeatureSupported =
    kGraphFeatureSceneColor | kGraphFeatureSceneDepth | kGraphFeatureFog;

struct PassSpec {
    std::string id;
    std::string vertexTemplate;
    std::string fragmentTemplate;
    std::vector<std::string> defines;
};

struct TargetSpec {
    std::string id;
    std::vector<PassSpec> passes;
    std::map<std::string, Value> defaults;
    std::vector<std::string> blocks;
    int textureBudget = 5;
    int uniformBudget = 32;
    int matrixBudget = 4;
    bool supportsAlphaClip = true;
    bool supportsDoubleSided = true;
};

const TargetSpec* findTarget(std::string_view id);

inline std::optional<ValueType> textureTypeForNode(std::string_view type) {
    std::string key;
    for (char character : type) {
        if (character != ' ' && character != '-') {
            key.push_back(character);
        }
    }
    if (key == "SampleTexture2D" || key == "SampleTexture2DLOD" ||
        key == "TextureSize" || key == "GatherTexture2D" ||
        key == "CalculateLevelOfDetailTexture2D" ||
        key == "SplitTextureTransform" || key == "NormalFromTexture" ||
        key == "ParallaxMapping" || key == "ParallaxOcclusionMapping" ||
        key == "Triplanar" || key == "Texture2DAsset") {
        return ValueType::Texture2D;
    }
    if (key == "SampleTexture2DArray" || key == "Texture2DArrayAsset") {
        return ValueType::Texture2DArray;
    }
    if (key == "SampleTexture3D" || key == "Texture3DAsset") {
        return ValueType::Texture3D;
    }
    if (key == "SampleCubemap" || key == "SampleReflectedCubemap" ||
        key == "CubemapAsset") {
        return ValueType::Cubemap;
    }
    return std::nullopt;
}

constexpr int kMaxVirtualTextureLayers = 4;

struct VirtualTextureSourceInfo {
    NodeId node = kInvalidNode;
    std::string name;
    int layers = 2;
    std::array<std::string, 4> layerRefNames;
    std::array<std::string, 4> layerTextureTypes;
    std::string addressMode = "wrap";
    std::string lodCalculation = "automatic";
    std::string sampleQuality = "high";
    bool enableGlobalMipBias = true;
    bool noFeedback = false;
};

std::optional<VirtualTextureSourceInfo> virtualTextureSource(const Graph& graph,
                                                             const NodeInstance& sampleNode);
std::string virtualTextureLayerRefName(std::string_view name, int layer);

struct StageBinding {
    std::string semantic;
    int stage = 0;
};

const std::vector<StageBinding>& stageMap();
bool validatePassPlan(const TargetSpec& target, std::string& error);

struct ActiveGraph {
    bool ok = false;
    std::vector<NodeId> order;
    std::set<NodeId> nodes;
    std::vector<Diagnostic> diagnostics;
};

bool preparePropertyNodes(Graph& graph, std::vector<Diagnostic>& diagnostics);

ActiveGraph collectActiveNodes(Graph& graph, const NodeRegistry& registry, const TargetSpec& target);

struct PropertyInfo {
    std::string name;
    std::string refName;
    ValueType type = ValueType::Float;
    Precision precision = Precision::Inherit;
    Value defaultValue;
    int samplerStage = -1;
    std::string samplerFilter = "linear";
    std::string samplerWrap = "repeat";
    std::string samplerAnisotropic = "none";
    bool isHDR = false;
    bool useTilingAndOffset = false;
    std::uint8_t defaultType = 0;
    Vec2 tiling{1.0f, 1.0f};
    Vec2 offset{0.0f, 0.0f};
    bool implicit = false;
    std::string declaration;
    std::string expression;
};

struct PropertySet {
    bool ok = false;
    std::vector<PropertyInfo> properties;
    std::vector<Diagnostic> diagnostics;
    const PropertyInfo* find(std::string_view refName) const;
};

PropertySet collectProperties(const Graph& graph, const ActiveGraph& active, const TargetSpec& target);

struct VariantInfo {
    std::uint64_t key = 0;
    std::vector<std::string> defines;
    std::vector<int> values;
};

struct KeywordSet {
    bool ok = false;
    std::vector<Keyword> keywords;
    std::vector<VariantInfo> variants;
    std::vector<int> bitOffsets;
    std::vector<Diagnostic> diagnostics;
};

KeywordSet collectKeywords(const Graph& graph, const TargetSpec& target, VariantPolicy policy,
                           int maxVariants);

struct EmittedGraph {
    bool ok = false;
    std::string source;
    std::string functionSource;
    std::vector<std::string> requirements;
    std::uint32_t requiredFeatureMask = 0;
    std::vector<Diagnostic> diagnostics;
};

bool hasVertexDisplacement(const Graph& graph, const NodeRegistry& registry, const ActiveGraph& active);

EmittedGraph emitSurface(const Graph& graph, const NodeRegistry& registry, const ActiveGraph& active,
                         const TargetSpec& target, const PropertySet& properties,
                         const KeywordSet& keywords, const VariantInfo& variant);

struct PackedBlob {
    std::string pass;
    std::uint16_t passId = 0;
    std::uint64_t variantKey = 0;
    Backend backend = Backend::Dxbc;
    int stage = 0;
    std::vector<std::uint8_t> data;
};

struct PackInput {
    const Graph* graph = nullptr;
    const TargetSpec* target = nullptr;
    std::vector<PropertyInfo> properties;
    std::vector<Keyword> keywords;
    std::vector<VariantInfo> variants;
    std::vector<PackedBlob> blobs;
    std::string hlsl;
    bool keepHlsl = false;
    std::uint32_t requiredFeatureMask = 0;
    std::uint64_t contentHash = 0;
    std::uint64_t sourceHash = 0;
};

bool packSgb(const PackInput& input, std::vector<std::uint8_t>& output, std::string& error);
bool validateSgb(const std::vector<std::uint8_t>& bytes, std::string& error);

struct ShadercOptions {
    std::filesystem::path root;
    std::filesystem::path shaderc;
    std::filesystem::path bgfxSource;
    std::filesystem::path cache;
    int timeoutSeconds = 600;
};

struct ShadercResult {
    bool ok = false;
    std::vector<std::uint8_t> data;
    std::string error;
    std::string compilerOutput;
    bool cached = false;
};

ShadercResult compileShader(const ShadercOptions& options, const std::string& sourcePath,
                            const std::string& outputPath, const std::string& stage,
                            Backend backend, const std::vector<std::string>& defines,
                            const std::string& hashKey);

struct FunctionRegistry {
    std::vector<std::string> names;
    std::map<std::string, std::string> sources;

    bool provide(const std::string& name, const std::string& code, std::string* error = nullptr);
    std::string render() const;
};

std::string typeName(ConcreteValueType type, ConcretePrecision precision);
std::string literal(const Value& value, ConcreteValueType target, ConcretePrecision precision);
std::string adapt(const std::string& expression, ConcreteValueType from, ConcreteValueType to,
                  ConcretePrecision precision);
std::string defineName(const std::string& refName);

} // namespace sg::detail
