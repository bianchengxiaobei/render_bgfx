#include "render/shadergraph/graph_program_cache.h"

#include "core/log.h"

#include <string>

namespace hbg::render::graph {

bool GraphProgramCache::init(gfx::ResourceManager& resources, Backend backend) {
    m_resources = &resources;
    m_backend = backend;
    m_programs.clear();
    m_warned.clear();
    return true;
}

void GraphProgramCache::shutdown(gfx::ResourceManager& resources) {
    for (auto& [key, program] : m_programs) {
        (void)key;
        if (program.valid()) {
            resources.destroyProgram(program);
        }
    }
    m_programs.clear();
    m_warned.clear();
    m_resources = nullptr;
}

void GraphProgramCache::release(uint64_t contentHash) {
    if (m_resources == nullptr) {
        return;
    }
    for (auto iterator = m_programs.begin(); iterator != m_programs.end();) {
        if (iterator->first.contentHash != contentHash) {
            ++iterator;
            continue;
        }
        if (iterator->second.valid()) {
            m_resources->destroyProgram(iterator->second);
        }
        iterator = m_programs.erase(iterator);
    }
}

gfx::ProgramHandle GraphProgramCache::program(const Asset& asset, PassKind pass, uint64_t variantKey) {
    if (m_resources == nullptr) {
        return {};
    }
    const PassDesc* passDesc = asset.findPass(pass);
    if (passDesc == nullptr) {
        return {};
    }

    const VariantDesc* variant = asset.findVariant(variantKey);
    uint64_t selectedKey = variantKey;
    if (variant == nullptr) {
        variant = asset.fallbackVariant(variantKey);
        if (variant == nullptr) {
            return {};
        }
        selectedKey = variant->key;
        const FallbackWarning warning{asset.contentHash(), static_cast<uint16_t>(passDesc->index)};
        if (m_warned.emplace(warning, true).second) {
            HBG_LOGW("graph program: variant 0x{:x} missing in '{}', using 0x{:x}",
                     variantKey, asset.path().filename().string(), selectedKey);
        }
    }

    const Key key{asset.contentHash(), selectedKey, static_cast<uint16_t>(passDesc->index),
                  static_cast<uint8_t>(m_backend)};
    const auto found = m_programs.find(key);
    if (found != m_programs.end()) {
        return found->second;
    }

    const ShaderBlob vertex = asset.blob(passDesc->index, selectedKey, m_backend, 0);
    const ShaderBlob fragment = asset.blob(passDesc->index, selectedKey, m_backend, 1);
    if (vertex.data == nullptr || fragment.data == nullptr) {
        HBG_LOGW("graph program: missing {} blobs for '{}' pass '{}' variant 0x{:x}",
                 m_backend == Backend::Dxbc ? "dxbc" : "spirv", asset.path().filename().string(),
                 passDesc->id, selectedKey);
        m_programs.emplace(key, gfx::ProgramHandle{});
        return {};
    }

    const std::string name = "graph:" + asset.path().filename().string() + ":" + passDesc->id + ":" +
                             std::to_string(selectedKey);
    const gfx::ProgramHandle program =
        m_resources->createProgram(vertex.data, vertex.size, fragment.data, fragment.size, name);
    if (!program.valid()) {
        HBG_LOGE("graph program: failed to create '{}'", name);
    }
    m_programs.emplace(key, program);
    return program;
}

} // namespace hbg::render::graph
