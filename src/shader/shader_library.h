#pragma once

#include "gfx/resource_manager.h"
#include "shader/shader_key.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace hbg::shader {

class ShaderLibrary {
  public:
    ShaderLibrary() = default;
    ~ShaderLibrary() = default;

    ShaderLibrary(const ShaderLibrary&) = delete;
    ShaderLibrary& operator=(const ShaderLibrary&) = delete;

    bool init(gfx::ResourceManager& resources, const std::filesystem::path& shaderRoot);
    void shutdown(gfx::ResourceManager& resources);
    bool valid() const { return m_valid; }

    gfx::ProgramHandle program(PassId pass, uint64_t materialKey);
    gfx::ProgramHandle program(const ProgramKey& key) { return program(key.pass, key.materialKey); }
    std::string_view backendName() const { return m_backend; }

  private:
    struct PassFiles {
        std::string vertex;
        std::vector<std::string> fragments;
    };

    struct BackendFiles {
        PassFiles forward;
        PassFiles forwardInstanced;
        PassFiles tonemap;
        PassFiles shadow;
        PassFiles shadowInstanced;
        PassFiles prepass;
        PassFiles motionVectors;
        PassFiles cameraMotionVectors;
        PassFiles depthPyramidCopy;
        PassFiles depthPyramidDownsample;
        PassFiles sky;
        PassFiles debugLightData;
        PassFiles debugNormal;
        PassFiles debugMotionVectors;
        PassFiles debugDepth;
        PassFiles debugDepthPyramid;
        PassFiles debugLightCluster;
        PassFiles debugShadowAtlas;
        PassFiles debugColor;
        PassFiles debugAlbedo;
        PassFiles debugRoughness;
        PassFiles wireframe;
        PassFiles ui;
    };

    bool loadManifest(const std::filesystem::path& manifestPath);
    gfx::ProgramHandle createProgram(const PassFiles& files, uint32_t variant);

    gfx::ResourceManager* m_resources = nullptr;
    std::filesystem::path m_root;
    std::string m_backend;
    BackendFiles m_files;
    bool m_valid = false;
    std::unordered_map<uint64_t, gfx::ProgramHandle> m_programs;
};

} // namespace hbg::shader
