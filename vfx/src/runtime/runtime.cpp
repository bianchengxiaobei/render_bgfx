#include "vfx/runtime.h"

#include "runtime_asset.h"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vfx {
namespace {

constexpr float kMaxDeltaSeconds = 0.1f;
constexpr float kStepDeltaSeconds = 1.0f / 60.0f;
constexpr double kSpawnerBoundaryEpsilon = 1.0e-6;
constexpr std::uint32_t kLutSampleCount = 256u;
constexpr std::uint32_t kLutRowPitch = kLutSampleCount * 8u;

constexpr std::uint32_t kAttributesBuffer = 0;
constexpr std::uint32_t kDeadListBuffer = 1;
constexpr std::uint32_t kIndirectionBuffer = 2;
constexpr std::uint32_t kRenderCountBuffer = 3;
constexpr std::uint32_t kSpawnCountBuffer = 4;
constexpr std::uint32_t kIndirectBuffer = 5;
constexpr std::uint32_t kBoundsBuffer = 6;
constexpr std::uint32_t kRenderKVPBuffer = 7;
constexpr std::uint32_t kSortScratchBuffer = 8;
constexpr std::uint32_t kBufferKindCount = 9;

constexpr std::uint8_t kTaskSpawner = 0;
constexpr std::uint8_t kTaskDeadListInit = 1;
constexpr std::uint8_t kTaskSortBitonic = 8;
constexpr std::uint8_t kTaskSortPrepass = 9;
constexpr std::uint8_t kTaskSortMerge = 10;
constexpr std::uint8_t kTaskEventPrefix = 11;
constexpr std::uint8_t kTaskEventReset = 12;
constexpr std::uint8_t kTaskEventCopy = 13;
constexpr std::uint32_t kSortChunkSize = 4096u;

std::uint32_t burstOccurrences(const AssetBurst& burst, double time) noexcept {
    if (time < static_cast<double>(burst.time)) {
        return 0u;
    }
    if (!(burst.period > 0.0f)) {
        return 1u;
    }
    const double period = static_cast<double>(burst.period);
    const double raw = 1.0 + std::floor((time - static_cast<double>(burst.time)) / period +
                                       kSpawnerBoundaryEpsilon);
    const double capped = std::min(raw, static_cast<double>(std::numeric_limits<std::uint32_t>::max()));
    std::uint32_t occurrences = static_cast<std::uint32_t>(std::max(0.0, capped));
    if (burst.repeat != 0u) {
        occurrences = std::min(occurrences, burst.repeat);
    }
    return occurrences;
}

Mat4 inverseMatrix(const Mat4& matrix) {
    const float* m = matrix.m.data();
    float inv[16];
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] +
             m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] -
             m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] +
             m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] -
              m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] -
             m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] +
             m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] -
             m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] +
              m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] +
             m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] -
             m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] +
              m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] -
              m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] -
             m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] +
             m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] -
              m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] +
              m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    const float determinant = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    Mat4 result = Mat4::identity();
    if (std::abs(determinant) < 1.0e-8f) {
        return result;
    }
    const float invDet = 1.0f / determinant;
    for (std::size_t i = 0; i < 16; ++i) {
        result.m[i] = inv[i] * invDet;
    }
    return result;
}

Vec3 transformPoint(const Mat4& matrix, const Vec3& point) {
    return Vec3{matrix.m[0] * point.x + matrix.m[4] * point.y + matrix.m[8] * point.z + matrix.m[12],
                matrix.m[1] * point.x + matrix.m[5] * point.y + matrix.m[9] * point.z + matrix.m[13],
                matrix.m[2] * point.x + matrix.m[6] * point.y + matrix.m[10] * point.z + matrix.m[14]};
}

Vec3 minVec3(const Vec3& a, const Vec3& b) {
    return Vec3{std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)};
}

Vec3 maxVec3(const Vec3& a, const Vec3& b) {
    return Vec3{std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)};
}

std::uint16_t floatToHalf(float value) {
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
    const std::uint16_t sign = static_cast<std::uint16_t>((bits >> 16u) & 0x8000u);
    const std::uint32_t exponent = (bits >> 23u) & 0xffu;
    const std::uint32_t mantissa = bits & 0x007fffffu;
    if (exponent == 0xffu) {
        return static_cast<std::uint16_t>(sign | (mantissa != 0u ? 0x7e00u : 0x7c00u));
    }
    int halfExponent = static_cast<int>(exponent) - 127 + 15;
    if (halfExponent >= 31) {
        return static_cast<std::uint16_t>(sign | 0x7c00u);
    }
    if (halfExponent <= 0) {
        if (halfExponent < -10) {
            return sign;
        }
        const std::uint32_t shifted = (mantissa | 0x00800000u) >> static_cast<unsigned>(14 - halfExponent);
        return static_cast<std::uint16_t>(sign | (shifted & 0x03ffu));
    }
    return static_cast<std::uint16_t>(sign | (static_cast<std::uint16_t>(halfExponent) << 10u) |
                                      ((mantissa >> 13u) & 0x03ffu));
}

void appendHalf(std::vector<std::byte>& bytes, float value) {
    const std::uint16_t half = floatToHalf(value);
    bytes.push_back(static_cast<std::byte>(half & 0xffu));
    bytes.push_back(static_cast<std::byte>((half >> 8u) & 0xffu));
}

float sampleCurve(std::span<const CurveKey> curve, float t) {
    if (curve.empty()) {
        return 0.0f;
    }
    t = std::clamp(t, 0.0f, 1.0f);
    if (curve.size() == 1u || t <= curve.front().time) {
        return curve.front().value;
    }
    for (std::size_t i = 1; i < curve.size(); ++i) {
        const CurveKey& a = curve[i - 1u];
        const CurveKey& b = curve[i];
        if (t < b.time) {
            const float span = b.time - a.time;
            return span > 0.0f ? a.value + (b.value - a.value) * ((t - a.time) / span) : b.value;
        }
    }
    return curve.back().value;
}

Vec4 sampleGradient(std::span<const GradientKey> gradient, float t) {
    if (gradient.empty()) {
        return Vec4{};
    }
    t = std::clamp(t, 0.0f, 1.0f);
    if (gradient.size() == 1u || t <= gradient.front().time) {
        return gradient.front().color;
    }
    for (std::size_t i = 1; i < gradient.size(); ++i) {
        const GradientKey& a = gradient[i - 1u];
        const GradientKey& b = gradient[i];
        if (t < b.time) {
            const float span = b.time - a.time;
            if (span <= 0.0f) {
                return b.color;
            }
            const float u = (t - a.time) / span;
            return Vec4{a.color.x + (b.color.x - a.color.x) * u,
                        a.color.y + (b.color.y - a.color.y) * u,
                        a.color.z + (b.color.z - a.color.z) * u,
                        a.color.w + (b.color.w - a.color.w) * u};
        }
    }
    return gradient.back().color;
}

std::vector<std::byte> makeLut(PropertyType type, std::span<const CurveKey> curve,
                               std::span<const GradientKey> gradient) {
    std::vector<std::byte> bytes;
    bytes.reserve(kLutRowPitch);
    for (std::uint32_t i = 0; i < kLutSampleCount; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kLutSampleCount - 1u);
        if (type == PropertyType::Curve) {
            appendHalf(bytes, sampleCurve(curve, t));
            appendHalf(bytes, 0.0f);
            appendHalf(bytes, 0.0f);
            appendHalf(bytes, 0.0f);
        } else {
            const Vec4 value = sampleGradient(gradient, t);
            appendHalf(bytes, value.x);
            appendHalf(bytes, value.y);
            appendHalf(bytes, value.z);
            appendHalf(bytes, value.w);
        }
    }
    return bytes;
}

bool isNumericProperty(PropertyType type) {
    return type == PropertyType::Float || type == PropertyType::Int || type == PropertyType::Bool ||
           type == PropertyType::Vector2 || type == PropertyType::Vector3 || type == PropertyType::Vector4 ||
           type == PropertyType::Color;
}

bool eventPropertyTypeMatches(PropertyType property, AttributeType target) {
    switch (target) {
    case AttributeType::Float:
        return property == PropertyType::Float;
    case AttributeType::Float2:
        return property == PropertyType::Vector2;
    case AttributeType::Float3:
        return property == PropertyType::Vector3 || property == PropertyType::Color;
    case AttributeType::Float4:
        return property == PropertyType::Vector4 || property == PropertyType::Color;
    case AttributeType::UInt:
    case AttributeType::Int:
        return property == PropertyType::Int;
    case AttributeType::Bool:
        return property == PropertyType::Bool;
    }
    return false;
}

std::array<float, 4> eventPropertyToPayload(const PropertyValue& value, AttributeType target) {
    std::array<float, 4> result{};
    switch (target) {
    case AttributeType::Float:
        result[0] = value.f[0];
        break;
    case AttributeType::Float2:
        result[0] = value.f[0];
        result[1] = value.f[1];
        break;
    case AttributeType::Float3:
        result[0] = value.f[0];
        result[1] = value.f[1];
        result[2] = value.f[2];
        break;
    case AttributeType::Float4:
        result = value.f;
        break;
    case AttributeType::UInt:
    case AttributeType::Int:
        result[0] = value.f[0];
        break;
    case AttributeType::Bool:
        result[0] = value.f[0] != 0.0f ? 1.0f : 0.0f;
        break;
    }
    return result;
}

std::size_t backendIndex(ShaderBackend backend) {
    return backend == ShaderBackend::Spirv ? 1u : 0u;
}

struct BufferRuntime {
    BufferHandle handle{};
    std::uint32_t size = 0;
    std::uint32_t stride = 0;
};

struct SystemRuntime {
    std::vector<BufferRuntime> buffers;
    std::vector<std::uint32_t> kernels;
    std::vector<std::uint32_t> outputs;
    std::vector<std::uint32_t> burstFired;
    double time = 0.0;
    double spawnAccumulator = 0.0;
    std::uint32_t spawnCount = 0;
    std::uint32_t aliveEstimate = 0;
};

struct CpuEventRecord {
    std::uint32_t link = 0;
    std::array<std::array<float, 4>, 4> payload{};
};

struct LutRuntime {
    std::vector<std::uint32_t> properties;
    TextureHandle texture{};
    SamplerHandle sampler{};
};

} // namespace

class EffectImpl final : public Effect {
  public:
    EffectImpl(Runtime& runtime, const std::shared_ptr<const Asset>& asset, const EffectDesc& desc,
               std::string& error);
    ~EffectImpl() override;

    void play() override;
    void pause() override;
    void stop() override;
    void reset() override;
    void advanceOneFrame() override;
    void setTransform(const Mat4& world) override;
    void setPlayRate(float rate) override;
    bool isAlive() const override;

    bool setProperty(std::string_view name, const PropertyValue& value) override;
    bool setFloat(std::string_view name, float value) override;
    bool setVector2(std::string_view name, Vec2 value) override;
    bool setVector3(std::string_view name, Vec3 value) override;
    bool setVector4(std::string_view name, Vec4 value) override;
    bool setColor(std::string_view name, Color value) override;
    bool setTexture(std::string_view name, TextureHandle texture) override;
    bool setCurve(std::string_view name, std::span<const CurveKey> curve) override;
    bool setGradient(std::string_view name, std::span<const GradientKey> gradient) override;

    void sendEvent(std::string_view name, std::span<const EventAttribute> attributes) override;
    void uploadCpuEvents();

    Bounds bounds() const override;
    bool isCulled() const override;

    void update(float deltaSeconds);
    void encodeSimulation(CommandSink& sink, const FrameContext& frame);
    void encodeOutput(CommandSink& sink, const FrameContext& frame);
    std::uint32_t aliveEstimate() const { return m_aliveEstimate; }
    std::uint32_t systemCount() const { return static_cast<std::uint32_t>(m_systems.size()); }
    const Asset* assetPtr() const noexcept { return m_asset.get(); }
    bool needsSimulation() const { return m_needsReset || m_stepPending || (m_playing && !m_paused); }
    bool needsOutput() const { return m_playing && !m_needsReset; }

  private:
    bool validateDeviceLimits(std::string& error) const;
    void createResources(std::string& error);
    void releaseResources();
    void refreshBounds();
    void resetCpuState();
    void setUniforms(CommandSink& sink, std::uint32_t systemIndex, bool simulate,
                     const FrameContext& frame);
    void bindKernelBuffers(CommandSink& sink, std::uint32_t systemIndex, const AssetProgram& program);
    void bindKernelTextures(CommandSink& sink, const AssetProgram& program);
    void dispatchKernel(CommandSink& sink, std::uint32_t systemIndex, std::uint32_t programIndex);
    void bindOutputBuffers(CommandSink& sink, std::uint32_t systemIndex, const AssetProgram& program);
    void bindOutputTexture(CommandSink& sink, const FrameContext& frame, const AssetProgram& program);
    void createLutTextures(std::string& error);
    void updateLutTexture(LutRuntime& lut);
    void updateLut(std::size_t propertyIndex);
    void report(std::string_view message);

    Runtime& m_runtime;
    Device& m_device;
    std::shared_ptr<const Asset> m_asset;
    Mat4 m_transform = Mat4::identity();
    float m_playRate = 1.0f;
    std::uint32_t m_randomSeed = 0;
    bool m_playing = false;
    bool m_paused = false;
    bool m_stopped = true;
    bool m_stepPending = false;
    bool m_stepFrame = false;
    bool m_needsReset = true;
    bool m_hasBoundsOverride = false;
    Bounds m_boundsOverride{};
    Bounds m_bounds{};
    float m_time = 0.0f;
    float m_deltaTime = 0.0f;
    std::uint32_t m_nextParticleId = 0;
    std::uint32_t m_aliveEstimate = 0;

    std::vector<SystemRuntime> m_systems;
    std::vector<ProgramHandle> m_programs;
    std::vector<UniformHandle> m_propertyUniforms;
    std::vector<std::array<float, 4>> m_propertyValues;
    std::vector<TextureHandle> m_propertyTextures;
    std::vector<std::vector<std::byte>> m_lutData;
    LutRuntime m_curveLut;
    LutRuntime m_gradientLut;
    TextureHandle m_whiteTexture{};
    UniformHandle m_paramsUniform{};
    UniformHandle m_idsUniform{};
    UniformHandle m_worldUniform{};
    UniformHandle m_invViewUniform{};
    UniformHandle m_viewUniform{};
    UniformHandle m_viewProjUniform{};
    UniformHandle m_invViewProjUniform{};
    UniformHandle m_cameraPosUniform{};
    UniformHandle m_softParamsUniform{};
    SamplerHandle m_mainSampler{};
    SamplerHandle m_sceneDepthSampler{};
    BufferHandle m_eventBuffer{};
    std::uint32_t m_eventPrefixProgram = kInvalidIndex;
    std::uint32_t m_eventResetProgram = kInvalidIndex;
    std::uint32_t m_eventCopyProgram = kInvalidIndex;
    BufferHandle m_cpuEventBuffer{};
    std::vector<std::byte> m_cpuEventData;
    std::vector<std::vector<CpuEventRecord>> m_cpuPending;
    std::vector<std::uint32_t> m_cpuEventTotal;
    std::vector<std::uint32_t> m_cpuEventDropped;
    std::vector<bool> m_systemHasEventInput;
};

EffectImpl::EffectImpl(Runtime& runtime, const std::shared_ptr<const Asset>& asset, const EffectDesc& desc,
                       std::string& error)
    : m_runtime(runtime), m_device(runtime.m_device), m_asset(asset), m_transform(desc.transform),
      m_playRate(std::max(0.0f, desc.playRate)), m_randomSeed(desc.randomSeed),
      m_playing(desc.playOnAwake), m_stopped(!desc.playOnAwake), m_hasBoundsOverride(desc.hasBoundsOverride),
      m_boundsOverride(desc.boundsOverride) {
    if (m_asset == nullptr) {
        error = "effect asset is null";
        return;
    }
    createResources(error);
    m_propertyValues.resize(m_asset->properties.size());
    m_propertyTextures.resize(m_asset->properties.size());
    m_lutData.resize(m_asset->properties.size());
    m_propertyUniforms.resize(m_asset->properties.size());
    for (std::size_t i = 0; i < m_asset->properties.size(); ++i) {
        const AssetProperty& property = m_asset->properties[i];
        m_propertyValues[i] = property.defaultFloat;
        if (property.type == PropertyType::Curve || property.type == PropertyType::Gradient) {
            if (property.lut.size() == kLutRowPitch) {
                m_lutData[i] = property.lut;
            } else {
                m_lutData[i].assign(kLutRowPitch, std::byte{0});
            }
            if (property.type == PropertyType::Curve) {
                m_curveLut.properties.push_back(static_cast<std::uint32_t>(i));
            } else {
                m_gradientLut.properties.push_back(static_cast<std::uint32_t>(i));
            }
        }
        if (isNumericProperty(property.type)) {
            m_propertyUniforms[i] =
                m_device.findUniform("u_vfxProp_" + property.name, UniformType::Vec4);
            if (!m_propertyUniforms[i].valid() && error.empty()) {
                error = "failed to create property uniform '" + property.name + "'";
            }
        }
    }
    m_paramsUniform = m_device.findUniform("u_vfx_params", UniformType::Vec4);
    m_idsUniform = m_device.findUniform("u_vfx_ids", UniformType::Vec4);
    m_worldUniform = m_device.findUniform("u_vfx_world", UniformType::Mat4);
    m_invViewUniform = m_device.findUniform("u_global_invView", UniformType::Mat4);
    m_viewUniform = m_device.findUniform("u_global_view", UniformType::Mat4);
    m_viewProjUniform = m_device.findUniform("u_global_viewProj", UniformType::Mat4);
    m_invViewProjUniform = m_device.findUniform("u_global_invViewProj", UniformType::Mat4);
    m_cameraPosUniform = m_device.findUniform("u_global_cameraPos", UniformType::Vec4);
    m_softParamsUniform = m_device.findUniform("u_vfxSoftParams", UniformType::Vec4);
    m_mainSampler = m_device.findSampler("s_vfxMain");
    m_sceneDepthSampler = m_device.findSampler("s_vfxSceneDepth");
    if (error.empty() &&
        (!m_paramsUniform.valid() || !m_idsUniform.valid() || !m_worldUniform.valid() ||
         !m_invViewUniform.valid() || !m_viewUniform.valid() || !m_viewProjUniform.valid() ||
         !m_invViewProjUniform.valid() || !m_cameraPosUniform.valid() ||
         !m_softParamsUniform.valid() || !m_mainSampler.valid() || !m_sceneDepthSampler.valid())) {
        error = "failed to create VFX runtime uniforms/sampler";
    }
    if (error.empty()) {
        createLutTextures(error);
    }
    const std::array<std::byte, 4> white = {std::byte{255}, std::byte{255}, std::byte{255}, std::byte{255}};
    TextureDesc whiteDesc;
    whiteDesc.width = 1;
    whiteDesc.height = 1;
    whiteDesc.format = TextureFormat::RGBA8;
    whiteDesc.initialData = white.data();
    whiteDesc.dataSize = static_cast<std::uint32_t>(white.size());
    whiteDesc.name = "vfx.white";
    m_whiteTexture = m_device.createTexture(whiteDesc);
    if (!m_whiteTexture.valid() && error.empty()) {
        error = "failed to create VFX fallback texture";
    }
    refreshBounds();
    resetCpuState();
    m_needsReset = true;
}

EffectImpl::~EffectImpl() { releaseResources(); }

bool EffectImpl::validateDeviceLimits(std::string& error) const {
    const Limits limits = m_device.limits();
    for (const AssetSystem& system : m_asset->systems) {
        if (system.capacity == 0u || system.capacity > kMaxParticleCapacity) {
            error = "VFX system '" + system.name + "' capacity " + std::to_string(system.capacity) +
                    " exceeds the runtime limit " + std::to_string(kMaxParticleCapacity);
            return false;
        }
    }

    const auto checkBindings = [&](const AssetProgram& program,
                                   std::span<const AssetBinding> bindings,
                                   std::string_view stageName) {
        for (const AssetBinding& binding : bindings) {
            const bool texture = binding.kind == AssetBindingKind::Texture;
            const std::uint32_t limit = texture ? limits.maxTextureSamplers : limits.maxComputeBindings;
            if (limit != 0u && binding.stage >= limit) {
                error = "VFX program " + std::to_string(program.program) + " " +
                        std::string(stageName) + " binding stage " + std::to_string(binding.stage) +
                        " exceeds the device " + (texture ? "texture sampler" : "storage") +
                        " limit " + std::to_string(limit);
                return false;
            }
        }
        return true;
    };

    const std::size_t currentBackend = backendIndex(m_device.backend());
    for (const AssetProgram& program : m_asset->programs) {
        if (!program.output) {
            if (program.computeBlobs[currentBackend].empty()) {
                error = "VFX program " + std::to_string(program.program) +
                        " is missing the selected backend shader";
                return false;
            }
            if (!checkBindings(program, program.bindings, "compute")) {
                return false;
            }
            continue;
        }
        if (program.vertexBlobs[currentBackend].empty() ||
            program.fragmentBlobs[currentBackend].empty()) {
            error = "VFX program " + std::to_string(program.program) +
                    " is missing the selected backend shader";
            return false;
        }
        if (!checkBindings(program, program.vertexBindings, "vertex") ||
            !checkBindings(program, program.fragmentBindings, "fragment")) {
            return false;
        }
    }
    return true;
}

void EffectImpl::createResources(std::string& error) {
    if (!validateDeviceLimits(error)) {
        return;
    }
    m_systems.resize(m_asset->systems.size());
    m_programs.resize(m_asset->programs.size());
    const std::size_t currentBackend = backendIndex(m_device.backend());
    for (std::size_t systemIndex = 0; systemIndex < m_asset->systems.size(); ++systemIndex) {
        const AssetSystem& assetSystem = m_asset->systems[systemIndex];
        SystemRuntime& system = m_systems[systemIndex];
        system.buffers.resize(kBufferKindCount);
        for (const AssetBuffer& assetBuffer : assetSystem.buffers) {
            BufferUsage usage = BufferUsage::Storage;
            if (assetBuffer.kind == kAttributesBuffer || assetBuffer.kind == kIndirectionBuffer ||
                assetBuffer.kind == kRenderKVPBuffer || assetBuffer.kind == kSortScratchBuffer) {
                usage = BufferUsage::StorageVertex;
            } else if (assetBuffer.kind == kIndirectBuffer) {
                usage = BufferUsage::Indirect;
            }
            BufferDesc desc;
            desc.usage = usage;
            desc.size = assetBuffer.size;
            desc.stride = assetBuffer.stride;
            const std::string bufferName =
                "vfx." + assetSystem.name + ".buffer" + std::to_string(assetBuffer.kind);
            desc.name = bufferName;
            BufferRuntime runtimeBuffer;
            runtimeBuffer.size = assetBuffer.size;
            runtimeBuffer.stride = assetBuffer.stride;
            runtimeBuffer.handle = m_device.createBuffer(desc);
            if (!runtimeBuffer.handle.valid() && error.empty()) {
                error = "failed to create VFX buffer '" + bufferName + "'";
            }
            system.buffers[assetBuffer.kind] = runtimeBuffer;
        }
        system.burstFired.assign(assetSystem.bursts.size(), 0u);
    }
    m_systemHasEventInput.assign(m_asset->systems.size(), false);
    for (const AssetEventLink& link : m_asset->eventLinks) {
        if (link.consumerSystem < m_systemHasEventInput.size()) {
            m_systemHasEventInput[link.consumerSystem] = true;
        }
    }
    if (m_asset->eventBufferSize > 0u) {
        BufferDesc eventDesc;
        eventDesc.usage = BufferUsage::Storage;
        eventDesc.size = m_asset->eventBufferSize;
        eventDesc.stride = 4u;
        eventDesc.initialData = nullptr;
        eventDesc.name = "vfx.events";
        m_eventBuffer = m_device.createBuffer(eventDesc);
        if (!m_eventBuffer.valid() && error.empty()) {
            error = "failed to create VFX event buffer";
        }
    }
    bool hasCpuEvents = false;
    for (const AssetEventLink& link : m_asset->eventLinks) {
        hasCpuEvents = hasCpuEvents || link.cpu;
    }
    m_cpuPending.assign(m_asset->eventLinks.size(), {});
    m_cpuEventTotal.assign(m_asset->eventLinks.size(), 0u);
    m_cpuEventDropped.assign(m_asset->eventLinks.size(), 0u);
    if (hasCpuEvents && m_asset->eventBufferSize > 0u) {
        m_cpuEventData.assign(m_asset->eventBufferSize, std::byte{0});
        BufferDesc cpuEventDesc;
        cpuEventDesc.usage = BufferUsage::Storage;
        cpuEventDesc.size = m_asset->eventBufferSize;
        cpuEventDesc.stride = 4u;
        cpuEventDesc.initialData = m_cpuEventData.data();
        cpuEventDesc.name = "vfx.cpu.events";
        m_cpuEventBuffer = m_device.createBuffer(cpuEventDesc);
        if (!m_cpuEventBuffer.valid() && error.empty()) {
            error = "failed to create VFX CPU event staging buffer";
        }
    }

    for (std::size_t programIndex = 0; programIndex < m_asset->programs.size(); ++programIndex) {
        const AssetProgram& assetProgram = m_asset->programs[programIndex];
        std::vector<ShaderBlob> blobs;
        if (assetProgram.output) {
            blobs.push_back({ShaderStage::Vertex, m_device.backend(),
                             std::span<const std::byte>(assetProgram.vertexBlobs[currentBackend])});
            blobs.push_back({ShaderStage::Fragment, m_device.backend(),
                             std::span<const std::byte>(assetProgram.fragmentBlobs[currentBackend])});
        } else {
            blobs.push_back({ShaderStage::Compute, m_device.backend(),
                             std::span<const std::byte>(assetProgram.computeBlobs[currentBackend])});
        }
        m_programs[programIndex] = m_device.createProgram(blobs);
        if (!m_programs[programIndex].valid() && error.empty()) {
            error = "failed to create VFX program " + std::to_string(assetProgram.program);
        }
        if (assetProgram.global) {
            if (assetProgram.task == kTaskEventPrefix) {
                m_eventPrefixProgram = static_cast<std::uint32_t>(programIndex);
            } else if (assetProgram.task == kTaskEventReset) {
                m_eventResetProgram = static_cast<std::uint32_t>(programIndex);
            } else if (assetProgram.task == kTaskEventCopy) {
                m_eventCopyProgram = static_cast<std::uint32_t>(programIndex);
            }
            continue;
        }
        if (assetProgram.output) {
            m_systems[assetProgram.system].outputs.push_back(static_cast<std::uint32_t>(programIndex));
        } else {
            m_systems[assetProgram.system].kernels.push_back(static_cast<std::uint32_t>(programIndex));
        }
    }
}

void EffectImpl::releaseResources() {
    for (const ProgramHandle program : m_programs) {
        if (program.valid()) {
            m_device.destroyProgram(program);
        }
    }
    m_programs.clear();
    for (SystemRuntime& system : m_systems) {
        for (BufferRuntime& buffer : system.buffers) {
            if (buffer.handle.valid()) {
                m_device.destroyBuffer(buffer.handle);
                buffer.handle = {};
            }
        }
    }
    m_systems.clear();
    for (LutRuntime* lut : {&m_curveLut, &m_gradientLut}) {
        if (lut->texture.valid()) {
            m_device.destroyTexture(lut->texture);
            lut->texture = {};
        }
    }
    if (m_whiteTexture.valid()) {
        m_device.destroyTexture(m_whiteTexture);
        m_whiteTexture = {};
    }
    if (m_eventBuffer.valid()) {
        m_device.destroyBuffer(m_eventBuffer);
        m_eventBuffer = {};
    }
    if (m_cpuEventBuffer.valid()) {
        m_device.destroyBuffer(m_cpuEventBuffer);
        m_cpuEventBuffer = {};
    }
}

void EffectImpl::play() {
    if (m_stopped || m_needsReset) {
        resetCpuState();
        m_aliveEstimate = 0;
        m_stopped = false;
        m_needsReset = true;
    }
    m_playing = true;
    m_paused = false;
    m_stepPending = false;
    m_stepFrame = false;
}

void EffectImpl::pause() {
    if (m_playing) {
        m_paused = true;
    }
}

void EffectImpl::stop() {
    m_playing = false;
    m_paused = false;
    m_stopped = true;
    m_stepPending = false;
    m_stepFrame = false;
    m_aliveEstimate = 0;
    m_deltaTime = 0.0f;
    resetCpuState();
    m_needsReset = true;
}

void EffectImpl::reset() {
    resetCpuState();
    m_aliveEstimate = 0;
    m_stepPending = false;
    m_stepFrame = false;
    m_needsReset = true;
}

void EffectImpl::advanceOneFrame() {
    if (m_stopped || !m_playing) {
        m_stopped = false;
        m_playing = true;
    }
    m_paused = true;
    m_stepPending = true;
}

void EffectImpl::resetCpuState() {
    m_time = 0.0f;
    m_deltaTime = 0.0f;
    m_nextParticleId = 0;
    for (std::size_t i = 0; i < m_systems.size(); ++i) {
        SystemRuntime& system = m_systems[i];
        system.time = 0.0;
        system.spawnAccumulator = 0.0;
        system.spawnCount = 0;
        system.aliveEstimate = 0;
        system.burstFired.assign(m_asset->systems[i].bursts.size(), 0u);
    }
    for (std::vector<CpuEventRecord>& pending : m_cpuPending) {
        pending.clear();
    }
    std::fill(m_cpuEventTotal.begin(), m_cpuEventTotal.end(), 0u);
    std::fill(m_cpuEventDropped.begin(), m_cpuEventDropped.end(), 0u);
    std::fill(m_cpuEventData.begin(), m_cpuEventData.end(), std::byte{0});
}

void EffectImpl::setTransform(const Mat4& world) {
    m_transform = world;
    refreshBounds();
}

void EffectImpl::setPlayRate(float rate) {
    if (std::isfinite(rate)) {
        m_playRate = std::max(0.0f, rate);
    }
}

bool EffectImpl::isAlive() const { return m_playing || m_paused || m_aliveEstimate != 0u; }

bool EffectImpl::setProperty(std::string_view name, const PropertyValue& value) {
    for (std::size_t i = 0; i < m_asset->properties.size(); ++i) {
        const AssetProperty& property = m_asset->properties[i];
        if (property.name != name) {
            continue;
        }
        if (value.type != property.type) {
            return false;
        }
        if (isNumericProperty(property.type)) {
            m_propertyValues[i] = value.f;
            return true;
        }
        if (property.type == PropertyType::Texture) {
            m_propertyTextures[i] = value.texture;
            return true;
        }
        if (property.type == PropertyType::Curve) {
            if (value.curve.data() == nullptr && !value.curve.empty()) {
                return false;
            }
            m_lutData[i] = makeLut(PropertyType::Curve, value.curve, {});
            updateLut(i);
            return true;
        }
        if (property.type == PropertyType::Gradient) {
            if (value.gradient.data() == nullptr && !value.gradient.empty()) {
                return false;
            }
            m_lutData[i] = makeLut(PropertyType::Gradient, {}, value.gradient);
            updateLut(i);
            return true;
        }
        return false;
    }
    return false;
}

bool EffectImpl::setFloat(std::string_view name, float value) {
    PropertyValue propertyValue;
    propertyValue.type = PropertyType::Float;
    propertyValue.f[0] = value;
    return setProperty(name, propertyValue);
}

bool EffectImpl::setVector2(std::string_view name, Vec2 value) {
    PropertyValue propertyValue;
    propertyValue.type = PropertyType::Vector2;
    propertyValue.f[0] = value.x;
    propertyValue.f[1] = value.y;
    return setProperty(name, propertyValue);
}

bool EffectImpl::setVector3(std::string_view name, Vec3 value) {
    PropertyValue propertyValue;
    propertyValue.type = PropertyType::Vector3;
    propertyValue.f[0] = value.x;
    propertyValue.f[1] = value.y;
    propertyValue.f[2] = value.z;
    return setProperty(name, propertyValue);
}

bool EffectImpl::setVector4(std::string_view name, Vec4 value) {
    PropertyValue propertyValue;
    propertyValue.type = PropertyType::Vector4;
    propertyValue.f[0] = value.x;
    propertyValue.f[1] = value.y;
    propertyValue.f[2] = value.z;
    propertyValue.f[3] = value.w;
    return setProperty(name, propertyValue);
}

bool EffectImpl::setColor(std::string_view name, Color value) {
    PropertyValue propertyValue;
    propertyValue.type = PropertyType::Color;
    propertyValue.f[0] = value.x;
    propertyValue.f[1] = value.y;
    propertyValue.f[2] = value.z;
    propertyValue.f[3] = value.w;
    return setProperty(name, propertyValue);
}

bool EffectImpl::setTexture(std::string_view name, TextureHandle texture) {
    PropertyValue propertyValue;
    propertyValue.type = PropertyType::Texture;
    propertyValue.texture = texture;
    return setProperty(name, propertyValue);
}

bool EffectImpl::setCurve(std::string_view name, std::span<const CurveKey> curve) {
    PropertyValue propertyValue;
    propertyValue.type = PropertyType::Curve;
    propertyValue.curve = curve;
    return setProperty(name, propertyValue);
}

bool EffectImpl::setGradient(std::string_view name, std::span<const GradientKey> gradient) {
    PropertyValue propertyValue;
    propertyValue.type = PropertyType::Gradient;
    propertyValue.gradient = gradient;
    return setProperty(name, propertyValue);
}

void EffectImpl::sendEvent(std::string_view name, std::span<const EventAttribute> attributes) {
    for (std::size_t linkIndex = 0; linkIndex < m_asset->eventLinks.size(); ++linkIndex) {
        const AssetEventLink& link = m_asset->eventLinks[linkIndex];
        if (link.name != name) {
            continue;
        }
        if (!link.cpu) {
            report("VFX event '" + std::string(name) + "' is a GPU event and cannot be sent from CPU");
            return;
        }
        if (linkIndex >= m_cpuPending.size()) {
            report("VFX CPU event link is unavailable");
            return;
        }
        std::vector<CpuEventRecord>& pending = m_cpuPending[linkIndex];
        if (pending.size() >= link.capacity) {
            ++m_cpuEventDropped[linkIndex];
            return;
        }
        CpuEventRecord record;
        record.link = static_cast<std::uint32_t>(linkIndex);
        for (const AssetEventPayload& payload : link.payload) {
            record.payload[payload.slot] = payload.defaultFloat;
            const auto found = std::find_if(attributes.begin(), attributes.end(),
                                            [&](const EventAttribute& attribute) {
                                                return attribute.name == payload.name;
                                            });
            if (found == attributes.end()) {
                continue;
            }
            if (!eventPropertyTypeMatches(found->value.type, payload.type)) {
                report("VFX event '" + std::string(name) + "' attribute '" + payload.name +
                       "' has an incompatible type");
                return;
            }
            record.payload[payload.slot] = eventPropertyToPayload(found->value, payload.type);
        }
        pending.push_back(record);
        ++m_cpuEventTotal[linkIndex];
        return;
    }
    report("VFX CPU event '" + std::string(name) + "' is not declared by the asset");
}

Bounds EffectImpl::bounds() const {
    return m_hasBoundsOverride ? m_boundsOverride : m_bounds;
}

bool EffectImpl::isCulled() const { return false; }

void EffectImpl::refreshBounds() {
    Bounds bounds{};
    bool hasBounds = false;
    for (std::size_t i = 0; i < m_asset->systems.size(); ++i) {
        Bounds systemBounds = m_asset->systems[i].bounds;
        if (m_asset->systems[i].space == SystemSpace::Local) {
            const Vec3 min = systemBounds.min;
            const Vec3 max = systemBounds.max;
            const std::array<Vec3, 8> corners = {
                Vec3{min.x, min.y, min.z}, Vec3{max.x, min.y, min.z}, Vec3{min.x, max.y, min.z},
                Vec3{max.x, max.y, min.z}, Vec3{min.x, min.y, max.z}, Vec3{max.x, min.y, max.z},
                Vec3{min.x, max.y, max.z}, Vec3{max.x, max.y, max.z}};
            Vec3 transformedMin = transformPoint(m_transform, corners[0]);
            Vec3 transformedMax = transformedMin;
            for (const Vec3& corner : corners) {
                const Vec3 transformed = transformPoint(m_transform, corner);
                transformedMin = minVec3(transformedMin, transformed);
                transformedMax = maxVec3(transformedMax, transformed);
            }
            systemBounds = Bounds{transformedMin, transformedMax, true};
        }
        if (!hasBounds) {
            bounds = systemBounds;
            hasBounds = true;
        } else {
            bounds = Bounds{minVec3(bounds.min, systemBounds.min), maxVec3(bounds.max, systemBounds.max), true};
        }
    }
    m_bounds = bounds;
}

void EffectImpl::update(float deltaSeconds) {
    if (!m_playing) {
        m_deltaTime = 0.0f;
        return;
    }

    m_stepFrame = false;
    if (m_stepPending) {
        m_stepPending = false;
        m_stepFrame = true;
        m_deltaTime = kStepDeltaSeconds;
    } else if (!m_paused) {
        const float clamped = std::clamp(deltaSeconds, 0.0f, kMaxDeltaSeconds);
        m_deltaTime = std::isfinite(clamped) ? clamped * m_playRate : 0.0f;
    } else {
        m_deltaTime = 0.0f;
        return;
    }
    m_time += m_deltaTime;
    for (std::size_t i = 0; i < m_systems.size(); ++i) {
        SystemRuntime& system = m_systems[i];
        const AssetSystem& assetSystem = m_asset->systems[i];
        system.time += static_cast<double>(m_deltaTime);
        system.spawnAccumulator += static_cast<double>(assetSystem.spawnerRate) * m_deltaTime;
        const double accumulated = std::floor(system.spawnAccumulator);
        const double maxSpawn = static_cast<double>(std::numeric_limits<std::uint32_t>::max());
        std::uint64_t spawnCount =
            static_cast<std::uint64_t>(std::min(std::max(accumulated, 0.0), maxSpawn));
        system.spawnAccumulator -= accumulated;

        for (std::size_t burstIndex = 0; burstIndex < assetSystem.bursts.size(); ++burstIndex) {
            const AssetBurst& burst = assetSystem.bursts[burstIndex];
            std::uint32_t& fired = system.burstFired[burstIndex];
            const std::uint32_t occurrences = burstOccurrences(burst, system.time);
            if (occurrences > fired) {
                const std::uint64_t additional =
                    static_cast<std::uint64_t>(occurrences - fired) * static_cast<std::uint64_t>(burst.count);
                const std::uint64_t maximum = std::numeric_limits<std::uint32_t>::max();
                spawnCount = additional >= maximum - spawnCount ? maximum : spawnCount + additional;
                fired = occurrences;
            }
        }
        system.spawnCount = static_cast<std::uint32_t>(spawnCount);
    }
}

void EffectImpl::setUniforms(CommandSink& sink, std::uint32_t systemIndex, bool simulate,
                             const FrameContext& frame) {
    const SystemRuntime& system = m_systems[systemIndex];
    const AssetSystem& assetSystem = m_asset->systems[systemIndex];
    const Vec4 params{static_cast<float>(system.time), simulate ? m_deltaTime : 0.0f,
                      simulate ? static_cast<float>(system.spawnCount) : 0.0f,
                      static_cast<float>(m_randomSeed)};
    sink.setUniform(m_paramsUniform, &params);
    const Vec4 ids{static_cast<float>(m_nextParticleId), static_cast<float>(systemIndex), 0.0f, 0.0f};
    sink.setUniform(m_idsUniform, &ids);
    if (assetSystem.sortMode == SortMode::Distance) {
        const Mat4 world =
            assetSystem.space == SystemSpace::Local ? m_transform : Mat4::identity();
        const Vec4 cameraPosition{frame.cameraPosition.x, frame.cameraPosition.y,
                                  frame.cameraPosition.z, 1.0f};
        sink.setUniform(m_worldUniform, &world);
        sink.setUniform(m_cameraPosUniform, &cameraPosition);
    }
    for (std::size_t i = 0; i < m_propertyUniforms.size(); ++i) {
        if (!m_propertyUniforms[i].valid()) {
            continue;
        }
        const std::array<float, 4>& value = m_propertyValues[i];
        sink.setUniform(m_propertyUniforms[i], value.data());
    }
}

void EffectImpl::bindKernelBuffers(CommandSink& sink, std::uint32_t systemIndex,
                                   const AssetProgram& program) {
    const SystemRuntime& system = m_systems[systemIndex];
    for (const AssetBinding& binding : program.bindings) {
        if (binding.kind == AssetBindingKind::Texture) {
            continue;
        }
        if (binding.kind == AssetBindingKind::Event) {
            if (!m_eventBuffer.valid()) {
                report("VFX kernel references an unavailable event buffer");
                continue;
            }
            sink.setStorageBuffer(binding.stage, m_eventBuffer, binding.access);
            continue;
        }
        if (binding.kind == AssetBindingKind::CpuEvent) {
            if (!m_cpuEventBuffer.valid()) {
                report("VFX kernel references an unavailable CPU event buffer");
                continue;
            }
            sink.setStorageBuffer(binding.stage, m_cpuEventBuffer, binding.access);
            continue;
        }
        if (binding.index >= system.buffers.size() || !system.buffers[binding.index].handle.valid()) {
            report("VFX kernel references an unavailable buffer");
            continue;
        }
        sink.setStorageBuffer(binding.stage, system.buffers[binding.index].handle, binding.access);
    }
}

void EffectImpl::bindKernelTextures(CommandSink& sink, const AssetProgram& program) {
    for (const AssetBinding& binding : program.bindings) {
        if (binding.kind != AssetBindingKind::Texture) {
            continue;
        }
        if (binding.index >= m_asset->properties.size()) {
            report("VFX kernel references an unknown texture property");
            continue;
        }
        const AssetProperty& property = m_asset->properties[binding.index];
        const LutRuntime* lut = nullptr;
        if (property.type == PropertyType::Curve) {
            lut = &m_curveLut;
        } else if (property.type == PropertyType::Gradient) {
            lut = &m_gradientLut;
        }
        if (lut == nullptr || !lut->texture.valid() || !lut->sampler.valid()) {
            report("VFX kernel references an unavailable LUT texture");
            continue;
        }
        sink.setTexture(binding.stage, lut->sampler, lut->texture);
    }
}

void EffectImpl::dispatchKernel(CommandSink& sink, std::uint32_t systemIndex, std::uint32_t programIndex) {
    const AssetProgram& program = m_asset->programs[programIndex];
    bindKernelBuffers(sink, systemIndex, program);
    bindKernelTextures(sink, program);
    const ProgramHandle handle = m_programs[programIndex];
    if (!handle.valid()) {
        report("VFX kernel program is invalid");
        return;
    }
    sink.dispatch(handle, program.groups[0], program.groups[1], program.groups[2]);
    ++m_runtime.m_diagnostics.dispatches;
    if (program.task == kTaskSortBitonic || program.task == kTaskSortPrepass ||
        program.task == kTaskSortMerge) {
        ++m_runtime.m_diagnostics.sortPasses;
    }
}

void EffectImpl::uploadCpuEvents() {
    if (!m_cpuEventBuffer.valid() || m_cpuEventData.empty()) {
        return;
    }
    const auto storeWord = [&](std::size_t wordIndex, std::uint32_t value) {
        const std::size_t offset = wordIndex * 4u;
        std::memcpy(m_cpuEventData.data() + offset, &value, sizeof(value));
    };
    for (std::size_t linkIndex = 0; linkIndex < m_asset->eventLinks.size(); ++linkIndex) {
        const AssetEventLink& link = m_asset->eventLinks[linkIndex];
        if (!link.cpu) {
            continue;
        }
        const std::size_t offset = link.bufferOffsetWords;
        const std::size_t words = 5u + link.capacity + link.capacity * 16u;
        if ((offset + words) * 4u > m_cpuEventData.size()) {
            report("VFX CPU event buffer range is out of bounds");
            return;
        }
        std::fill(m_cpuEventData.begin() + static_cast<std::ptrdiff_t>(offset * 4u),
                  m_cpuEventData.begin() + static_cast<std::ptrdiff_t>((offset + words) * 4u),
                  std::byte{0});
        const std::vector<CpuEventRecord>& pending = m_cpuPending[linkIndex];
        storeWord(offset, static_cast<std::uint32_t>(pending.size()));
        storeWord(offset + 1u, m_cpuEventTotal[linkIndex]);
        storeWord(offset + 3u, m_cpuEventDropped[linkIndex]);
        for (std::size_t eventIndex = 0; eventIndex < pending.size(); ++eventIndex) {
            storeWord(offset + 5u + eventIndex, static_cast<std::uint32_t>(eventIndex));
            for (std::uint32_t slot = 0; slot < 4u; ++slot) {
                for (std::uint32_t component = 0; component < 4u; ++component) {
                    const float value = pending[eventIndex].payload[slot][component];
                    std::uint32_t bits = 0;
                    std::memcpy(&bits, &value, sizeof(bits));
                    storeWord(link.payloadOffsetWords + eventIndex * 16u + slot * 4u + component, bits);
                }
            }
        }
    }
    if (!m_device.updateBuffer(m_cpuEventBuffer, m_cpuEventData)) {
        report("failed to upload VFX CPU event buffer");
        return;
    }
    for (std::vector<CpuEventRecord>& pending : m_cpuPending) {
        pending.clear();
    }
}

void EffectImpl::encodeSimulation(CommandSink& sink, const FrameContext& frame) {
    const bool stepFrame = m_stepFrame;
    if (!m_needsReset && (!m_playing || (m_paused && !stepFrame))) {
        return;
    }
    const bool simulate = m_playing && (!m_paused || stepFrame);
    if (m_needsReset) {
        m_aliveEstimate = 0;
        m_nextParticleId = 0;
        for (SystemRuntime& system : m_systems) {
            system.aliveEstimate = 0;
        }
    }

    const bool hasEvents = !m_asset->eventLinks.empty();
    if (hasEvents) {
        m_runtime.m_diagnostics.eventCount += static_cast<std::uint32_t>(m_asset->eventLinks.size());
        if (m_needsReset && m_eventBuffer.valid()) {
            if (m_eventResetProgram != kInvalidIndex) {
                sink.beginPass({"VFX Event Reset", true, false, false});
                dispatchKernel(sink, 0u, m_eventResetProgram);
                sink.endPass();
            } else {
                report("VFX event reset program is missing");
            }
        }
        uploadCpuEvents();
        if (m_cpuEventBuffer.valid()) {
            if (m_eventCopyProgram != kInvalidIndex) {
                sink.beginPass({"VFX CPU Event Upload", true, false, false});
                dispatchKernel(sink, 0u, m_eventCopyProgram);
                sink.endPass();
            } else {
                report("VFX event copy program is missing");
            }
        }
    }
    const auto encodeSystem = [&](std::size_t systemIndex) {
        SystemRuntime& system = m_systems[systemIndex];
        const std::string passName = "VFX " + m_asset->systems[systemIndex].name + " Simulation";
        sink.beginPass({passName, true, false, false});
        setUniforms(sink, static_cast<std::uint32_t>(systemIndex), simulate, frame);
        for (const std::uint32_t programIndex : system.kernels) {
            const AssetProgram& program = m_asset->programs[programIndex];
            if (program.task == static_cast<std::uint8_t>(kTaskDeadListInit) && !m_needsReset) {
                continue;
            }
            if (program.task == static_cast<std::uint8_t>(kTaskSortPrepass)) {
                const std::uint32_t chunks =
                    (m_asset->systems[systemIndex].capacity + kSortChunkSize - 1u) / kSortChunkSize;
                for (std::uint32_t chunk = 0u; chunk < chunks; ++chunk) {
                    const Vec4 ids{static_cast<float>(m_nextParticleId),
                                   static_cast<float>(systemIndex), static_cast<float>(chunk), 0.0f};
                    sink.setUniform(m_idsUniform, &ids);
                    dispatchKernel(sink, static_cast<std::uint32_t>(systemIndex), programIndex);
                }
                continue;
            }
            dispatchKernel(sink, static_cast<std::uint32_t>(systemIndex), programIndex);
            if (simulate && program.task == static_cast<std::uint8_t>(kTaskSpawner)) {
                m_nextParticleId += system.spawnCount;
            }
        }
        if (simulate) {
            system.aliveEstimate =
                std::min(m_asset->systems[systemIndex].capacity, system.aliveEstimate + system.spawnCount);
        }
        sink.endPass();
    };

    if (!hasEvents) {
        for (std::size_t systemIndex = 0; systemIndex < m_systems.size(); ++systemIndex) {
            encodeSystem(systemIndex);
        }
    } else {
        for (std::size_t systemIndex = 0; systemIndex < m_systems.size(); ++systemIndex) {
            if (!m_systemHasEventInput[systemIndex]) {
                encodeSystem(systemIndex);
            }
        }
        if (m_eventPrefixProgram != kInvalidIndex) {
            sink.beginPass({"VFX Event Prefix Sum", true, false, false});
            dispatchKernel(sink, 0u, m_eventPrefixProgram);
            sink.endPass();
        } else {
            report("VFX event prefix program is missing");
        }
        for (std::size_t systemIndex = 0; systemIndex < m_systems.size(); ++systemIndex) {
            if (m_systemHasEventInput[systemIndex]) {
                encodeSystem(systemIndex);
            }
        }
    }

    m_needsReset = false;
    m_stepFrame = false;
    m_aliveEstimate = 0;
    for (const SystemRuntime& system : m_systems) {
        m_aliveEstimate += system.aliveEstimate;
    }
    m_runtime.m_diagnostics.particles = 0;
    for (const std::unique_ptr<Effect>& effect : m_runtime.m_effects) {
        const auto* impl = dynamic_cast<const EffectImpl*>(effect.get());
        if (impl != nullptr) {
            m_runtime.m_diagnostics.particles += impl->aliveEstimate();
        }
    }
}void EffectImpl::bindOutputBuffers(CommandSink& sink, std::uint32_t systemIndex,
                                   const AssetProgram& program) {
    const SystemRuntime& system = m_systems[systemIndex];
    for (const AssetBinding& binding : program.vertexBindings) {
        if (binding.kind == AssetBindingKind::Texture) {
            continue;
        }
        if (binding.index >= system.buffers.size() || !system.buffers[binding.index].handle.valid()) {
            report("VFX output references an unavailable vertex buffer");
            continue;
        }
        sink.setStorageBuffer(binding.stage, system.buffers[binding.index].handle, binding.access);
    }
}

void EffectImpl::bindOutputTexture(CommandSink& sink, const FrameContext& frame,
                                   const AssetProgram& program) {
    TextureHandle texture = m_whiteTexture;
    for (const AssetBinding& binding : program.fragmentBindings) {
        if (binding.kind != AssetBindingKind::Texture) {
            continue;
        }
        if (binding.index == kSceneDepthBindingIndex) {
            const TextureHandle depth = frame.sceneDepth.valid() ? frame.sceneDepth : m_whiteTexture;
            sink.setTexture(binding.stage, m_sceneDepthSampler, depth);
            continue;
        }
        if (binding.index < m_asset->properties.size() &&
            m_asset->properties[binding.index].type == PropertyType::Texture) {
            const TextureHandle configured = m_propertyTextures[binding.index];
            if (configured.valid()) {
                texture = configured;
            }
        }
        sink.setTexture(binding.stage, m_mainSampler, texture);
    }
}

void EffectImpl::encodeOutput(CommandSink& sink, const FrameContext& frame) {
    if (!m_playing || m_needsReset) {
        return;
    }
    const Mat4 invView = inverseMatrix(frame.view);
    const Vec4 cameraPosition{frame.cameraPosition.x, frame.cameraPosition.y, frame.cameraPosition.z, 1.0f};
    for (std::size_t systemIndex = 0; systemIndex < m_systems.size(); ++systemIndex) {
        const SystemRuntime& system = m_systems[systemIndex];
        const Mat4 world = m_asset->systems[systemIndex].space == SystemSpace::Local ? m_transform
                                                                                     : Mat4::identity();
        for (const std::uint32_t programIndex : system.outputs) {
            const AssetProgram& program = m_asset->programs[programIndex];
            const std::string passName = "VFX " + m_asset->systems[systemIndex].name + " Output";
            sink.beginPass({passName, false, program.softParticles, false});
            sink.setUniform(m_worldUniform, &world);
            if (program.softParticles) {
                const float width = std::max(frame.screenSize.x, 1.0f);
                const float height = std::max(frame.screenSize.y, 1.0f);
                const Vec4 softParams{program.softDistance, 1.0f / width, 1.0f / height, 0.0f};
                sink.setUniform(m_softParamsUniform, &softParams);
            }
            const bool lit = program.lit && m_runtime.m_shading != nullptr;
            if (lit) {
                for (std::size_t i = 0; i < m_propertyUniforms.size(); ++i) {
                    if (m_propertyUniforms[i].valid()) {
                        sink.setUniform(m_propertyUniforms[i], m_propertyValues[i].data());
                    }
                }
                LitSettings settings;
                settings.softDistance = program.softDistance;
                m_runtime.m_shading->bindLighting(sink, frame, settings);
            } else {
                sink.setUniform(m_invViewUniform, &invView);
                sink.setUniform(m_viewProjUniform, &frame.viewProjection);
                sink.setUniform(m_cameraPosUniform, &cameraPosition);
                if (program.softParticles) {
                    const Mat4 invViewProj = inverseMatrix(frame.viewProjection);
                    sink.setUniform(m_viewUniform, &frame.view);
                    sink.setUniform(m_invViewProjUniform, &invViewProj);
                }
            }
            bindOutputBuffers(sink, static_cast<std::uint32_t>(systemIndex), program);
            bindOutputTexture(sink, frame, program);
            DrawState state;
            state.depthTest = true;
            state.depthWrite = false;
            state.cullBackFace = false;
            state.blend = program.blend;
            state.depthFunc = CompareFunc::LessEqual;
            sink.setState(state);
            if (programIndex < m_programs.size() && m_programs[programIndex].valid() &&
                system.buffers[kIndirectBuffer].handle.valid()) {
                sink.drawIndirect(m_programs[programIndex], system.buffers[kIndirectBuffer].handle, 0, 1);
                ++m_runtime.m_diagnostics.draws;
            }
            sink.endPass();
        }
    }
}

void EffectImpl::createLutTextures(std::string& error) {
    const auto create = [&](LutRuntime& lut, std::string_view name, std::string_view samplerName) {
        if (lut.properties.empty() || !error.empty()) {
            return;
        }
        std::vector<std::byte> data;
        data.reserve(lut.properties.size() * kLutRowPitch);
        for (const std::uint32_t propertyIndex : lut.properties) {
            if (propertyIndex >= m_lutData.size() || m_lutData[propertyIndex].size() != kLutRowPitch) {
                error = "invalid VFX LUT row for property '" +
                        std::string(m_asset->properties[propertyIndex].name) + "'";
                return;
            }
            data.insert(data.end(), m_lutData[propertyIndex].begin(), m_lutData[propertyIndex].end());
        }
        TextureDesc desc;
        desc.width = static_cast<std::uint16_t>(kLutSampleCount);
        desc.height = static_cast<std::uint16_t>(lut.properties.size());
        desc.format = TextureFormat::RGBA16F;
        desc.initialData = data.data();
        desc.dataSize = static_cast<std::uint32_t>(data.size());
        desc.name = name;
        lut.texture = m_device.createTexture(desc);
        if (!lut.texture.valid()) {
            error = "failed to create VFX " + std::string(name) + " texture";
            return;
        }
        lut.sampler = m_device.findSampler(samplerName);
        if (!lut.sampler.valid()) {
            error = "failed to create VFX " + std::string(name) + " sampler";
        }
    };
    create(m_curveLut, "vfx.lut.curves", "s_vfxCurveLut");
    create(m_gradientLut, "vfx.lut.gradients", "s_vfxGradientLut");
}

void EffectImpl::updateLutTexture(LutRuntime& lut) {
    if (!lut.texture.valid()) {
        report("VFX LUT texture is unavailable");
        return;
    }
    std::vector<std::byte> data;
    data.reserve(lut.properties.size() * kLutRowPitch);
    for (const std::uint32_t propertyIndex : lut.properties) {
        if (propertyIndex >= m_lutData.size() || m_lutData[propertyIndex].size() != kLutRowPitch) {
            report("VFX LUT row is invalid");
            return;
        }
        data.insert(data.end(), m_lutData[propertyIndex].begin(), m_lutData[propertyIndex].end());
    }
    if (!m_device.updateTexture(lut.texture, data, kLutRowPitch)) {
        report("failed to update VFX LUT texture");
    }
}

void EffectImpl::updateLut(std::size_t propertyIndex) {
    if (propertyIndex >= m_lutData.size()) {
        return;
    }
    const AssetProperty& property = m_asset->properties[propertyIndex];
    if (property.type == PropertyType::Curve) {
        updateLutTexture(m_curveLut);
    } else if (property.type == PropertyType::Gradient) {
        updateLutTexture(m_gradientLut);
    }
}

void EffectImpl::report(std::string_view message) { m_runtime.reportError(message); }

Runtime::Runtime(Device& device, ShadingProvider* shading) : m_device(device), m_shading(shading) {}

Runtime::~Runtime() {
    m_effects.clear();
    m_assets.clear();
}

std::shared_ptr<const Asset> Runtime::loadAsset(const std::filesystem::path& path, std::string* error) {
    std::string localError;
    std::shared_ptr<const Asset> asset = loadAssetBinaryFile(path, localError);
    if (asset == nullptr) {
        if (error != nullptr) {
            *error = localError;
        }
        reportError(localError);
        return nullptr;
    }
    m_assets.push_back(asset);
    if (error != nullptr) {
        error->clear();
    }
    return asset;
}

Effect* Runtime::createEffect(const Asset& asset, const EffectDesc& desc, std::string* error) {
    std::shared_ptr<const Asset> owned;
    for (const std::shared_ptr<const Asset>& candidate : m_assets) {
        if (candidate.get() == &asset) {
            owned = candidate;
            break;
        }
    }
    if (owned == nullptr) {
        owned = std::make_shared<const Asset>(asset);
        m_assets.push_back(owned);
    }
    std::string localError;
    auto effect = std::make_unique<EffectImpl>(*this, owned, desc, localError);
    if (!localError.empty()) {
        if (error != nullptr) {
            *error = localError;
        }
        reportError(localError);
        pruneAssets();
        return nullptr;
    }
    Effect* result = effect.get();
    m_effects.push_back(std::move(effect));
    if (error != nullptr) {
        error->clear();
    }
    refreshDiagnostics();
    return result;
}

Effect* Runtime::reloadEffect(Effect* effect, const Asset& asset, const EffectDesc& desc,
                              std::string* error) {
    if (effect != nullptr) {
        const auto existing = std::find_if(m_effects.begin(), m_effects.end(),
                                           [effect](const std::unique_ptr<Effect>& candidate) {
                                               return candidate.get() == effect;
                                           });
        if (existing == m_effects.end()) {
            constexpr std::string_view message = "effect is not owned by this runtime";
            if (error != nullptr) {
                *error = message;
            }
            reportError(message);
            pruneAssets();
            return nullptr;
        }
    }

    Effect* replacement = createEffect(asset, desc, error);
    if (replacement == nullptr) {
        pruneAssets();
        return nullptr;
    }
    if (effect == nullptr) {
        return replacement;
    }
    for (auto iterator = m_effects.begin(); iterator != m_effects.end(); ++iterator) {
        if (iterator->get() == effect) {
            m_effects.erase(iterator);
            break;
        }
    }
    refreshDiagnostics();
    pruneAssets();
    if (error != nullptr) {
        error->clear();
    }
    return replacement;
}

Effect* Runtime::reloadEffect(Effect* effect, const std::filesystem::path& path, const EffectDesc& desc,
                              std::string* error) {
    const std::shared_ptr<const Asset> asset = loadAsset(path, error);
    if (asset == nullptr) {
        return nullptr;
    }
    return reloadEffect(effect, *asset, desc, error);
}

void Runtime::destroyEffect(Effect* effect) {
    if (effect == nullptr) {
        return;
    }
    for (auto iterator = m_effects.begin(); iterator != m_effects.end(); ++iterator) {
        if (iterator->get() == effect) {
            m_effects.erase(iterator);
            refreshDiagnostics();
            pruneAssets();
            return;
        }
    }
}

void Runtime::refreshDiagnostics() {
    m_diagnostics.effects = static_cast<std::uint32_t>(m_effects.size());
    m_diagnostics.systems = 0;
    m_diagnostics.particles = 0;
    for (const std::unique_ptr<Effect>& current : m_effects) {
        const auto* impl = dynamic_cast<const EffectImpl*>(current.get());
        if (impl != nullptr) {
            m_diagnostics.systems += impl->systemCount();
            m_diagnostics.particles += impl->aliveEstimate();
        }
    }
}

void Runtime::pruneAssets() {
    m_assets.erase(
        std::remove_if(m_assets.begin(), m_assets.end(),
                       [this](const std::shared_ptr<const Asset>& candidate) {
                           for (const std::unique_ptr<Effect>& effect : m_effects) {
                               const auto* impl = dynamic_cast<const EffectImpl*>(effect.get());
                               if (impl != nullptr && impl->assetPtr() == candidate.get()) {
                                   return false;
                               }
                           }
                           return true;
                       }),
        m_assets.end());
}

void Runtime::update(float deltaSeconds) {
    m_simulationMs = 0.0f;
    m_outputMs = 0.0f;
    m_diagnostics.simulationMs = 0.0f;
    m_diagnostics.outputMs = 0.0f;
    const auto start = std::chrono::steady_clock::now();
    for (const std::unique_ptr<Effect>& effect : m_effects) {
        auto* impl = dynamic_cast<EffectImpl*>(effect.get());
        if (impl != nullptr) {
            impl->update(deltaSeconds);
        }
    }
    const auto end = std::chrono::steady_clock::now();
    m_updateMs = std::chrono::duration<float, std::milli>(end - start).count();
    std::uint32_t systems = 0;
    std::uint32_t particles = 0;
    for (const std::unique_ptr<Effect>& effect : m_effects) {
        const auto* impl = dynamic_cast<const EffectImpl*>(effect.get());
        if (impl != nullptr) {
            systems += impl->systemCount();
            particles += impl->aliveEstimate();
        }
    }
    m_diagnostics.effects = static_cast<std::uint32_t>(m_effects.size());
    m_diagnostics.systems = systems;
    m_diagnostics.particles = particles;
    m_diagnostics.updateMs = m_updateMs;
    m_diagnostics.encodeMs = 0.0f;
    m_diagnostics.totalMs = m_updateMs;
}

void Runtime::encodeSimulation(CommandSink& sink, const FrameContext& frame) {
    const auto start = std::chrono::steady_clock::now();
    m_diagnostics.dispatches = 0;
    m_diagnostics.draws = 0;
    m_diagnostics.sortPasses = 0;
    m_diagnostics.eventCount = 0;
    for (const std::unique_ptr<Effect>& effect : m_effects) {
        auto* impl = dynamic_cast<EffectImpl*>(effect.get());
        if (impl != nullptr) {
            impl->encodeSimulation(sink, frame);
        }
    }
    const auto end = std::chrono::steady_clock::now();
    m_simulationMs = std::chrono::duration<float, std::milli>(end - start).count();
    m_encodeMs = m_simulationMs + m_outputMs;
    m_diagnostics.simulationMs = m_simulationMs;
    m_diagnostics.encodeMs = m_encodeMs;
    m_diagnostics.totalMs = m_updateMs + m_encodeMs;
}

void Runtime::encodeOutput(CommandSink& sink, const FrameContext& frame) {
    const auto start = std::chrono::steady_clock::now();
    m_diagnostics.draws = 0;
    for (const std::unique_ptr<Effect>& effect : m_effects) {
        auto* impl = dynamic_cast<EffectImpl*>(effect.get());
        if (impl != nullptr) {
            impl->encodeOutput(sink, frame);
        }
    }
    const auto end = std::chrono::steady_clock::now();
    m_outputMs = std::chrono::duration<float, std::milli>(end - start).count();
    m_encodeMs = m_simulationMs + m_outputMs;
    m_diagnostics.outputMs = m_outputMs;
    m_diagnostics.encodeMs = m_encodeMs;
    m_diagnostics.totalMs = m_updateMs + m_encodeMs;
}

void Runtime::setErrorCallback(std::function<void(std::string_view)> callback) {
    m_errorCallback = std::move(callback);
}

bool Runtime::hasLitOutputs() const {
    for (const std::shared_ptr<const Asset>& asset : m_assets) {
        if (asset == nullptr) {
            continue;
        }
        for (const AssetProgram& program : asset->programs) {
            if (program.output && program.lit) {
                return true;
            }
        }
    }
    return false;
}

Diagnostics Runtime::diagnostics() const { return m_diagnostics; }

void Runtime::reportError(std::string_view message) {
    if (message.empty() || message == m_lastError) {
        return;
    }
    m_lastError.assign(message);
    if (m_errorCallback) {
        m_errorCallback(message);
    }
}

} // namespace vfx
