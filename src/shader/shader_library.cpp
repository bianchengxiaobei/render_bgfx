#include "shader/shader_library.h"

#include "core/log.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <string_view>
#include <utility>

namespace hbg::shader {
namespace {

std::string backendKey(bgfx::RendererType::Enum type) {
    switch (type) {
    case bgfx::RendererType::Direct3D11:
    case bgfx::RendererType::Direct3D12:
        return "dxbc";
    case bgfx::RendererType::Vulkan:
        return "spirv";
    default:
        return {};
    }
}

uint64_t cacheKey(PassId pass, uint32_t variant) {
    return (static_cast<uint64_t>(pass) << 32) | variant;
}

} // namespace

bool ShaderLibrary::init(gfx::ResourceManager& resources, const std::filesystem::path& shaderRoot) {
    m_resources = &resources;
    m_root = shaderRoot;
    m_backend = backendKey(bgfx::getRendererType());
    if (m_backend.empty()) {
        HBG_LOGE("shader library: unsupported renderer type {}", static_cast<int>(bgfx::getRendererType()));
        return false;
    }

    const std::filesystem::path manifestPath = m_root / "manifest.json";
    if (!loadManifest(manifestPath) || !m_valid) {
        HBG_LOGE("shader library: failed to load manifest '{}'", manifestPath.string());
        return false;
    }

    HBG_LOGI("shader library: backend={} root={}", m_backend, m_root.string());
    return true;
}

bool ShaderLibrary::loadManifest(const std::filesystem::path& manifestPath) {
    std::ifstream file(manifestPath);
    if (!file.good()) {
        HBG_LOGE("shader manifest not found: {}", manifestPath.string());
        return false;
    }

    nlohmann::json root;
    try {
        file >> root;
    } catch (const std::exception& exception) {
        HBG_LOGE("shader manifest parse failed: {}", exception.what());
        return false;
    }

    const auto passes = root.find("passes");
    if (passes == root.end() || !passes->is_object()) {
        HBG_LOGE("shader manifest has no 'passes' object");
        return false;
    }
    const auto backend = passes->find(m_backend);
    if (backend == passes->end() || !backend->is_object()) {
        HBG_LOGE("shader manifest has no '{}' backend", m_backend);
        return false;
    }

    const auto readFiles = [&](const nlohmann::json& object, bool hasFragments) {
        PassFiles files;
        const auto vertex = object.find("vertex");
        if (vertex == object.end() || !vertex->is_string()) {
            return files;
        }
        files.vertex = vertex->get<std::string>();

        if (hasFragments) {
            const auto fragments = object.find("fragments");
            if (fragments == object.end() || !fragments->is_array()) {
                return files;
            }
            for (const auto& fragment : *fragments) {
                if (fragment.is_string()) {
                    files.fragments.push_back(fragment.get<std::string>());
                }
            }
        } else {
            const auto fragment = object.find("fragment");
            if (fragment == object.end() || !fragment->is_string()) {
                return files;
            }
            files.fragments.push_back(fragment->get<std::string>());
        }
        return files;
    };

    const auto forward = backend->find("forward");
    const auto forwardInstanced = backend->find("forwardInstanced");
    const auto tonemap = backend->find("tonemap");
    const auto shadow = backend->find("shadow");
    const auto shadowInstanced = backend->find("shadowInstanced");
    const auto prepass = backend->find("prepass");
    const auto motionVectors = backend->find("motionVectors");
    const auto cameraMotionVectors = backend->find("cameraMotionVectors");
    const auto depthPyramidCopy = backend->find("depthPyramidCopy");
    const auto depthPyramidDownsample = backend->find("depthPyramidDownsample");
    const auto sky = backend->find("sky");
    const auto debugLightData = backend->find("debugLightData");
    const auto debugNormal = backend->find("debugNormal");
    const auto debugMotionVectors = backend->find("debugMotionVectors");
    const auto debugDepth = backend->find("debugDepth");
    const auto debugDepthPyramid = backend->find("debugDepthPyramid");
    const auto debugLightCluster = backend->find("debugLightCluster");
    const auto debugShadowAtlas = backend->find("debugShadowAtlas");
    const auto debugColor = backend->find("debugColor");
    const auto debugAlbedo = backend->find("debugAlbedo");
    const auto debugRoughness = backend->find("debugRoughness");
    const auto wireframe = backend->find("wireframe");
    const auto ui = backend->find("ui");
    if (forward == backend->end() || forwardInstanced == backend->end() || tonemap == backend->end() ||
        shadow == backend->end() || shadowInstanced == backend->end() || prepass == backend->end() ||
        motionVectors == backend->end() || cameraMotionVectors == backend->end() ||
        depthPyramidCopy == backend->end() || depthPyramidDownsample == backend->end() || sky == backend->end() ||
        debugLightData == backend->end() || debugNormal == backend->end() || debugMotionVectors == backend->end() ||
        debugDepth == backend->end() || debugDepthPyramid == backend->end() || debugLightCluster == backend->end() ||
        debugShadowAtlas == backend->end() || debugColor == backend->end() || debugAlbedo == backend->end() ||
        debugRoughness == backend->end() || wireframe == backend->end() || ui == backend->end()) {
        HBG_LOGE("shader manifest backend '{}' is incomplete", m_backend);
        return false;
    }

    m_files.forward = readFiles(*forward, true);
    m_files.forwardInstanced = readFiles(*forwardInstanced, true);
    m_files.tonemap = readFiles(*tonemap, false);
    m_files.shadow = readFiles(*shadow, false);
    m_files.shadowInstanced = readFiles(*shadowInstanced, false);
    m_files.prepass = readFiles(*prepass, true);
    m_files.motionVectors = readFiles(*motionVectors, false);
    m_files.cameraMotionVectors = readFiles(*cameraMotionVectors, false);
    m_files.depthPyramidCopy = readFiles(*depthPyramidCopy, false);
    m_files.depthPyramidDownsample = readFiles(*depthPyramidDownsample, false);
    m_files.sky = readFiles(*sky, false);
    m_files.debugLightData = readFiles(*debugLightData, false);
    m_files.debugNormal = readFiles(*debugNormal, false);
    m_files.debugMotionVectors = readFiles(*debugMotionVectors, false);
    m_files.debugDepth = readFiles(*debugDepth, false);
    m_files.debugDepthPyramid = readFiles(*debugDepthPyramid, false);
    m_files.debugLightCluster = readFiles(*debugLightCluster, false);
    m_files.debugShadowAtlas = readFiles(*debugShadowAtlas, false);
    m_files.debugColor = readFiles(*debugColor, false);
    m_files.debugAlbedo = readFiles(*debugAlbedo, true);
    m_files.debugRoughness = readFiles(*debugRoughness, true);
    m_files.wireframe = readFiles(*wireframe, false);
    m_files.ui = readFiles(*ui, false);
    m_valid = !m_files.forward.vertex.empty() && !m_files.forward.fragments.empty() &&
              !m_files.forwardInstanced.vertex.empty() && !m_files.forwardInstanced.fragments.empty() &&
              !m_files.tonemap.vertex.empty() && !m_files.tonemap.fragments.empty() && !m_files.shadow.vertex.empty() &&
              !m_files.shadow.fragments.empty() && !m_files.shadowInstanced.vertex.empty() &&
              !m_files.shadowInstanced.fragments.empty() && !m_files.prepass.vertex.empty() &&
              !m_files.prepass.fragments.empty() && !m_files.motionVectors.vertex.empty() &&
              !m_files.motionVectors.fragments.empty() && !m_files.cameraMotionVectors.vertex.empty() &&
              !m_files.cameraMotionVectors.fragments.empty() && !m_files.depthPyramidCopy.vertex.empty() &&
              !m_files.depthPyramidCopy.fragments.empty() && !m_files.depthPyramidDownsample.vertex.empty() &&
              !m_files.depthPyramidDownsample.fragments.empty() && !m_files.sky.vertex.empty() &&
              !m_files.sky.fragments.empty() && !m_files.debugLightData.vertex.empty() &&
              !m_files.debugLightData.fragments.empty() && !m_files.debugNormal.vertex.empty() &&
              !m_files.debugNormal.fragments.empty() && !m_files.debugMotionVectors.vertex.empty() &&
              !m_files.debugMotionVectors.fragments.empty() && !m_files.debugDepth.vertex.empty() &&
              !m_files.debugDepth.fragments.empty() && !m_files.debugDepthPyramid.vertex.empty() &&
              !m_files.debugDepthPyramid.fragments.empty() && !m_files.debugLightCluster.vertex.empty() &&
              !m_files.debugLightCluster.fragments.empty() && !m_files.debugShadowAtlas.vertex.empty() &&
              !m_files.debugShadowAtlas.fragments.empty() && !m_files.debugColor.vertex.empty() &&
              !m_files.debugColor.fragments.empty() && !m_files.debugAlbedo.vertex.empty() &&
              !m_files.debugAlbedo.fragments.empty() && !m_files.debugRoughness.vertex.empty() &&
              !m_files.debugRoughness.fragments.empty() && !m_files.wireframe.vertex.empty() &&
              !m_files.wireframe.fragments.empty() && !m_files.ui.vertex.empty() && !m_files.ui.fragments.empty();
    if (!m_valid) {
        HBG_LOGE("shader manifest backend '{}' has invalid pass entries", m_backend);
    }
    return m_valid;
}

gfx::ProgramHandle ShaderLibrary::createProgram(const PassFiles& files, uint32_t variant) {
    if (m_resources == nullptr) {
        return {};
    }
    if (variant >= files.fragments.size()) {
        HBG_LOGW("shader variant {} is not compiled; falling back to variant 0", variant);
        variant = 0;
    }
    if (variant >= files.fragments.size()) {
        return {};
    }

    const std::filesystem::path vertexPath = m_root / files.vertex;
    const std::filesystem::path fragmentPath = m_root / files.fragments[variant];
    const std::string name = fragmentPath.stem().string();
    gfx::ProgramHandle program = m_resources->createProgramFromFiles(vertexPath.string(), fragmentPath.string(), name);
    if (!program.valid()) {
        HBG_LOGE("failed to create shader program '{}' (vertex='{}')", name, vertexPath.string());
    }
    return program;
}

gfx::ProgramHandle ShaderLibrary::program(PassId pass, uint64_t materialKey) {
    if (!m_valid) {
        return {};
    }

    uint32_t variant = static_cast<uint32_t>(materialKey) & kMaterialVariantMask;
    const PassFiles* files = &m_files.forward;
    switch (pass) {
    case PassId::Forward:
        break;
    case PassId::ForwardInstanced:
        files = &m_files.forwardInstanced;
        break;
    case PassId::Tonemap:
        files = &m_files.tonemap;
        variant = 0;
        break;
    case PassId::Shadow:
        files = &m_files.shadow;
        variant = 0;
        break;
    case PassId::ShadowInstanced:
        files = &m_files.shadowInstanced;
        variant = 0;
        break;
    case PassId::Prepass:
        files = &m_files.prepass;
        break;
    case PassId::MotionVectors:
        files = &m_files.motionVectors;
        variant = 0;
        break;
    case PassId::CameraMotionVectors:
        files = &m_files.cameraMotionVectors;
        variant = 0;
        break;
    case PassId::DepthPyramidCopy:
        files = &m_files.depthPyramidCopy;
        variant = 0;
        break;
    case PassId::DepthPyramidDownsample:
        files = &m_files.depthPyramidDownsample;
        variant = 0;
        break;
    case PassId::Sky:
        files = &m_files.sky;
        variant = 0;
        break;
    case PassId::DebugLightData:
        files = &m_files.debugLightData;
        variant = 0;
        break;
    case PassId::DebugNormal:
        files = &m_files.debugNormal;
        variant = 0;
        break;
    case PassId::DebugMotionVectors:
        files = &m_files.debugMotionVectors;
        variant = 0;
        break;
    case PassId::DebugDepth:
        files = &m_files.debugDepth;
        variant = 0;
        break;
    case PassId::DebugDepthPyramid:
        files = &m_files.debugDepthPyramid;
        variant = 0;
        break;
    case PassId::DebugLightCluster:
        files = &m_files.debugLightCluster;
        variant = 0;
        break;
    case PassId::DebugShadowAtlas:
        files = &m_files.debugShadowAtlas;
        variant = 0;
        break;
    case PassId::DebugColor:
        files = &m_files.debugColor;
        variant = 0;
        break;
    case PassId::DebugAlbedo:
        files = &m_files.debugAlbedo;
        break;
    case PassId::DebugRoughness:
        files = &m_files.debugRoughness;
        break;
    case PassId::Wireframe:
        files = &m_files.wireframe;
        variant = 0;
        break;
    case PassId::Ui:
        files = &m_files.ui;
        variant = 0;
        break;
    }

    const uint64_t key = cacheKey(pass, variant);
    const auto cached = m_programs.find(key);
    if (cached != m_programs.end()) {
        return cached->second;
    }

    gfx::ProgramHandle program = createProgram(*files, variant);
    if (!program.valid() && variant != 0) {
        HBG_LOGW("falling back to shader variant 0 for pass {}", static_cast<int>(pass));
        program = createProgram(*files, 0);
    }
    if (program.valid()) {
        m_programs.emplace(key, program);
    }
    return program;
}

void ShaderLibrary::shutdown(gfx::ResourceManager& resources) {
    for (const auto& [key, program] : m_programs) {
        (void)key;
        if (program.valid()) {
            resources.destroyProgram(program);
        }
    }
    m_programs.clear();
    m_valid = false;
    m_resources = nullptr;
}

} // namespace hbg::shader
