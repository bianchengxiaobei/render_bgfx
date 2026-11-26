#pragma once

#include "asset/material.h"
#include "asset/texture_loader.h"
#include "gfx/encoder.h"
#include "gfx/resource_manager.h"
#include "gfx/texture.h"
#include "render/scene/render_scene.h"
#include "render/shadergraph/sgb_asset.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace hbg::render::graph {

class GraphUniformCache {
  public:
    bool init(gfx::ResourceManager& resources);
    void shutdown(gfx::ResourceManager& resources);
    gfx::UniformHandle uniform(std::string_view name, gfx::UniformType type);

  private:
    struct Entry {
        gfx::UniformHandle handle;
        gfx::UniformType type = gfx::UniformType::Vec4;
    };

    gfx::ResourceManager* m_resources = nullptr;
    std::unordered_map<std::string, Entry> m_uniforms;
};

uint64_t graphVariantKey(const Asset& asset, const asset::Material& material);
Vec4 graphTextureTransform(const PropertyDesc& property, const asset::Material& material);

struct GraphBindingFallbacks {
    gfx::TextureHandle white;
    gfx::TextureHandle black;
    gfx::TextureHandle textureBlack;
    gfx::TextureHandle grey;
    gfx::TextureHandle normal;
    gfx::TextureHandle linearGrey;
    gfx::TextureHandle red;
    gfx::TextureHandle whiteArray;
    gfx::TextureHandle white3D;
    gfx::TextureHandle whiteCube;
};

class GraphBinding {
  public:
    bool init(gfx::ResourceManager& resources, GraphUniformCache& uniforms, const Asset& asset,
              const asset::Material& material, const RenderScene& scene, const GraphBindingFallbacks& fallbacks,
              std::string* error = nullptr);
    void destroy(gfx::ResourceManager& resources);
    void bind(gfx::Encoder& encoder) const;
    void setGlobalMipBias(float bias) { m_globalMipBias = bias; }

    bool valid() const { return m_valid; }
    uint64_t variantKey() const { return m_variantKey; }
    bool variantMissing() const { return m_variantMissing; }

  private:
    struct ValueBinding {
        PropertyKind kind = PropertyKind::Other;
        gfx::UniformHandle uniform;
        Vec4 vector{};
        Mat4 matrix = Mat4::identity();
    };

    struct TextureBinding {
        gfx::UniformHandle sampler;
        gfx::UniformHandle texelSize;
        gfx::UniformHandle scaleTranslate;
        gfx::UniformHandle hdrDecode;
        gfx::TextureHandle texture;
        uint8_t stage = 0;
        uint32_t samplerFlags = 0;
        Vec4 texelSizeValue{1.0f, 1.0f, 1.0f, 1.0f};
        Vec4 scaleTranslateValue{1.0f, 1.0f, 0.0f, 0.0f};
        Vec4 hdrDecodeValue{1.0f, 1.0f, 0.0f, 0.0f};
    };

    gfx::ResourceManager* m_resources = nullptr;
    gfx::UniformHandle m_globalMipBiasUniform;
    float m_globalMipBias = 0.0f;
    std::vector<ValueBinding> m_values;
    std::vector<TextureBinding> m_textures;
    std::vector<gfx::TextureHandle> m_ownedTextures;
    uint64_t m_variantKey = 0;
    bool m_variantMissing = false;
    bool m_valid = false;
};

} // namespace hbg::render::graph
