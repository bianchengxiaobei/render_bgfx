#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace hbg::render::graph {

constexpr uint32_t kGraphFeatureSceneColor = 1u << 0;
constexpr uint32_t kGraphFeatureSceneDepth = 1u << 1;
constexpr uint32_t kGraphFeatureFog = 1u << 2;
constexpr uint32_t kSupportedGraphFeatures =
    kGraphFeatureSceneColor | kGraphFeatureSceneDepth | kGraphFeatureFog;

enum class Backend : uint8_t {
    Dxbc = 0,
    Spirv = 1,
};

enum class PassKind : uint8_t {
    Forward,
    GBuffer,
    Depth,
    Shadow,
    Motion,
    Unknown,
};

enum class PropertyKind : uint8_t {
    Float,
    Vector2,
    Vector3,
    Vector4,
    Color,
    Boolean,
    Integer,
    Matrix2,
    Matrix3,
    Matrix4,
    Texture2D,
    Texture2DArray,
    Texture3D,
    Cubemap,
    Gradient,
    SamplerState,
    Other,
};

PropertyKind propertyKindFromPacked(uint8_t value);
bool isTextureKind(PropertyKind kind);
bool isMatrixKind(PropertyKind kind);

enum class TextureDefaultType : uint8_t {
    White = 0,
    Black = 1,
    Grey = 2,
    Normal = 3,
    LinearGrey = 4,
    Red = 5,
};

struct StageBinding {
    std::string name;
    uint8_t stage = 0;
};

struct PropertyDesc {
    std::string name;
    std::string refName;
    PropertyKind kind = PropertyKind::Other;
    uint8_t precision = 0;
    int samplerStage = -1;
    uint8_t samplerFilter = 0;
    uint8_t samplerWrap = 0;
    uint8_t samplerAnisotropy = 0;
    bool isHDR = false;
    bool useTilingAndOffset = false;
    TextureDefaultType defaultType = TextureDefaultType::White;
    std::array<float, 2> tiling{1.0f, 1.0f};
    std::array<float, 2> offset{0.0f, 0.0f};
    std::array<float, 4> vector{};
    std::array<float, 16> matrix{};
};

struct KeywordEntryDesc {
    uint32_t id = 0;
    std::string displayName;
    std::string referenceName;
};

struct KeywordDesc {
    std::string name;
    std::string refName;
    uint8_t type = 0;
    uint8_t definition = 0;
    uint8_t scope = 0;
    uint32_t defaultValue = 0;
    std::vector<KeywordEntryDesc> entries;
};

struct PassDesc {
    uint32_t index = 0;
    std::string id;
    PassKind kind = PassKind::Unknown;
    std::vector<StageBinding> stages;
};

struct VariantDesc {
    uint64_t key = 0;
    std::vector<uint16_t> groups;
};

struct ShaderBlob {
    const uint8_t* data = nullptr;
    uint32_t size = 0;
    Backend backend = Backend::Dxbc;
    uint8_t stage = 0;
};

class Asset {
  public:
    bool load(const std::filesystem::path& path, std::string* error = nullptr);
    bool loadFromMemory(const std::vector<uint8_t>& bytes, std::string* error = nullptr);

    const std::filesystem::path& path() const { return m_path; }
    uint64_t contentHash() const { return m_contentHash; }
    uint64_t sourceHash() const { return m_sourceHash; }
    const std::string& target() const { return m_target; }
    int stageBudget() const { return m_stageBudget; }
    uint32_t requiredFeatureMask() const { return static_cast<uint32_t>(m_requiredFeatureMask); }
    const std::vector<PropertyDesc>& properties() const { return m_properties; }
    const std::vector<KeywordDesc>& keywords() const { return m_keywords; }
    const std::vector<PassDesc>& passes() const { return m_passes; }
    const std::vector<VariantDesc>& variants() const { return m_variants; }
    std::filesystem::file_time_type lastWriteTime() const { return m_lastWriteTime; }

    const PropertyDesc* findProperty(std::string_view refName) const;
    const KeywordDesc* findKeyword(std::string_view refName) const;
    const PassDesc* findPass(PassKind kind) const;
    const VariantDesc* findVariant(uint64_t key) const;
    const VariantDesc* fallbackVariant(uint64_t key) const;
    ShaderBlob blob(uint32_t passIndex, uint64_t variantKey, Backend backend, uint8_t stage) const;

  private:
    void reset();
    bool parseBytes(std::string* error);

    struct BlobDesc {
        uint8_t stage = 0;
        Backend backend = Backend::Dxbc;
        uint32_t offset = 0;
        uint32_t size = 0;
    };

    struct Group {
        std::vector<BlobDesc> blobs;
    };

    std::vector<uint8_t> m_bytes;
    std::filesystem::path m_path;
    std::filesystem::file_time_type m_lastWriteTime{};
    uint64_t m_contentHash = 0;
    uint64_t m_sourceHash = 0;
    uint64_t m_requiredFeatureMask = 0;
    std::string m_target;
    int m_stageBudget = 5;
    std::vector<PropertyDesc> m_properties;
    std::vector<KeywordDesc> m_keywords;
    std::vector<PassDesc> m_passes;
    std::vector<VariantDesc> m_variants;
    std::vector<Group> m_groups;
};

bool validateStageContract(const Asset& asset, std::string* error = nullptr);

} // namespace hbg::render::graph
