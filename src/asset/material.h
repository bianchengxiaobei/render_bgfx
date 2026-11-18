#pragma once

#include "core/math.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace hbg::asset {

constexpr int kRenderQueueOpaque = 2000;
constexpr int kRenderQueueAlphaTest = 2450;
constexpr int kRenderQueueTransparent = 3000;

enum class AlphaMode : uint8_t { Opaque, Mask, Blend };
enum class MaterialType : uint8_t { Lit, Unlit };

enum MaterialFeature : uint32_t {
    MaterialFeatureNone = 0,
    MaterialFeatureClearCoat = 1u << 0,
    MaterialFeatureTransmission = 1u << 1,
    MaterialFeatureIor = 1u << 2,
    MaterialFeatureIridescence = 1u << 3,
    MaterialFeatureAnisotropy = 1u << 4,
    MaterialFeatureEmissiveStrength = 1u << 5,
    MaterialFeatureSubsurface = 1u << 6,
};

struct TextureRef {
    int imageIndex = -1;
    int uvSet = 0;

    bool valid() const { return imageIndex >= 0; }
};

struct Material {
    std::string name;
    MaterialType type = MaterialType::Lit;
    AlphaMode alphaMode = AlphaMode::Opaque;
    bool doubleSided = false;
    uint32_t features = MaterialFeatureNone;
    int renderQueue = kRenderQueueOpaque;

    Vec4 baseColorFactor{1.0f, 1.0f, 1.0f, 1.0f};
    float metallic = 1.0f;
    float roughness = 1.0f;
    Vec3 emissiveFactor{0.0f, 0.0f, 0.0f};
    float emissiveStrength = 1.0f;
    float normalScale = 1.0f;
    float occlusionStrength = 1.0f;
    float alphaCutoff = 0.5f;

    TextureRef baseColorTexture;
    TextureRef metallicRoughnessTexture;
    TextureRef normalTexture;
    TextureRef occlusionTexture;
    TextureRef emissiveTexture;

    std::string diffusionProfile;
    bool maskMapPacked = true;

    float clearCoat = 0.0f;
    float clearCoatRoughness = 0.03f;
    float clearCoatNormalScale = 1.0f;
    float transmission = 0.0f;
    float thickness = 0.0f;
    float ior = 1.5f;
    float iridescence = 0.0f;
    float iridescenceIor = 1.3f;
    float iridescenceThickness = 0.0f;
    float anisotropy = 0.0f;
    float anisotropyRotation = 0.0f;
    float subsurfaceMask = 0.0f;
};

struct MaterialKey {
    uint32_t type = 0;
    uint32_t alphaMode = 0;
    uint32_t features = 0;
    bool doubleSided = false;
    int renderQueue = 0;
    Vec4 baseColorFactor;
    float metallic = 0.0f;
    float roughness = 0.0f;
    Vec3 emissiveFactor;
    float emissiveStrength = 0.0f;
    float normalScale = 0.0f;
    float occlusionStrength = 0.0f;
    float alphaCutoff = 0.0f;
    float clearCoat = 0.0f;
    float clearCoatRoughness = 0.0f;
    float clearCoatNormalScale = 0.0f;
    float transmission = 0.0f;
    float thickness = 0.0f;
    float ior = 0.0f;
    float iridescence = 0.0f;
    float iridescenceIor = 0.0f;
    float iridescenceThickness = 0.0f;
    float anisotropy = 0.0f;
    float anisotropyRotation = 0.0f;
    float subsurfaceMask = 0.0f;
    int baseColorImage = -1;
    int baseColorUv = 0;
    int metallicRoughnessImage = -1;
    int metallicRoughnessUv = 0;
    int normalImage = -1;
    int normalUv = 0;
    int occlusionImage = -1;
    int occlusionUv = 0;
    int emissiveImage = -1;
    int emissiveUv = 0;

    bool operator==(const MaterialKey&) const = default;
};

struct MaterialKeyHash {
    std::size_t operator()(const MaterialKey& key) const;
};

MaterialKey makeMaterialKey(const Material& material);

} // namespace hbg::asset
