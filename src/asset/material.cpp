#include "asset/material.h"

#include <cstdint>

namespace hbg::asset {
namespace {

void hashCombine(std::size_t& seed, std::size_t value) {
    seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
}

void hashCombine(std::size_t& seed, float value) { hashCombine(seed, std::hash<float>{}(value)); }

void hashCombine(std::size_t& seed, int value) { hashCombine(seed, std::hash<int>{}(value)); }

void hashCombine(std::size_t& seed, uint32_t value) { hashCombine(seed, std::hash<uint32_t>{}(value)); }

void hashCombine(std::size_t& seed, bool value) { hashCombine(seed, std::hash<bool>{}(value)); }

void hashCombine(std::size_t& seed, const Vec3& value) {
    hashCombine(seed, value.x);
    hashCombine(seed, value.y);
    hashCombine(seed, value.z);
}

void hashCombine(std::size_t& seed, const Vec4& value) {
    hashCombine(seed, value.x);
    hashCombine(seed, value.y);
    hashCombine(seed, value.z);
    hashCombine(seed, value.w);
}

} // namespace

MaterialKey makeMaterialKey(const Material& material) {
    MaterialKey key;
    key.type = static_cast<uint32_t>(material.type);
    key.alphaMode = static_cast<uint32_t>(material.alphaMode);
    key.features = material.features;
    key.doubleSided = material.doubleSided;
    key.transparentBackface = material.transparentBackface;
    key.renderQueue = material.renderQueue;
    key.baseColorFactor = material.baseColorFactor;
    key.metallic = material.metallic;
    key.roughness = material.roughness;
    key.emissiveFactor = material.emissiveFactor;
    key.emissiveStrength = material.emissiveStrength;
    key.normalScale = material.normalScale;
    key.occlusionStrength = material.occlusionStrength;
    key.alphaCutoff = material.alphaCutoff;
    key.clearCoat = material.clearCoat;
    key.clearCoatRoughness = material.clearCoatRoughness;
    key.clearCoatNormalScale = material.clearCoatNormalScale;
    key.transmission = material.transmission;
    key.thickness = material.thickness;
    key.ior = material.ior;
    key.iridescence = material.iridescence;
    key.iridescenceIor = material.iridescenceIor;
    key.iridescenceThickness = material.iridescenceThickness;
    key.anisotropy = material.anisotropy;
    key.anisotropyRotation = material.anisotropyRotation;
    key.subsurfaceMask = material.subsurfaceMask;
    key.baseColorImage = material.baseColorTexture.imageIndex;
    key.baseColorUv = material.baseColorTexture.uvSet;
    key.metallicRoughnessImage = material.metallicRoughnessTexture.imageIndex;
    key.metallicRoughnessUv = material.metallicRoughnessTexture.uvSet;
    key.normalImage = material.normalTexture.imageIndex;
    key.normalUv = material.normalTexture.uvSet;
    key.occlusionImage = material.occlusionTexture.imageIndex;
    key.occlusionUv = material.occlusionTexture.uvSet;
    key.emissiveImage = material.emissiveTexture.imageIndex;
    key.emissiveUv = material.emissiveTexture.uvSet;
    key.baseColorSampler = samplerStateKey(material.baseColorTexture.sampler);
    key.metallicRoughnessSampler = samplerStateKey(material.metallicRoughnessTexture.sampler);
    key.normalSampler = samplerStateKey(material.normalTexture.sampler);
    key.occlusionSampler = samplerStateKey(material.occlusionTexture.sampler);
    key.emissiveSampler = samplerStateKey(material.emissiveTexture.sampler);
    return key;
}

std::size_t MaterialKeyHash::operator()(const MaterialKey& key) const {
    std::size_t seed = 0;
    hashCombine(seed, key.type);
    hashCombine(seed, key.alphaMode);
    hashCombine(seed, key.features);
    hashCombine(seed, key.doubleSided);
    hashCombine(seed, key.transparentBackface);
    hashCombine(seed, key.renderQueue);
    hashCombine(seed, key.baseColorFactor);
    hashCombine(seed, key.metallic);
    hashCombine(seed, key.roughness);
    hashCombine(seed, key.emissiveFactor);
    hashCombine(seed, key.emissiveStrength);
    hashCombine(seed, key.normalScale);
    hashCombine(seed, key.occlusionStrength);
    hashCombine(seed, key.alphaCutoff);
    hashCombine(seed, key.clearCoat);
    hashCombine(seed, key.clearCoatRoughness);
    hashCombine(seed, key.clearCoatNormalScale);
    hashCombine(seed, key.transmission);
    hashCombine(seed, key.thickness);
    hashCombine(seed, key.ior);
    hashCombine(seed, key.iridescence);
    hashCombine(seed, key.iridescenceIor);
    hashCombine(seed, key.iridescenceThickness);
    hashCombine(seed, key.anisotropy);
    hashCombine(seed, key.anisotropyRotation);
    hashCombine(seed, key.subsurfaceMask);
    hashCombine(seed, key.baseColorImage);
    hashCombine(seed, key.baseColorUv);
    hashCombine(seed, key.metallicRoughnessImage);
    hashCombine(seed, key.metallicRoughnessUv);
    hashCombine(seed, key.normalImage);
    hashCombine(seed, key.normalUv);
    hashCombine(seed, key.occlusionImage);
    hashCombine(seed, key.occlusionUv);
    hashCombine(seed, key.emissiveImage);
    hashCombine(seed, key.emissiveUv);
    hashCombine(seed, key.baseColorSampler);
    hashCombine(seed, key.metallicRoughnessSampler);
    hashCombine(seed, key.normalSampler);
    hashCombine(seed, key.occlusionSampler);
    hashCombine(seed, key.emissiveSampler);
    return seed;
}

} // namespace hbg::asset
