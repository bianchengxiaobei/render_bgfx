#pragma once

#include "app/sg_preview.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace hbg {

namespace gfx {
class ResourceManager;
}

class SgGraphPreviewRenderer {
  public:
    SgGraphPreviewRenderer();
    ~SgGraphPreviewRenderer();

    SgGraphPreviewRenderer(const SgGraphPreviewRenderer&) = delete;
    SgGraphPreviewRenderer& operator=(const SgGraphPreviewRenderer&) = delete;

    bool init(gfx::ResourceManager& resources, std::string* error = nullptr);
    void shutdown(gfx::ResourceManager& resources);
    bool render(const std::vector<std::uint8_t>& sgb, const SgPreviewOptions& options,
                std::vector<std::uint8_t>& jpeg, std::string& error);

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace hbg
