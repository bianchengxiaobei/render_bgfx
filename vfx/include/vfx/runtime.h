#pragma once

#include "vfx/events.h"
#include "vfx/shading.h"
#include "vfx/types.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace vfx {

struct Asset;

struct EffectDesc {
    Mat4 transform = Mat4::identity();
    bool playOnAwake = true;
    float playRate = 1.0f;
    std::uint32_t randomSeed = 0;
    std::uint32_t instanceBatchLimit = 0;
    bool hasBoundsOverride = false;
    Bounds boundsOverride{};
};

class Effect {
  public:
    virtual ~Effect() = default;

    virtual void play() = 0;
    virtual void pause() = 0;
    virtual void stop() = 0;
    virtual void reset() = 0;
    virtual void advanceOneFrame() = 0;
    virtual void setTransform(const Mat4& world) = 0;
    virtual void setInstanceTransforms(std::span<const Mat4> worlds) = 0;
    virtual void setCulled(bool culled) = 0;
    virtual void setInstanceCulled(std::size_t index, bool culled) = 0;
    virtual void setPlayRate(float rate) = 0;
    virtual bool isAlive() const = 0;

    virtual bool setProperty(std::string_view name, const PropertyValue& value) = 0;
    virtual bool setFloat(std::string_view name, float value) = 0;
    virtual bool setVector2(std::string_view name, Vec2 value) = 0;
    virtual bool setVector3(std::string_view name, Vec3 value) = 0;
    virtual bool setVector4(std::string_view name, Vec4 value) = 0;
    virtual bool setColor(std::string_view name, Color value) = 0;
    virtual bool setTexture(std::string_view name, TextureHandle texture) = 0;
    virtual bool setCurve(std::string_view name, std::span<const CurveKey> curve) = 0;
    virtual bool setGradient(std::string_view name, std::span<const GradientKey> gradient) = 0;

    virtual void sendEvent(std::string_view name, std::span<const EventAttribute> attributes = {}) = 0;

    virtual Bounds bounds() const = 0;
    virtual Bounds visibleBounds() const = 0;
    virtual Bounds instanceBounds(std::size_t index) const = 0;
    virtual std::size_t instanceCount() const = 0;
    virtual bool supportsEffectInstancing() const = 0;
    virtual bool isCulled() const = 0;
    virtual bool isInstanceCulled(std::size_t index) const = 0;
    virtual std::uint32_t outputBatchCount() const = 0;
    virtual std::uint32_t outputBatchSize() const = 0;
};

struct Diagnostics {
    std::uint32_t effects = 0;
    std::uint32_t systems = 0;
    std::uint32_t particles = 0;
    std::uint32_t draws = 0;
    std::uint32_t dispatches = 0;
    std::uint32_t sortPasses = 0;
    std::uint32_t eventCount = 0;
    std::uint32_t shadowDraws = 0;
    float updateMs = 0.0f;
    float simulationMs = 0.0f;
    float outputMs = 0.0f;
    float encodeMs = 0.0f;
    float totalMs = 0.0f;
};

class Runtime {
  public:
    Runtime(Device& device, ShadingProvider* shading = nullptr);
    ~Runtime();

    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    std::shared_ptr<const Asset> loadAsset(const std::filesystem::path& path, std::string* error = nullptr);
    Effect* createEffect(const Asset& asset, const EffectDesc& desc, std::string* error = nullptr);
    Effect* reloadEffect(Effect* effect, const Asset& asset, const EffectDesc& desc,
                         std::string* error = nullptr);
    Effect* reloadEffect(Effect* effect, const std::filesystem::path& path, const EffectDesc& desc,
                         std::string* error = nullptr);
    void destroyEffect(Effect* effect);

    void update(float deltaSeconds);
    void encodeSimulation(CommandSink& sink, const FrameContext& frame);
    void beginOutputFrame();
    void encodeOutput(CommandSink& sink, const FrameContext& frame);
    void encodeOutputEffect(CommandSink& sink, const FrameContext& frame, Effect& effect);
    void encodeOutputEffectBatch(CommandSink& sink, const FrameContext& frame, Effect& effect,
                                 std::uint32_t batchIndex);
    void encodeMotionVectors(CommandSink& sink, const FrameContext& frame);
    void encodeShadow(CommandSink& sink, const Mat4& shadowViewProjection);

    void setErrorCallback(std::function<void(std::string_view)> callback);
    bool hasLitOutputs() const;
    bool hasShadowOutputs() const;
    Diagnostics diagnostics() const;

  private:
    friend class EffectImpl;
    void reportError(std::string_view message);
    void refreshDiagnostics();
    void pruneAssets();
    ProgramHandle motionVectorProgram();
    ProgramHandle shadowProgram();

    Device& m_device;
    ShadingProvider* m_shading = nullptr;
    std::vector<std::shared_ptr<const Asset>> m_assets;
    std::vector<std::unique_ptr<Effect>> m_effects;
    std::function<void(std::string_view)> m_errorCallback;
    std::string m_lastError;
    Diagnostics m_diagnostics{};
    float m_updateMs = 0.0f;
    float m_simulationMs = 0.0f;
    float m_outputMs = 0.0f;
    float m_encodeMs = 0.0f;
    ProgramHandle m_motionVectorProgram{};
    bool m_motionVectorProgramResolved = false;
    ProgramHandle m_shadowProgram{};
    bool m_shadowProgramResolved = false;
};

} // namespace vfx
