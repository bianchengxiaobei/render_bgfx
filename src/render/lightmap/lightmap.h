#pragma once

#include "gfx/resource_manager.h"
#include "gfx/texture.h"
#include "render/scene/render_scene.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace hbg::render::lightmap {

inline constexpr uint32_t kShChannelCount = 3u;

struct AtlasImage {
    uint32_t size = 0;
    std::array<std::vector<uint16_t>, kShChannelCount> sh;
};

struct LightmapAtlas {
    uint32_t layerBase = 0;
    uint32_t size = 0;
};

bool loadAtlasImage(const std::filesystem::path& directory, const LightmapAtlasSource& source, AtlasImage& image,
                    std::string* error = nullptr);

class LightmapSet {
  public:
    bool load(gfx::ResourceManager& resources, const RenderScene& scene, std::string* error = nullptr);
    void destroy(gfx::ResourceManager& resources);

    bool valid() const { return m_shArray.valid() && !m_atlases.empty(); }
    gfx::TextureHandle shArray() const { return m_shArray; }
    const std::vector<LightmapAtlas>& atlases() const { return m_atlases; }
    uint32_t atlasCount() const { return static_cast<uint32_t>(m_atlases.size()); }
    uint32_t layerCount() const { return m_layerCount; }

  private:
    gfx::TextureHandle m_shArray;
    std::vector<LightmapAtlas> m_atlases;
    uint32_t m_layerCount = 0;
};

} // namespace hbg::render::lightmap
