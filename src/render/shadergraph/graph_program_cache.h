#pragma once

#include "gfx/resource_manager.h"
#include "render/shadergraph/sgb_asset.h"

#include <cstdint>
#include <unordered_map>

namespace hbg::render::graph {

class GraphProgramCache {
  public:
    bool init(gfx::ResourceManager& resources, Backend backend);
    void shutdown(gfx::ResourceManager& resources);
    void release(uint64_t contentHash);

    gfx::ProgramHandle program(const Asset& asset, PassKind pass, uint64_t variantKey);
    Backend backend() const { return m_backend; }

  private:
    struct Key {
        uint64_t contentHash = 0;
        uint64_t variantKey = 0;
        uint16_t passIndex = 0;
        uint8_t backend = 0;

        bool operator==(const Key&) const = default;
    };

    struct KeyHash {
        std::size_t operator()(const Key& key) const noexcept {
            uint64_t value = key.contentHash;
            value ^= key.variantKey + 0x9e3779b97f4a7c15ull + (value << 6) + (value >> 2);
            value ^= static_cast<uint64_t>(key.passIndex) << 32u;
            value ^= key.backend;
            return std::hash<uint64_t>{}(value);
        }
    };

    struct FallbackWarning {
        uint64_t contentHash = 0;
        uint16_t passIndex = 0;

        bool operator==(const FallbackWarning&) const = default;
    };

    struct FallbackWarningHash {
        std::size_t operator()(const FallbackWarning& warning) const noexcept {
            const uint64_t value = warning.contentHash ^ (static_cast<uint64_t>(warning.passIndex) << 48u);
            return std::hash<uint64_t>{}(value);
        }
    };

    gfx::ResourceManager* m_resources = nullptr;
    Backend m_backend = Backend::Dxbc;
    std::unordered_map<Key, gfx::ProgramHandle, KeyHash> m_programs;
    std::unordered_map<FallbackWarning, bool, FallbackWarningHash> m_warned;
};

} // namespace hbg::render::graph
