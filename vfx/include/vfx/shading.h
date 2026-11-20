#pragma once

#include "vfx/command_sink.h"
#include "vfx/device.h"
#include "vfx/types.h"

#include <cstdint>
#include <string_view>

namespace vfx {

struct FrameContext {
    Mat4 view = Mat4::identity();
    Mat4 projection = Mat4::identity();
    Mat4 viewProjection = Mat4::identity();
    Mat4 prevViewProjection = Mat4::identity();
    Vec3 cameraPosition{};
    Vec3 cameraForward{0.0f, 0.0f, 1.0f};
    Vec3 cameraRight{1.0f, 0.0f, 0.0f};
    Vec3 cameraUp{0.0f, 1.0f, 0.0f};
    Vec2 screenSize{1.0f, 1.0f};
    float time = 0.0f;
    float deltaTime = 0.0f;
    std::uint64_t frameIndex = 0;
    TextureHandle sceneDepth{};
    TextureHandle sceneColor{};
    void* lighting = nullptr;
};

struct LitSettings {
    float metallic = 0.0f;
    float roughness = 0.5f;
    float softDistance = 0.1f;
};

struct RenderOutputDesc {
    std::string_view name;
    bool lit = false;
};

class ShadingProvider {
  public:
    virtual ~ShadingProvider() = default;

    virtual void bindLighting(CommandSink& sink, const FrameContext& frame, const LitSettings& settings) = 0;
    virtual ProgramHandle resolveProgram(const RenderOutputDesc& output, ShaderBackend backend) = 0;
};

} // namespace vfx
