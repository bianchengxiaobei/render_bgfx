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
constexpr std::uint8_t kTaskFillIndirect = 5;
constexpr std::uint8_t kTaskOutputUpdate = 4;
constexpr std::uint8_t kTaskSortReset = 16;
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

float decodeFloat(std::uint32_t value) {
    const std::uint32_t mask = ((value >> 31u) - 1u) | 0x80000000u;
    return std::bit_cast<float>(value ^ mask);
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

struct BatchIndirectRuntime {
    std::vector<BufferRuntime> buffers;
    std::uint32_t serial = 0;
};

struct SortOutputRuntime {
    SortMode mode = SortMode::None;
    BufferRuntime indirection;
    BufferRuntime kvp;
    BufferRuntime renderCount;
    BufferRuntime indirect;
    BatchIndirectRuntime activeIndirect;
    std::uint32_t validGeneration = 0;
};

struct SortBufferSet {
    BufferHandle indirection{};
    BufferHandle kvp{};
    BufferHandle renderCount{};
    BufferHandle indirect{};
};

struct SystemRuntime {
    std::vector<BufferRuntime> buffers;
    std::vector<SortOutputRuntime> sortOutputs;
    std::vector<std::uint32_t> kernels;
    std::vector<std::uint32_t> outputs;
    std::vector<std::uint32_t> burstFired;
    BatchIndirectRuntime activeIndirect;
    BatchIndirectRuntime shadowIndirect;
    std::uint32_t fillIndirectProgram = kInvalidIndex;
    double time = 0.0;
    double spawnAccumulator = 0.0;
    std::uint32_t spawnCount = 0;
    std::uint32_t aliveEstimate = 0;
    Bounds gpuBounds{};
    bool gpuBoundsValid = false;
    std::array<std::uint32_t, 6> boundsReadback{};
    ReadbackHandle boundsReadbackHandle{};
    bool boundsReadbackPending = false;
};

struct OutputRuntimeRef {
    std::uint32_t system = 0;
    std::uint32_t programIndex = 0;
    std::int32_t sortPriority = 0;
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
    struct InstanceRuntime {
        Mat4 world = Mat4::identity();
        Mat4 previousWorld = Mat4::identity();
        bool transformHistoryValid = false;
        bool culled = false;
        Bounds bounds{};
    };

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
    void setInstanceTransforms(std::span<const Mat4> worlds) override;
    void setCulled(bool culled) override;
    void setInstanceCulled(std::size_t index, bool culled) override;
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
    Bounds visibleBounds() const override;
    Bounds instanceBounds(std::size_t index) const override;
    std::size_t instanceCount() const override;
    bool supportsEffectInstancing() const override;
    bool isCulled() const override;
    bool isInstanceCulled(std::size_t index) const override;
    std::uint32_t outputBatchCount() const override;
    std::uint32_t outputBatchSize() const override;

    void update(float deltaSeconds);
    void encodeSimulation(CommandSink& sink, const FrameContext& frame);
    void encodeOutput(CommandSink& sink, const FrameContext& frame);
    void encodeOutputBatch(CommandSink& sink, const FrameContext& frame, std::uint32_t batchIndex);
    void encodeMotionVectors(CommandSink& sink, const FrameContext& frame);
    void encodeShadow(CommandSink& sink, const Mat4& shadowViewProjection);
    void refreshDrawInstancing();
    void ensureInstanceTransformBuffer();
    void uploadInstanceTransforms();
    void uploadInstanceActiveList();
    void bindInstanceTransforms(CommandSink& sink, BufferHandle fallback);
    void setInstanceDrawParams(CommandSink& sink, std::uint32_t totalCount, std::uint32_t activeCount,
                               std::uint32_t activeBase, bool activeIndirection);
    std::vector<BufferHandle> prepareActiveIndirect(CommandSink& sink, std::uint32_t systemIndex,
                                                    SortMode sortMode, const SortBufferSet& buffers);
    std::vector<BufferHandle> prepareShadowIndirect(CommandSink& sink, std::uint32_t systemIndex,
                                                    const SortBufferSet& buffers);
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
    void refreshVisibleBounds();
    void pollAutomaticBounds();
    void resetCpuState();
    void setUniforms(CommandSink& sink, std::uint32_t systemIndex, bool simulate,
                     const FrameContext& frame, SortMode sortMode, bool useSortUniforms);
    void encodeOutputDraws(CommandSink& sink, const FrameContext& frame, const InstanceRuntime& instance,
                           bool batch, std::uint32_t firstBatch, std::uint32_t batchCount);
    void bindKernelBuffers(CommandSink& sink, std::uint32_t systemIndex, const AssetProgram& program,
                           const SortBufferSet* overrides = nullptr);
    void bindKernelTextures(CommandSink& sink, const AssetProgram& program);
    void dispatchKernel(CommandSink& sink, std::uint32_t systemIndex, std::uint32_t programIndex,
                        const SortBufferSet* overrides = nullptr);
    void ensureOutputSort(CommandSink& sink, std::uint32_t systemIndex, SortMode sortMode,
                          const FrameContext& frame);
    SortBufferSet outputBuffers(std::uint32_t systemIndex, SortMode sortMode) const;
    void bindOutputBuffers(CommandSink& sink, std::uint32_t systemIndex, const AssetProgram& program,
                           const SortBufferSet& buffers);
    void bindOutputTexture(CommandSink& sink, const FrameContext& frame, const AssetProgram& program);
    void createLutTextures(std::string& error);
    void updateLutTexture(LutRuntime& lut);
    void updateLut(std::size_t propertyIndex);
    void report(std::string_view message);

    Runtime& m_runtime;
    Device& m_device;
    std::shared_ptr<const Asset> m_asset;
    Mat4 m_transform = Mat4::identity();
    std::vector<InstanceRuntime> m_instances;
    BufferHandle m_instanceTransforms{};
    std::uint32_t m_instanceTransformCapacity = 0;
    std::uint32_t m_batchInstanceCount = 1;
    std::uint32_t m_outputInstanceCount = 1;
    std::uint32_t m_outputBatchSize = 1;
    std::uint32_t m_outputBatchCount = 1;
    std::uint32_t m_shadowBatchSize = 1;
    std::uint32_t m_shadowBatchCount = 1;
    std::uint32_t m_activeInstanceBase = 0;
    std::vector<std::uint32_t> m_activeInstances;
    bool m_outputBatch = false;
    bool m_activeIndirection = false;
    bool m_activeArgsRequired = false;
    bool m_shadowArgsRequired = false;
    std::uint32_t m_activeArgsSerial = 1;
    std::uint32_t m_shadowArgsSerial = 1;
    bool m_activeInstancesDirty = false;
    bool m_transformSetThisFrame = false;
    bool m_supportsEffectInstancing = false;
    float m_playRate = 1.0f;
    std::uint32_t m_randomSeed = 0;
    std::uint32_t m_instanceBatchLimit = 0;
    bool m_playing = false;
    bool m_paused = false;
    bool m_stopped = true;
    bool m_stepPending = false;
    bool m_stepFrame = false;
    bool m_needsReset = true;
    bool m_hasBoundsOverride = false;
    Bounds m_boundsOverride{};
    Bounds m_bounds{};
    Bounds m_visibleBounds{};
    float m_time = 0.0f;
    float m_deltaTime = 0.0f;
    std::uint32_t m_nextParticleId = 0;
    std::uint32_t m_aliveEstimate = 0;

    std::vector<SystemRuntime> m_systems;
    std::vector<ProgramHandle> m_programs;
    std::vector<OutputRuntimeRef> m_outputOrder;
    std::uint32_t m_sortGeneration = 1;
    std::vector<bool> m_hasPerOutputSort;
    FrameContext m_lastFrame;
    bool m_lastFrameValid = false;
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
    UniformHandle m_boundsParamsUniform{};
    UniformHandle m_boundsRightUniform{};
    UniformHandle m_boundsUpUniform{};
    UniformHandle m_instanceParamsUniform{};
    UniformHandle m_invViewUniform{};
    UniformHandle m_viewUniform{};
    UniformHandle m_viewProjUniform{};
    UniformHandle m_invViewProjUniform{};
    UniformHandle m_cameraPosUniform{};
    UniformHandle m_sortWorldUniform{};
    UniformHandle m_sortCameraPosUniform{};
    UniformHandle m_sortInvViewUniform{};
    UniformHandle m_softParamsUniform{};
    UniformHandle m_motionParamsUniform{};
    UniformHandle m_prevWorldUniform{};
    UniformHandle m_shadowViewProjUniform{};
    UniformHandle m_shadowParamsUniform{};
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
      m_instanceBatchLimit(desc.instanceBatchLimit), m_playing(desc.playOnAwake),
      m_stopped(!desc.playOnAwake), m_hasBoundsOverride(desc.hasBoundsOverride),
      m_boundsOverride(desc.boundsOverride) {
    if (m_asset == nullptr) {
        error = "effect asset is null";
        return;
    }
    InstanceRuntime instance;
    instance.world = desc.transform;
    instance.previousWorld = desc.transform;
    m_instances.push_back(instance);
    m_supportsEffectInstancing = true;
    for (const AssetSystem& system : m_asset->systems) {
        if (system.space != SystemSpace::Local) {
            m_supportsEffectInstancing = false;
            break;
        }
    }
    if (m_supportsEffectInstancing) {
        for (const AssetProgram& program : m_asset->programs) {
            if (program.output && program.sortMode != SortMode::None &&
                program.sortMode != SortMode::YoungestInFront) {
                m_supportsEffectInstancing = false;
                break;
            }
        }
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
    m_boundsParamsUniform = m_device.findUniform("u_vfxBoundsParams", UniformType::Vec4);
    m_boundsRightUniform = m_device.findUniform("u_vfxBoundsRight", UniformType::Vec4);
    m_boundsUpUniform = m_device.findUniform("u_vfxBoundsUp", UniformType::Vec4);
    m_instanceParamsUniform = m_device.findUniform("u_vfxInstanceParams", UniformType::Vec4);
    m_invViewUniform = m_device.findUniform("u_global_invView", UniformType::Mat4);
    m_viewUniform = m_device.findUniform("u_global_view", UniformType::Mat4);
    m_viewProjUniform = m_device.findUniform("u_global_viewProj", UniformType::Mat4);
    m_invViewProjUniform = m_device.findUniform("u_global_invViewProj", UniformType::Mat4);
    m_cameraPosUniform = m_device.findUniform("u_global_cameraPos", UniformType::Vec4);
    m_sortWorldUniform = m_device.findUniform("u_vfxSortWorld", UniformType::Mat4);
    m_sortCameraPosUniform = m_device.findUniform("u_vfxSortCameraPos", UniformType::Vec4);
    m_sortInvViewUniform = m_device.findUniform("u_vfxSortInvView", UniformType::Mat4);
    m_softParamsUniform = m_device.findUniform("u_vfxSoftParams", UniformType::Vec4);
    m_mainSampler = m_device.findSampler("s_vfxMain");
    m_sceneDepthSampler = m_device.findSampler("s_vfxSceneDepth");
    if (error.empty() &&
        (!m_paramsUniform.valid() || !m_idsUniform.valid() || !m_worldUniform.valid() ||
         !m_invViewUniform.valid() || !m_viewUniform.valid() || !m_viewProjUniform.valid() ||
         !m_invViewProjUniform.valid() || !m_cameraPosUniform.valid() ||
         !m_sortWorldUniform.valid() || !m_sortCameraPosUniform.valid() ||
         !m_sortInvViewUniform.valid() || !m_softParamsUniform.valid() ||
         !m_mainSampler.valid() || !m_sceneDepthSampler.valid())) {
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

EffectImpl::~EffectImpl() {
    for (SystemRuntime& system : m_systems) {
        if (system.boundsReadbackPending) {
            m_device.releaseReadback(system.boundsReadbackHandle);
        }
    }
    releaseResources();
}

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
    m_hasPerOutputSort.assign(m_asset->systems.size(), false);
    const std::size_t currentBackend = backendIndex(m_device.backend());
    for (std::size_t systemIndex = 0; systemIndex < m_asset->systems.size(); ++systemIndex) {
        const AssetSystem& assetSystem = m_asset->systems[systemIndex];
        SystemRuntime& system = m_systems[systemIndex];
        system.buffers.resize(kBufferKindCount);
        for (const AssetBuffer& assetBuffer : assetSystem.buffers) {
            BufferUsage usage = BufferUsage::Storage;
            if (assetBuffer.kind == kAttributesBuffer || assetBuffer.kind == kIndirectionBuffer ||
                assetBuffer.kind == kRenderCountBuffer || assetBuffer.kind == kRenderKVPBuffer ||
                assetBuffer.kind == kSortScratchBuffer) {
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
            if (assetProgram.task == kTaskFillIndirect) {
                m_systems[assetProgram.system].fillIndirectProgram = static_cast<std::uint32_t>(programIndex);
            }
            if (assetProgram.sortMode != SortMode::None) {
                m_hasPerOutputSort[assetProgram.system] = true;
            }
        }
    }
    for (std::size_t systemIndex = 0; systemIndex < m_asset->systems.size(); ++systemIndex) {
        const AssetSystem& assetSystem = m_asset->systems[systemIndex];
        SystemRuntime& system = m_systems[systemIndex];
        const BufferRuntime& indirectionTemplate = system.buffers[kIndirectionBuffer];
        const BufferRuntime& kvpTemplate = system.buffers[kRenderKVPBuffer];
        const BufferRuntime& countTemplate = system.buffers[kRenderCountBuffer];
        const BufferRuntime& indirectTemplate = system.buffers[kIndirectBuffer];
        if (!indirectionTemplate.handle.valid() || !kvpTemplate.handle.valid() ||
            !countTemplate.handle.valid() || !indirectTemplate.handle.valid()) {
            continue;
        }
        for (const AssetProgram& assetProgram : m_asset->programs) {
            if (assetProgram.output || assetProgram.system != systemIndex ||
                assetProgram.sortMode == SortMode::None ||
                assetProgram.sortMode == assetSystem.sortMode) {
                continue;
            }
            const auto existing = std::find_if(
                system.sortOutputs.begin(), system.sortOutputs.end(),
                [&](const SortOutputRuntime& list) { return list.mode == assetProgram.sortMode; });
            if (existing != system.sortOutputs.end()) {
                continue;
            }
            const std::string listName =
                "vfx." + assetSystem.name + ".sorted." + toString(assetProgram.sortMode);
            SortOutputRuntime list;
            list.mode = assetProgram.sortMode;
            const auto createListBuffer = [&](BufferRuntime& buffer, BufferUsage usage,
                                              const BufferRuntime& templateBuffer,
                                              std::string_view suffix) {
                const std::string bufferName = listName + std::string(suffix);
                BufferDesc desc;
                desc.usage = usage;
                desc.size = templateBuffer.size;
                desc.stride = templateBuffer.stride;
                desc.name = bufferName;
                buffer.size = templateBuffer.size;
                buffer.stride = templateBuffer.stride;
                buffer.handle = m_device.createBuffer(desc);
                if (!buffer.handle.valid() && error.empty()) {
                    error = "failed to create VFX sorted output buffer '" + bufferName + "'";
                }
            };
            createListBuffer(list.indirection, BufferUsage::StorageVertex, indirectionTemplate, "");
            createListBuffer(list.kvp, BufferUsage::StorageVertex, kvpTemplate, ".kvp");
            createListBuffer(list.renderCount, BufferUsage::StorageVertex, countTemplate, ".count");
            createListBuffer(list.indirect, BufferUsage::Indirect, indirectTemplate, ".indirect");
            system.sortOutputs.push_back(std::move(list));
        }
    }
    m_outputOrder.clear();
    for (std::size_t programIndex = 0; programIndex < m_asset->programs.size(); ++programIndex) {
        const AssetProgram& program = m_asset->programs[programIndex];
        if (!program.output) {
            continue;
        }
        m_outputOrder.push_back(OutputRuntimeRef{program.system, static_cast<std::uint32_t>(programIndex),
                                                 program.sortPriority});
    }
    std::stable_sort(m_outputOrder.begin(), m_outputOrder.end(),
                     [](const OutputRuntimeRef& lhs, const OutputRuntimeRef& rhs) {
                         return lhs.sortPriority < rhs.sortPriority;
                     });
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
        for (BufferRuntime& buffer : system.activeIndirect.buffers) {
            if (buffer.handle.valid()) {
                m_device.destroyBuffer(buffer.handle);
                buffer.handle = {};
            }
        }
        for (BufferRuntime& buffer : system.shadowIndirect.buffers) {
            if (buffer.handle.valid()) {
                m_device.destroyBuffer(buffer.handle);
                buffer.handle = {};
            }
        }
        for (SortOutputRuntime& list : system.sortOutputs) {
            for (BufferRuntime& buffer : list.activeIndirect.buffers) {
                if (buffer.handle.valid()) {
                    m_device.destroyBuffer(buffer.handle);
                    buffer.handle = {};
                }
            }
            for (BufferRuntime* buffer : {&list.indirection, &list.kvp, &list.renderCount, &list.indirect}) {
                if (buffer->handle.valid()) {
                    m_device.destroyBuffer(buffer->handle);
                    buffer->handle = {};
                }
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
    if (m_instanceTransforms.valid()) {
        m_device.destroyBuffer(m_instanceTransforms);
        m_instanceTransforms = {};
    }
    m_instanceTransformCapacity = 0;
    m_batchInstanceCount = 1;
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
    for (InstanceRuntime& instance : m_instances) {
        instance.previousWorld = instance.world;
        instance.transformHistoryValid = false;
    }
    m_transformSetThisFrame = false;
    for (SystemRuntime& system : m_systems) {
        if (system.boundsReadbackPending) {
            m_device.releaseReadback(system.boundsReadbackHandle);
            system.boundsReadbackHandle = {};
            system.boundsReadbackPending = false;
        }
        system.gpuBoundsValid = false;
    }
    refreshBounds();
    refreshDrawInstancing();
    uploadInstanceTransforms();
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
    for (SystemRuntime& system : m_systems) {
        for (SortOutputRuntime& list : system.sortOutputs) {
            list.validGeneration = 0u;
        }
    }
    m_sortGeneration = 1u;
    m_lastFrameValid = false;
}

void EffectImpl::setTransform(const Mat4& world) {
    setInstanceTransforms(std::span<const Mat4>(&world, 1));
}

void EffectImpl::setInstanceTransforms(std::span<const Mat4> worlds) {
    if (worlds.empty()) {
        return;
    }
    if (worlds.size() > 1u && !m_supportsEffectInstancing) {
        report("VFX effect instancing requires local-space systems and non-camera-dependent outputs");
        return;
    }
    if (worlds.size() != m_instances.size()) {
        const std::size_t previousSize = m_instances.size();
        m_instances.resize(worlds.size());
        for (std::size_t i = previousSize; i < m_instances.size(); ++i) {
            m_instances[i].world = worlds[i];
            m_instances[i].previousWorld = worlds[i];
            m_instances[i].transformHistoryValid = false;
        }
    }
    for (std::size_t i = 0; i < worlds.size(); ++i) {
        InstanceRuntime& instance = m_instances[i];
        if (!m_transformSetThisFrame) {
            instance.previousWorld = instance.transformHistoryValid ? instance.world : worlds[i];
            instance.transformHistoryValid = true;
        }
        instance.world = worlds[i];
    }
    m_transformSetThisFrame = true;
    m_transform = m_instances.front().world;
    refreshBounds();
    refreshDrawInstancing();
    uploadInstanceTransforms();
}

void EffectImpl::setCulled(bool culled) {
    for (InstanceRuntime& instance : m_instances) {
        instance.culled = culled;
    }
    refreshVisibleBounds();
    refreshDrawInstancing();
}

void EffectImpl::setInstanceCulled(std::size_t index, bool culled) {
    if (index < m_instances.size()) {
        m_instances[index].culled = culled;
        refreshVisibleBounds();
        refreshDrawInstancing();
    }
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

Bounds EffectImpl::visibleBounds() const {
    return m_visibleBounds;
}

Bounds EffectImpl::instanceBounds(std::size_t index) const {
    if (m_hasBoundsOverride) {
        return m_boundsOverride;
    }
    return index < m_instances.size() ? m_instances[index].bounds : Bounds{};
}

std::size_t EffectImpl::instanceCount() const { return m_instances.size(); }

bool EffectImpl::supportsEffectInstancing() const { return m_supportsEffectInstancing; }

bool EffectImpl::isCulled() const {
    if (m_instances.empty()) {
        return true;
    }
    return std::all_of(m_instances.begin(), m_instances.end(),
                       [](const InstanceRuntime& instance) { return instance.culled; });
}

bool EffectImpl::isInstanceCulled(std::size_t index) const {
    return index >= m_instances.size() || m_instances[index].culled;
}

std::uint32_t EffectImpl::outputBatchCount() const {
    return m_outputBatch ? m_outputBatchCount : 1u;
}

std::uint32_t EffectImpl::outputBatchSize() const { return m_outputBatchSize; }

void EffectImpl::refreshBounds() {
    Bounds bounds{};
    bool hasBounds = false;
    for (InstanceRuntime& instance : m_instances) {
        Bounds instanceBounds{};
        bool hasInstanceBounds = false;
        for (std::size_t i = 0; i < m_asset->systems.size(); ++i) {
            const AssetSystem& assetSystem = m_asset->systems[i];
            Bounds systemBounds = assetSystem.bounds;
            if (assetSystem.boundsMode == BoundsMode::Automatic && i < m_systems.size() &&
                m_systems[i].gpuBoundsValid) {
                systemBounds = m_systems[i].gpuBounds;
            }
            if (assetSystem.boundsMode == BoundsMode::Automatic) {
                const Vec3 padding = assetSystem.boundsPadding;
                systemBounds = Bounds{Vec3{systemBounds.min.x - padding.x, systemBounds.min.y - padding.y,
                                           systemBounds.min.z - padding.z},
                                      Vec3{systemBounds.max.x + padding.x, systemBounds.max.y + padding.y,
                                           systemBounds.max.z + padding.z},
                                      systemBounds.valid};
            }
            if (assetSystem.space == SystemSpace::Local) {
                const Vec3 min = systemBounds.min;
                const Vec3 max = systemBounds.max;
                const std::array<Vec3, 8> corners = {
                    Vec3{min.x, min.y, min.z}, Vec3{max.x, min.y, min.z}, Vec3{min.x, max.y, min.z},
                    Vec3{max.x, max.y, min.z}, Vec3{min.x, min.y, max.z}, Vec3{max.x, min.y, max.z},
                    Vec3{min.x, max.y, max.z}, Vec3{max.x, max.y, max.z}};
                Vec3 transformedMin = transformPoint(instance.world, corners[0]);
                Vec3 transformedMax = transformedMin;
                for (const Vec3& corner : corners) {
                    const Vec3 transformed = transformPoint(instance.world, corner);
                    transformedMin = minVec3(transformedMin, transformed);
                    transformedMax = maxVec3(transformedMax, transformed);
                }
                systemBounds = Bounds{transformedMin, transformedMax, true};
            }
            if (!hasInstanceBounds) {
                instanceBounds = systemBounds;
                hasInstanceBounds = true;
            } else {
                instanceBounds = Bounds{minVec3(instanceBounds.min, systemBounds.min),
                                         maxVec3(instanceBounds.max, systemBounds.max), true};
            }
        }
        instance.bounds = instanceBounds;
        if (!hasBounds) {
            bounds = instanceBounds;
            hasBounds = true;
        } else {
            bounds = Bounds{minVec3(bounds.min, instanceBounds.min), maxVec3(bounds.max, instanceBounds.max),
                            true};
        }
    }
    m_bounds = bounds;
    refreshVisibleBounds();
}

void EffectImpl::refreshVisibleBounds() {
    Bounds bounds{};
    bool hasBounds = false;
    for (std::size_t i = 0; i < m_instances.size(); ++i) {
        if (m_instances[i].culled) {
            continue;
        }
        const Bounds instanceBounds = this->instanceBounds(i);
        if (!instanceBounds.valid) {
            continue;
        }
        if (!hasBounds) {
            bounds = instanceBounds;
            hasBounds = true;
        } else {
            bounds = Bounds{minVec3(bounds.min, instanceBounds.min),
                            maxVec3(bounds.max, instanceBounds.max), true};
        }
    }
    m_visibleBounds = hasBounds ? bounds : Bounds{};
}

void EffectImpl::ensureInstanceTransformBuffer() {
    const std::uint32_t count =
        static_cast<std::uint32_t>(std::max<std::size_t>(m_instances.size(), 1u));
    if (count <= 1u) {
        if (m_instanceTransforms.valid()) {
            m_device.destroyBuffer(m_instanceTransforms);
            m_instanceTransforms = {};
        }
        m_instanceTransformCapacity = count;
        m_activeInstanceBase = 0u;
        m_activeInstances.clear();
        m_activeInstancesDirty = false;
        return;
    }
    if (m_instanceTransforms.valid() && m_instanceTransformCapacity == count) {
        return;
    }
    if (m_instanceTransforms.valid()) {
        m_device.destroyBuffer(m_instanceTransforms);
        m_instanceTransforms = {};
    }
    const std::uint32_t activeSlots = (count + 31u) / 32u;
    const std::uint32_t totalSlots = count + activeSlots;
    const std::vector<float> initial(totalSlots * 32u, 0.0f);
    BufferDesc desc;
    desc.usage = BufferUsage::StorageVertex;
    desc.size = totalSlots * 32u * static_cast<std::uint32_t>(sizeof(float));
    desc.stride = 32u * static_cast<std::uint32_t>(sizeof(float));
    desc.initialData = initial.data();
    desc.name = "vfx.instance.transforms";
    m_instanceTransforms = m_device.createBuffer(desc);
    m_instanceTransformCapacity = count;
    m_activeInstanceBase = count * 32u;
    if (!m_instanceTransforms.valid()) {
        report("failed to create VFX instance transform buffer");
    }
}

void EffectImpl::uploadInstanceTransforms() {
    if (!m_instanceTransforms.valid() || m_instances.size() <= 1u) {
        return;
    }
    const std::uint32_t count = static_cast<std::uint32_t>(m_instances.size());
    const std::uint32_t activeSlots = (count + 31u) / 32u;
    std::vector<std::uint32_t> data((count + activeSlots) * 32u, 0u);
    for (std::size_t i = 0; i < m_instances.size(); ++i) {
        std::memcpy(data.data() + i * 32u, m_instances[i].world.data(), 16u * sizeof(float));
        std::memcpy(data.data() + i * 32u + 16u, m_instances[i].previousWorld.data(),
                    16u * sizeof(float));
    }
    for (std::size_t i = 0; i < m_activeInstances.size() && i < count; ++i) {
        data[m_activeInstanceBase + i] = m_activeInstances[i];
    }
    if (!m_device.updateBuffer(m_instanceTransforms, std::as_bytes(std::span(data.data(), data.size())))) {
        report("failed to upload VFX instance transform buffer");
        return;
    }
    m_activeInstancesDirty = false;
}

void EffectImpl::uploadInstanceActiveList() {
    if (!m_instanceTransforms.valid() || !m_activeIndirection || !m_activeInstancesDirty ||
        m_activeInstances.empty()) {
        return;
    }
    if (!m_device.updateBuffer(
            m_instanceTransforms,
            std::as_bytes(std::span(m_activeInstances.data(), m_activeInstances.size())),
            m_activeInstanceBase * static_cast<std::uint32_t>(sizeof(std::uint32_t)))) {
        report("failed to upload VFX active instance list");
        return;
    }
    m_activeInstancesDirty = false;
}

void EffectImpl::refreshDrawInstancing() {
    ensureInstanceTransformBuffer();
    m_activeInstances.clear();
    for (std::size_t i = 0; i < m_instances.size(); ++i) {
        if (!m_instances[i].culled) {
            m_activeInstances.push_back(static_cast<std::uint32_t>(i));
        }
    }
    const bool instancing = m_supportsEffectInstancing && m_instances.size() > 1u &&
                            m_instanceTransforms.valid();
    m_batchInstanceCount = instancing ? static_cast<std::uint32_t>(m_instances.size()) : 1u;
    m_outputBatch = instancing && m_activeInstances.size() > 1u;
    m_outputInstanceCount =
        m_outputBatch ? static_cast<std::uint32_t>(m_activeInstances.size()) : 1u;
    m_outputBatchSize = 1u;
    m_outputBatchCount = 1u;
    if (m_outputBatch) {
        const std::uint32_t limit = m_instanceBatchLimit == 0u
                                        ? m_outputInstanceCount
                                        : std::min(m_instanceBatchLimit, m_outputInstanceCount);
        m_outputBatchSize = std::max(limit, 1u);
        m_outputBatchCount =
            (m_outputInstanceCount + m_outputBatchSize - 1u) / m_outputBatchSize;
    }
    const bool split = m_outputBatchCount > 1u;
    m_activeIndirection = m_outputBatch && (m_activeInstances.size() < m_instances.size() || split);
    m_activeArgsRequired = instancing && !m_activeInstances.empty() &&
                           (m_activeInstances.size() < m_instances.size() || split);
    ++m_activeArgsSerial;
    if (m_activeArgsSerial == 0u) {
        m_activeArgsSerial = 1u;
    }
    m_shadowBatchSize = 1u;
    m_shadowBatchCount = 1u;
    m_shadowArgsRequired = false;
    if (instancing && m_instanceBatchLimit != 0u &&
        m_instances.size() > static_cast<std::size_t>(m_instanceBatchLimit)) {
        m_shadowBatchSize = m_instanceBatchLimit;
        m_shadowBatchCount =
            (static_cast<std::uint32_t>(m_instances.size()) + m_shadowBatchSize - 1u) /
            m_shadowBatchSize;
        m_shadowArgsRequired = m_shadowBatchCount > 1u;
    }
    ++m_shadowArgsSerial;
    if (m_shadowArgsSerial == 0u) {
        m_shadowArgsSerial = 1u;
    }
    m_activeInstancesDirty = true;
}

void EffectImpl::bindInstanceTransforms(CommandSink& sink, BufferHandle fallback) {
    if (m_instanceTransforms.valid()) {
        sink.setStorageBuffer(15, m_instanceTransforms, Access::Read);
    } else if (fallback.valid()) {
        sink.setStorageBuffer(15, fallback, Access::Read);
    }
}

void EffectImpl::setInstanceDrawParams(CommandSink& sink, std::uint32_t totalCount,
                                       std::uint32_t activeCount, std::uint32_t activeBase,
                                       bool activeIndirection) {
    if (m_instanceParamsUniform.valid()) {
        const Vec4 params{static_cast<float>(totalCount), static_cast<float>(activeCount),
                          static_cast<float>(activeBase), activeIndirection ? 1.0f : 0.0f};
        sink.setUniform(m_instanceParamsUniform, &params);
    }
}

std::vector<BufferHandle> EffectImpl::prepareActiveIndirect(CommandSink& sink,
                                                            std::uint32_t systemIndex,
                                                            SortMode sortMode,
                                                            const SortBufferSet& buffers) {
    if (!m_activeArgsRequired || systemIndex >= m_systems.size()) {
        return {};
    }
    SystemRuntime& system = m_systems[systemIndex];
    const AssetSystem& assetSystem = m_asset->systems[systemIndex];
    BatchIndirectRuntime* active = nullptr;
    BufferRuntime* source = nullptr;
    std::string name;
    if (sortMode == assetSystem.sortMode) {
        active = &system.activeIndirect;
        source = &system.buffers[kIndirectBuffer];
        name = "vfx." + assetSystem.name + ".active.indirect";
    } else {
        for (SortOutputRuntime& list : system.sortOutputs) {
            if (list.mode != sortMode) {
                continue;
            }
            active = &list.activeIndirect;
            source = &list.indirect;
            name = "vfx." + assetSystem.name + ".sorted." + toString(sortMode) + ".active.indirect";
            break;
        }
    }
    if (active == nullptr || source == nullptr || !source->handle.valid() ||
        system.fillIndirectProgram == kInvalidIndex) {
        return {};
    }
    const std::uint32_t batchCount = std::max(m_outputBatchCount, 1u);
    if (active->buffers.size() < batchCount) {
        active->buffers.resize(batchCount);
    }
    const auto handlesValid = [&]() {
        for (std::uint32_t batchIndex = 0; batchIndex < batchCount; ++batchIndex) {
            if (!active->buffers[batchIndex].handle.valid()) {
                return false;
            }
        }
        return true;
    };
    if (active->serial == m_activeArgsSerial && handlesValid()) {
        std::vector<BufferHandle> handles;
        handles.reserve(batchCount);
        for (std::uint32_t batchIndex = 0; batchIndex < batchCount; ++batchIndex) {
            handles.push_back(active->buffers[batchIndex].handle);
        }
        return handles;
    }
    for (std::uint32_t batchIndex = 0; batchIndex < batchCount; ++batchIndex) {
        BufferRuntime& batch = active->buffers[batchIndex];
        if (batch.handle.valid()) {
            continue;
        }
        BufferDesc desc;
        desc.usage = BufferUsage::Indirect;
        desc.size = source->size;
        desc.stride = source->stride;
        const std::string batchName =
            batchIndex == 0u ? name : name + "." + std::to_string(batchIndex);
        desc.name = batchName;
        batch.size = source->size;
        batch.stride = source->stride;
        batch.handle = m_device.createBuffer(desc);
        if (!batch.handle.valid()) {
            report("failed to create VFX active indirect buffer");
            return {};
        }
    }
    active->serial = 0;
    const std::uint32_t activeCount = static_cast<std::uint32_t>(m_activeInstances.size());
    sink.beginPass({"VFX " + assetSystem.name + " Active Indirect", true, false, false});
    for (std::uint32_t batchIndex = 0; batchIndex < batchCount; ++batchIndex) {
        const std::uint32_t batchOffset = batchIndex * m_outputBatchSize;
        if (batchOffset >= activeCount) {
            break;
        }
        const std::uint32_t batchInstances =
            std::min(m_outputBatchSize, activeCount - batchOffset);
        setInstanceDrawParams(sink, batchInstances, batchInstances, m_activeInstanceBase, false);
        SortBufferSet activeBuffers = buffers;
        activeBuffers.indirect = active->buffers[batchIndex].handle;
        dispatchKernel(sink, systemIndex, system.fillIndirectProgram, &activeBuffers);
    }
    sink.endPass();
    active->serial = m_activeArgsSerial;
    std::vector<BufferHandle> handles;
    handles.reserve(batchCount);
    for (std::uint32_t batchIndex = 0; batchIndex < batchCount; ++batchIndex) {
        handles.push_back(active->buffers[batchIndex].handle);
    }
    return handles;
}

std::vector<BufferHandle> EffectImpl::prepareShadowIndirect(CommandSink& sink,
                                                            std::uint32_t systemIndex,
                                                            const SortBufferSet& buffers) {
    if (!m_shadowArgsRequired || systemIndex >= m_systems.size()) {
        return {};
    }
    SystemRuntime& system = m_systems[systemIndex];
    const AssetSystem& assetSystem = m_asset->systems[systemIndex];
    BufferRuntime* source = &system.buffers[kIndirectBuffer];
    if (!source->handle.valid() || system.fillIndirectProgram == kInvalidIndex) {
        return {};
    }
    const std::uint32_t batchCount = std::max(m_shadowBatchCount, 1u);
    BatchIndirectRuntime* indirect = &system.shadowIndirect;
    if (indirect->buffers.size() < batchCount) {
        indirect->buffers.resize(batchCount);
    }
    const auto handlesValid = [&]() {
        for (std::uint32_t batchIndex = 0; batchIndex < batchCount; ++batchIndex) {
            if (!indirect->buffers[batchIndex].handle.valid()) {
                return false;
            }
        }
        return true;
    };
    if (indirect->serial == m_shadowArgsSerial && handlesValid()) {
        std::vector<BufferHandle> handles;
        handles.reserve(batchCount);
        for (std::uint32_t batchIndex = 0; batchIndex < batchCount; ++batchIndex) {
            handles.push_back(indirect->buffers[batchIndex].handle);
        }
        return handles;
    }
    const std::string baseName = "vfx." + assetSystem.name + ".shadow.indirect";
    for (std::uint32_t batchIndex = 0; batchIndex < batchCount; ++batchIndex) {
        BufferRuntime& batch = indirect->buffers[batchIndex];
        if (batch.handle.valid()) {
            continue;
        }
        BufferDesc desc;
        desc.usage = BufferUsage::Indirect;
        desc.size = source->size;
        desc.stride = source->stride;
        const std::string batchName =
            batchIndex == 0u ? baseName : baseName + "." + std::to_string(batchIndex);
        desc.name = batchName;
        batch.size = source->size;
        batch.stride = source->stride;
        batch.handle = m_device.createBuffer(desc);
        if (!batch.handle.valid()) {
            report("failed to create VFX shadow indirect buffer");
            return {};
        }
    }
    indirect->serial = 0;
    const std::uint32_t totalInstances = static_cast<std::uint32_t>(m_instances.size());
    sink.beginPass({"VFX " + assetSystem.name + " Shadow Indirect", true, false, false});
    for (std::uint32_t batchIndex = 0; batchIndex < batchCount; ++batchIndex) {
        const std::uint32_t batchOffset = batchIndex * m_shadowBatchSize;
        if (batchOffset >= totalInstances) {
            break;
        }
        const std::uint32_t batchInstances =
            std::min(m_shadowBatchSize, totalInstances - batchOffset);
        setInstanceDrawParams(sink, batchInstances, batchInstances, 0u, false);
        SortBufferSet activeBuffers = buffers;
        activeBuffers.indirect = indirect->buffers[batchIndex].handle;
        dispatchKernel(sink, systemIndex, system.fillIndirectProgram, &activeBuffers);
    }
    sink.endPass();
    indirect->serial = m_shadowArgsSerial;
    std::vector<BufferHandle> handles;
    handles.reserve(batchCount);
    for (std::uint32_t batchIndex = 0; batchIndex < batchCount; ++batchIndex) {
        handles.push_back(indirect->buffers[batchIndex].handle);
    }
    return handles;
}

void EffectImpl::pollAutomaticBounds() {
    bool changed = false;
    for (std::size_t i = 0; i < m_asset->systems.size() && i < m_systems.size(); ++i) {
        const AssetSystem& assetSystem = m_asset->systems[i];
        if (assetSystem.boundsMode != BoundsMode::Automatic) {
            continue;
        }
        SystemRuntime& system = m_systems[i];
        if (system.boundsReadbackPending) {
            if (!m_device.readbackReady(system.boundsReadbackHandle)) {
                continue;
            }
            if (m_device.copyReadback(system.boundsReadbackHandle,
                                      std::as_writable_bytes(std::span(system.boundsReadback)))) {
                const std::uint32_t minWord = system.boundsReadback[0];
                const std::uint32_t maxWord = system.boundsReadback[3];
                if (minWord <= maxWord) {
                    system.gpuBounds = Bounds{
                        Vec3{decodeFloat(system.boundsReadback[0]), decodeFloat(system.boundsReadback[1]),
                             decodeFloat(system.boundsReadback[2])},
                        Vec3{decodeFloat(system.boundsReadback[3]), decodeFloat(system.boundsReadback[4]),
                             decodeFloat(system.boundsReadback[5])},
                        true};
                    system.gpuBoundsValid = true;
                } else {
                    system.gpuBoundsValid = false;
                }
                changed = true;
            }
            m_device.releaseReadback(system.boundsReadbackHandle);
            system.boundsReadbackHandle = {};
            system.boundsReadbackPending = false;
        }
        if (system.boundsReadbackPending) {
            continue;
        }
        if (kBoundsBuffer < system.buffers.size() && system.buffers[kBoundsBuffer].handle.valid() &&
            system.buffers[kBoundsBuffer].size >= sizeof(system.boundsReadback)) {
            system.boundsReadbackHandle = m_device.readBufferAsync(
                system.buffers[kBoundsBuffer].handle, static_cast<std::uint32_t>(sizeof(system.boundsReadback)));
            system.boundsReadbackPending = system.boundsReadbackHandle.valid();
        }
    }
    if (changed) {
        refreshBounds();
    }
}

void EffectImpl::update(float deltaSeconds) {
    m_transformSetThisFrame = false;
    pollAutomaticBounds();
    if (isCulled() && (m_asset->cullingFlags & kCullSimulation) != 0u) {
        m_deltaTime = 0.0f;
        return;
    }
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
                             const FrameContext& frame, SortMode sortMode, bool useSortUniforms) {
    const SystemRuntime& system = m_systems[systemIndex];
    const AssetSystem& assetSystem = m_asset->systems[systemIndex];
    const Vec4 params{static_cast<float>(system.time), simulate ? m_deltaTime : 0.0f,
                      simulate ? static_cast<float>(system.spawnCount) : 0.0f,
                      static_cast<float>(m_randomSeed)};
    sink.setUniform(m_paramsUniform, &params);
    const Vec4 ids{static_cast<float>(m_nextParticleId), static_cast<float>(systemIndex), 0.0f, 0.0f};
    sink.setUniform(m_idsUniform, &ids);
    if (assetSystem.boundsMode == BoundsMode::Automatic && m_boundsParamsUniform.valid() &&
        m_boundsRightUniform.valid() && m_boundsUpUniform.valid()) {
        const Vec4 boundsParams{1.0f, 0.0f, 0.0f, 0.0f};
        const Vec4 boundsRight{frame.cameraRight.x, frame.cameraRight.y, frame.cameraRight.z, 0.0f};
        const Vec4 boundsUp{frame.cameraUp.x, frame.cameraUp.y, frame.cameraUp.z, 0.0f};
        sink.setUniform(m_boundsParamsUniform, &boundsParams);
        sink.setUniform(m_boundsRightUniform, &boundsRight);
        sink.setUniform(m_boundsUpUniform, &boundsUp);
    }
    setInstanceDrawParams(sink, m_batchInstanceCount, m_batchInstanceCount, m_activeInstanceBase,
                          false);
    if (sortMode == SortMode::Distance || sortMode == SortMode::CameraDepth) {
        const Mat4 world =
            assetSystem.space == SystemSpace::Local ? m_transform : Mat4::identity();
        const Vec4 cameraPosition{frame.cameraPosition.x, frame.cameraPosition.y,
                                  frame.cameraPosition.z, 1.0f};
        const UniformHandle worldUniform =
            useSortUniforms ? m_sortWorldUniform : m_worldUniform;
        const UniformHandle cameraPosUniform =
            useSortUniforms ? m_sortCameraPosUniform : m_cameraPosUniform;
        sink.setUniform(worldUniform, &world);
        sink.setUniform(cameraPosUniform, &cameraPosition);
        if (sortMode == SortMode::CameraDepth) {
            const Mat4 invView = inverseMatrix(frame.view);
            const UniformHandle invViewUniform =
                useSortUniforms ? m_sortInvViewUniform : m_invViewUniform;
            sink.setUniform(invViewUniform, &invView);
        }
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
                                   const AssetProgram& program, const SortBufferSet* overrides) {
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
        if (binding.index >= system.buffers.size()) {
            report("VFX kernel references an unavailable buffer");
            continue;
        }
        BufferHandle buffer = system.buffers[binding.index].handle;
        if (overrides != nullptr) {
            if (binding.index == kIndirectionBuffer && overrides->indirection.valid()) {
                buffer = overrides->indirection;
            } else if (binding.index == kRenderKVPBuffer && overrides->kvp.valid()) {
                buffer = overrides->kvp;
            } else if (binding.index == kRenderCountBuffer && overrides->renderCount.valid()) {
                buffer = overrides->renderCount;
            } else if (binding.index == kIndirectBuffer && overrides->indirect.valid()) {
                buffer = overrides->indirect;
            }
        }
        if (!buffer.valid()) {
            report("VFX kernel references an unavailable buffer");
            continue;
        }
        sink.setStorageBuffer(binding.stage, buffer, binding.access);
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

void EffectImpl::dispatchKernel(CommandSink& sink, std::uint32_t systemIndex,
                                std::uint32_t programIndex, const SortBufferSet* overrides) {
    const AssetProgram& program = m_asset->programs[programIndex];
    bindKernelBuffers(sink, systemIndex, program, overrides);
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

SortBufferSet EffectImpl::outputBuffers(std::uint32_t systemIndex, SortMode sortMode) const {
    SortBufferSet buffers;
    if (systemIndex >= m_systems.size()) {
        return buffers;
    }
    const SystemRuntime& system = m_systems[systemIndex];
    buffers.indirection = system.buffers[kIndirectionBuffer].handle;
    buffers.kvp = system.buffers[kRenderKVPBuffer].handle;
    buffers.renderCount = system.buffers[kRenderCountBuffer].handle;
    buffers.indirect = system.buffers[kIndirectBuffer].handle;
    if (sortMode != m_asset->systems[systemIndex].sortMode) {
        for (const SortOutputRuntime& list : system.sortOutputs) {
            if (list.mode == sortMode && list.indirection.handle.valid()) {
                buffers.indirection = list.indirection.handle;
                buffers.kvp = list.kvp.handle;
                buffers.renderCount = list.renderCount.handle;
                buffers.indirect = list.indirect.handle;
                break;
            }
        }
    }
    return buffers;
}

void EffectImpl::ensureOutputSort(CommandSink& sink, std::uint32_t systemIndex, SortMode sortMode,
                                  const FrameContext& frame) {
    if (sortMode == SortMode::None || systemIndex >= m_systems.size()) {
        return;
    }
    const AssetSystem& assetSystem = m_asset->systems[systemIndex];
    if (assetSystem.sortMode == SortMode::None) {
        return;
    }
    if (sortMode == assetSystem.sortMode) {
        return;
    }

    SystemRuntime& system = m_systems[systemIndex];
    SortOutputRuntime* list = nullptr;
    for (SortOutputRuntime& candidate : system.sortOutputs) {
        if (candidate.mode == sortMode) {
            list = &candidate;
            break;
        }
    }
    if (list == nullptr || !list->indirection.handle.valid() || !list->kvp.handle.valid() ||
        !list->renderCount.handle.valid() || !list->indirect.handle.valid() ||
        list->validGeneration == m_sortGeneration) {
        return;
    }

    const SortBufferSet buffers = outputBuffers(systemIndex, sortMode);
    bool hasPipeline = false;
    sink.beginPass({"VFX " + assetSystem.name + " Output Sort", true, false, false});
    setUniforms(sink, systemIndex, false, frame, sortMode,
                m_hasPerOutputSort[systemIndex]);
    for (const std::uint32_t programIndex : system.kernels) {
        const AssetProgram& program = m_asset->programs[programIndex];
        if (program.sortMode != sortMode) {
            continue;
        }
        if (program.task == static_cast<std::uint8_t>(kTaskSortPrepass)) {
            hasPipeline = true;
            const std::uint32_t chunks = (assetSystem.capacity + kSortChunkSize - 1u) / kSortChunkSize;
            for (std::uint32_t chunk = 0u; chunk < chunks; ++chunk) {
                const Vec4 ids{static_cast<float>(m_nextParticleId), static_cast<float>(systemIndex),
                               static_cast<float>(chunk), 0.0f};
                sink.setUniform(m_idsUniform, &ids);
                dispatchKernel(sink, systemIndex, programIndex, &buffers);
            }
            continue;
        }
        if (program.task == static_cast<std::uint8_t>(kTaskSortReset) ||
            program.task == static_cast<std::uint8_t>(kTaskOutputUpdate) ||
            program.task == static_cast<std::uint8_t>(kTaskSortBitonic) ||
            program.task == static_cast<std::uint8_t>(kTaskSortMerge)) {
            hasPipeline = true;
            dispatchKernel(sink, systemIndex, programIndex, &buffers);
        }
    }
    if (hasPipeline) {
        for (const std::uint32_t programIndex : system.kernels) {
            const AssetProgram& program = m_asset->programs[programIndex];
            if (program.task == static_cast<std::uint8_t>(kTaskFillIndirect)) {
                dispatchKernel(sink, systemIndex, programIndex, &buffers);
                break;
            }
        }
        list->validGeneration = m_sortGeneration;
    }
    sink.endPass();
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
    m_lastFrame = frame;
    m_lastFrameValid = true;
    refreshDrawInstancing();
    if (isCulled() && (m_asset->cullingFlags & kCullSimulation) != 0u) {
        return;
    }
    const bool stepFrame = m_stepFrame;
    if (!m_needsReset && (!m_playing || (m_paused && !stepFrame))) {
        return;
    }
    ++m_sortGeneration;
    if (m_sortGeneration == 0u) {
        m_sortGeneration = 1u;
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
        setUniforms(sink, static_cast<std::uint32_t>(systemIndex), simulate, frame,
                    m_asset->systems[systemIndex].sortMode,
                    m_hasPerOutputSort[systemIndex]);
        const SortMode systemSortMode = m_asset->systems[systemIndex].sortMode;
        for (const std::uint32_t programIndex : system.kernels) {
            const AssetProgram& program = m_asset->programs[programIndex];
            if (program.task == static_cast<std::uint8_t>(kTaskSortReset)) {
                continue;
            }
            if (program.sortMode != SortMode::None && program.sortMode != systemSortMode) {
                continue;
            }
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
                                   const AssetProgram& program, const SortBufferSet& buffers) {
    const SystemRuntime& system = m_systems[systemIndex];
    for (const AssetBinding& binding : program.vertexBindings) {
        if (binding.kind == AssetBindingKind::Texture) {
            continue;
        }
        if (binding.index >= system.buffers.size()) {
            report("VFX output references an unavailable vertex buffer");
            continue;
        }
        BufferHandle buffer = system.buffers[binding.index].handle;
        if (binding.index == kIndirectionBuffer && buffers.indirection.valid()) {
            buffer = buffers.indirection;
        } else if (binding.index == kRenderCountBuffer && buffers.renderCount.valid()) {
            buffer = buffers.renderCount;
        }
        if (!buffer.valid()) {
            report("VFX output references an unavailable vertex buffer");
            continue;
        }
        sink.setStorageBuffer(binding.stage, buffer, binding.access);
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

void EffectImpl::encodeOutputDraws(CommandSink& sink, const FrameContext& frame,
                                   const InstanceRuntime& instance, bool batch,
                                   std::uint32_t firstBatch, std::uint32_t batchCount) {
    const Mat4 invView = inverseMatrix(frame.view);
    const Vec4 cameraPosition{frame.cameraPosition.x, frame.cameraPosition.y, frame.cameraPosition.z, 1.0f};
    const std::uint32_t requiredBatches = batch ? m_outputBatchCount : 1u;
    for (const OutputRuntimeRef& output : m_outputOrder) {
        const std::uint32_t systemIndex = output.system;
        const std::uint32_t programIndex = output.programIndex;
        if (systemIndex >= m_systems.size() || programIndex >= m_asset->programs.size()) {
            continue;
        }
        const SystemRuntime& system = m_systems[systemIndex];
        const AssetProgram& program = m_asset->programs[programIndex];
        const SortBufferSet buffers = outputBuffers(systemIndex, program.sortMode);
        ensureOutputSort(sink, systemIndex, program.sortMode, frame);
        const std::vector<BufferHandle> activeIndirect =
            prepareActiveIndirect(sink, systemIndex, program.sortMode, buffers);
        if (m_activeArgsRequired && activeIndirect.size() != requiredBatches) {
            report("VFX active indirect args are unavailable for an instance batch");
            continue;
        }
        const Mat4 world = m_asset->systems[systemIndex].space == SystemSpace::Local
                               ? instance.world
                               : Mat4::identity();
        const std::string passName = "VFX " + m_asset->systems[systemIndex].name + " Output";
        for (std::uint32_t localBatch = 0; localBatch < batchCount; ++localBatch) {
            const std::uint32_t batchIndex = firstBatch + localBatch;
            if (batch && (batchIndex >= m_outputBatchCount ||
                          batchIndex * m_outputBatchSize >= m_outputInstanceCount)) {
                continue;
            }
            const std::uint32_t batchOffset = batch ? batchIndex * m_outputBatchSize : 0u;
            const std::uint32_t localInstances =
                batch ? std::min(m_outputBatchSize, m_outputInstanceCount - batchOffset) : 1u;
            const std::uint32_t activeBase = m_activeInstanceBase + batchOffset;
            sink.beginPass({passName, false, program.softParticles, false});
            sink.setUniform(m_worldUniform, &world);
            setInstanceDrawParams(sink, batch ? m_batchInstanceCount : 1u, localInstances,
                                  activeBase, batch && m_activeIndirection);
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
            bindOutputBuffers(sink, systemIndex, program, buffers);
            bindInstanceTransforms(sink, system.buffers[kAttributesBuffer].handle);
            bindOutputTexture(sink, frame, program);
            DrawState state;
            state.depthTest = true;
            state.depthWrite = false;
            state.cullBackFace = false;
            state.blend = program.blend;
            state.depthFunc = CompareFunc::LessEqual;
            sink.setState(state);
            const BufferHandle indirect =
                activeIndirect.empty() ? buffers.indirect : activeIndirect[batchIndex];
            if (programIndex < m_programs.size() && m_programs[programIndex].valid() &&
                indirect.valid()) {
                sink.drawIndirect(m_programs[programIndex], indirect, 0, 1);
                ++m_runtime.m_diagnostics.draws;
            }
            sink.endPass();
        }
    }
}

void EffectImpl::encodeOutput(CommandSink& sink, const FrameContext& frame) {
    if (isCulled() || !m_playing || m_needsReset || m_instances.empty()) {
        return;
    }
    uploadInstanceActiveList();
    const bool batch = m_outputBatch;
    const std::size_t drawCount = batch ? 1u : m_instances.size();
    for (std::size_t draw = 0; draw < drawCount; ++draw) {
        const InstanceRuntime& instance = m_instances[batch ? 0u : draw];
        if (!batch && instance.culled) {
            continue;
        }
        encodeOutputDraws(sink, frame, instance, batch, 0u, batch ? m_outputBatchCount : 1u);
    }
}

void EffectImpl::encodeOutputBatch(CommandSink& sink, const FrameContext& frame,
                                   std::uint32_t batchIndex) {
    if (isCulled() || !m_playing || m_needsReset || m_instances.empty()) {
        return;
    }
    if (!m_outputBatch || m_outputBatchCount <= 1u) {
        if (batchIndex == 0u) {
            encodeOutput(sink, frame);
        }
        return;
    }
    if (batchIndex >= m_outputBatchCount) {
        return;
    }
    uploadInstanceActiveList();
    encodeOutputDraws(sink, frame, m_instances.front(), true, batchIndex, 1u);
}

void EffectImpl::encodeMotionVectors(CommandSink& sink, const FrameContext& frame) {
    if (isCulled() || !m_playing || m_needsReset) {
        return;
    }
    uploadInstanceActiveList();
    const ProgramHandle program = m_runtime.motionVectorProgram();
    if (!program.valid()) {
        return;
    }
    if (!m_motionParamsUniform.valid()) {
        m_motionParamsUniform = m_device.findUniform("u_vfxMotionParams", UniformType::Vec4);
    }
    if (!m_motionParamsUniform.valid()) {
        return;
    }
    if (!m_prevWorldUniform.valid()) {
        m_prevWorldUniform = m_device.findUniform("u_vfx_prevWorld", UniformType::Mat4);
    }
    if (!m_prevWorldUniform.valid()) {
        return;
    }

    const bool batch = m_outputBatch;
    const std::size_t drawCount = batch ? 1u : m_instances.size();
    for (std::size_t draw = 0; draw < drawCount; ++draw) {
        const InstanceRuntime& instance = m_instances[batch ? 0u : draw];
        if (!batch && instance.culled) {
            continue;
        }
        for (const OutputRuntimeRef& outputRef : m_outputOrder) {
            const std::uint32_t systemIndex = outputRef.system;
            const std::uint32_t programIndex = outputRef.programIndex;
            if (systemIndex >= m_systems.size() || programIndex >= m_asset->programs.size()) {
                continue;
            }
            const SystemRuntime& system = m_systems[systemIndex];
            const AssetProgram& output = m_asset->programs[programIndex];
            if (!output.generateMotionVector) {
                continue;
            }
            const SortBufferSet buffers = outputBuffers(systemIndex, output.sortMode);
            if (!system.buffers[kAttributesBuffer].handle.valid() ||
                !buffers.indirection.valid() || !buffers.indirect.valid() ||
                !buffers.renderCount.valid()) {
                continue;
            }
            ensureOutputSort(sink, systemIndex, output.sortMode, frame);
            const std::vector<BufferHandle> activeIndirect =
                prepareActiveIndirect(sink, systemIndex, output.sortMode, buffers);
            const std::uint32_t batchCount = batch ? m_outputBatchCount : 1u;
            if (m_activeArgsRequired && activeIndirect.size() != batchCount) {
                report("VFX active indirect args are unavailable for an instance batch");
                continue;
            }
            const bool localSpace = m_asset->systems[systemIndex].space == SystemSpace::Local;
            const Mat4 world = localSpace ? instance.world : Mat4::identity();
            const Mat4 previousWorld = localSpace ? instance.previousWorld : Mat4::identity();
            const float strideWords = static_cast<float>(system.buffers[kAttributesBuffer].stride / 4u);
            const bool reverseSort =
                m_asset->systems[systemIndex].sortMode != SortMode::None &&
                output.sortOrder != m_asset->systems[systemIndex].sortOrder;
            const std::string passName =
                "VFX " + m_asset->systems[systemIndex].name + " Motion Vectors";
            for (std::uint32_t batchIndex = 0; batchIndex < batchCount; ++batchIndex) {
                const std::uint32_t batchOffset = batch ? batchIndex * m_outputBatchSize : 0u;
                if (batch && batchOffset >= m_outputInstanceCount) {
                    continue;
                }
                const std::uint32_t localInstances =
                    batch ? std::min(m_outputBatchSize, m_outputInstanceCount - batchOffset) : 1u;
                const std::uint32_t activeBase = m_activeInstanceBase + batchOffset;
                sink.beginPass({passName, false, false, false});
                sink.setUniform(m_worldUniform, &world);
                sink.setUniform(m_prevWorldUniform, &previousWorld);
                setInstanceDrawParams(sink, batch ? m_batchInstanceCount : 1u, localInstances,
                                      activeBase, batch && m_activeIndirection);
                const Vec4 motionParams{0.001f, output.blend != BlendMode::Opaque ? 1.0f : 0.0f,
                                        reverseSort ? 1.0f : 0.0f, strideWords};
                sink.setUniform(m_motionParamsUniform, &motionParams.x);
                sink.setStorageBuffer(12, system.buffers[kAttributesBuffer].handle, Access::Read);
                sink.setStorageBuffer(13, buffers.indirection, Access::Read);
                sink.setStorageBuffer(14, buffers.renderCount, Access::Read);
                bindInstanceTransforms(sink, system.buffers[kAttributesBuffer].handle);

                TextureHandle texture = m_whiteTexture;
                if (output.textureProperty != kInvalidIndex &&
                    output.textureProperty < m_propertyTextures.size() &&
                    m_propertyTextures[output.textureProperty].valid()) {
                    texture = m_propertyTextures[output.textureProperty];
                }
                sink.setTexture(0, m_mainSampler, texture);

                DrawState state;
                state.depthTest = true;
                state.depthWrite = false;
                state.cullBackFace = false;
                state.blend = BlendMode::Opaque;
                state.depthFunc = CompareFunc::LessEqual;
                sink.setState(state);
                const BufferHandle indirect =
                    activeIndirect.empty() ? buffers.indirect : activeIndirect[batchIndex];
                sink.drawIndirect(program, indirect, 0, 1);
                ++m_runtime.m_diagnostics.draws;
                sink.endPass();
            }
        }
    }
}
void EffectImpl::encodeShadow(CommandSink& sink, const Mat4& shadowViewProjection) {
    if (!m_playing || m_needsReset) {
        return;
    }
    const ProgramHandle program = m_runtime.shadowProgram();
    if (!program.valid()) {
        return;
    }
    if (!m_shadowViewProjUniform.valid()) {
        m_shadowViewProjUniform = m_device.findUniform("u_vfxShadowViewProj", UniformType::Mat4);
    }
    if (!m_shadowParamsUniform.valid()) {
        m_shadowParamsUniform = m_device.findUniform("u_vfxShadowParams", UniformType::Vec4);
    }
    if (!m_shadowViewProjUniform.valid() || !m_shadowParamsUniform.valid()) {
        return;
    }

    const bool batch = m_batchInstanceCount > 1u;
    const bool split = m_shadowArgsRequired;
    const std::uint32_t batchCount = split ? m_shadowBatchCount : 1u;
    const std::uint32_t totalInstances = static_cast<std::uint32_t>(m_instances.size());
    const std::size_t drawCount = batch ? 1u : m_instances.size();
    for (std::size_t draw = 0; draw < drawCount; ++draw) {
        const InstanceRuntime& instance = m_instances[batch ? 0u : draw];
        for (const OutputRuntimeRef& outputRef : m_outputOrder) {
            const std::uint32_t systemIndex = outputRef.system;
            const std::uint32_t programIndex = outputRef.programIndex;
            if (systemIndex >= m_systems.size() || programIndex >= m_asset->programs.size()) {
                continue;
            }
            const SystemRuntime& system = m_systems[systemIndex];
            const AssetProgram& output = m_asset->programs[programIndex];
            if (!output.castShadows) {
                continue;
            }
            const SortBufferSet buffers = outputBuffers(systemIndex, output.sortMode);
            if (!system.buffers[kAttributesBuffer].handle.valid() ||
                !buffers.indirection.valid() || !buffers.indirect.valid() ||
                !buffers.renderCount.valid()) {
                continue;
            }
            if (m_lastFrameValid) {
                ensureOutputSort(sink, systemIndex, output.sortMode, m_lastFrame);
            }
            const std::vector<BufferHandle> shadowIndirect =
                prepareShadowIndirect(sink, systemIndex, buffers);
            if (split && shadowIndirect.size() != batchCount) {
                report("VFX shadow indirect args are unavailable for an instance batch");
                continue;
            }
            const Mat4 world = m_asset->systems[systemIndex].space == SystemSpace::Local
                                   ? instance.world
                                   : Mat4::identity();
            const float strideWords = static_cast<float>(system.buffers[kAttributesBuffer].stride / 4u);
            const bool reverseSort =
                m_asset->systems[systemIndex].sortMode != SortMode::None &&
                output.sortOrder != m_asset->systems[systemIndex].sortOrder;
            const std::string passName =
                "VFX " + m_asset->systems[systemIndex].name + " Shadow";
            for (std::uint32_t batchIndex = 0; batchIndex < batchCount; ++batchIndex) {
                const std::uint32_t batchOffset = split ? batchIndex * m_shadowBatchSize : 0u;
                if (split && batchOffset >= totalInstances) {
                    break;
                }
                const std::uint32_t localInstances =
                    split ? std::min(m_shadowBatchSize, totalInstances - batchOffset)
                          : (batch ? totalInstances : 1u);
                sink.beginPass({passName, false, false, false});
                sink.setUniform(m_worldUniform, &world);
                sink.setUniform(m_shadowViewProjUniform, shadowViewProjection.data());
                setInstanceDrawParams(sink, batch ? totalInstances : 1u, localInstances, batchOffset,
                                      false);
                const Vec4 shadowParams{0.001f, output.blend != BlendMode::Opaque ? 1.0f : 0.0f,
                                        strideWords, reverseSort ? 1.0f : 0.0f};
                sink.setUniform(m_shadowParamsUniform, &shadowParams.x);
                sink.setStorageBuffer(12, system.buffers[kAttributesBuffer].handle, Access::Read);
                sink.setStorageBuffer(13, buffers.indirection, Access::Read);
                sink.setStorageBuffer(14, buffers.renderCount, Access::Read);
                bindInstanceTransforms(sink, system.buffers[kAttributesBuffer].handle);

                TextureHandle texture = m_whiteTexture;
                if (output.textureProperty != kInvalidIndex &&
                    output.textureProperty < m_propertyTextures.size() &&
                    m_propertyTextures[output.textureProperty].valid()) {
                    texture = m_propertyTextures[output.textureProperty];
                }
                sink.setTexture(0, m_mainSampler, texture);

                DrawState state;
                state.depthTest = true;
                state.depthWrite = true;
                state.cullBackFace = false;
                state.blend = BlendMode::Opaque;
                state.depthFunc = CompareFunc::LessEqual;
                sink.setState(state);
                const BufferHandle indirect =
                    shadowIndirect.empty() ? buffers.indirect : shadowIndirect[batchIndex];
                sink.drawIndirect(program, indirect, 0, 1);
                ++m_runtime.m_diagnostics.shadowDraws;
                sink.endPass();
            }
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
    m_diagnostics.shadowDraws = 0;
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
    m_diagnostics.shadowDraws = 0;
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

void Runtime::beginOutputFrame() {
    m_diagnostics.draws = 0;
    m_outputMs = 0.0f;
    m_encodeMs = m_simulationMs;
    m_diagnostics.outputMs = 0.0f;
    m_diagnostics.encodeMs = m_encodeMs;
    m_diagnostics.totalMs = m_updateMs + m_encodeMs;
}

void Runtime::encodeOutput(CommandSink& sink, const FrameContext& frame) {
    const auto start = std::chrono::steady_clock::now();
    beginOutputFrame();
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

void Runtime::encodeOutputEffect(CommandSink& sink, const FrameContext& frame, Effect& effect) {
    const auto start = std::chrono::steady_clock::now();
    auto* impl = dynamic_cast<EffectImpl*>(&effect);
    if (impl != nullptr) {
        impl->encodeOutput(sink, frame);
    }
    const auto end = std::chrono::steady_clock::now();
    m_outputMs += std::chrono::duration<float, std::milli>(end - start).count();
    m_encodeMs = m_simulationMs + m_outputMs;
    m_diagnostics.outputMs = m_outputMs;
    m_diagnostics.encodeMs = m_encodeMs;
    m_diagnostics.totalMs = m_updateMs + m_encodeMs;
}

void Runtime::encodeOutputEffectBatch(CommandSink& sink, const FrameContext& frame, Effect& effect,
                                      std::uint32_t batchIndex) {
    const auto start = std::chrono::steady_clock::now();
    auto* impl = dynamic_cast<EffectImpl*>(&effect);
    if (impl != nullptr) {
        impl->encodeOutputBatch(sink, frame, batchIndex);
    }
    const auto end = std::chrono::steady_clock::now();
    m_outputMs += std::chrono::duration<float, std::milli>(end - start).count();
    m_encodeMs = m_simulationMs + m_outputMs;
    m_diagnostics.outputMs = m_outputMs;
    m_diagnostics.encodeMs = m_encodeMs;
    m_diagnostics.totalMs = m_updateMs + m_encodeMs;
}

void Runtime::encodeMotionVectors(CommandSink& sink, const FrameContext& frame) {
    const auto start = std::chrono::steady_clock::now();
    for (const std::unique_ptr<Effect>& effect : m_effects) {
        auto* impl = dynamic_cast<EffectImpl*>(effect.get());
        if (impl != nullptr) {
            impl->encodeMotionVectors(sink, frame);
        }
    }
    const auto end = std::chrono::steady_clock::now();
    m_outputMs += std::chrono::duration<float, std::milli>(end - start).count();
    m_encodeMs = m_simulationMs + m_outputMs;
    m_diagnostics.outputMs = m_outputMs;
    m_diagnostics.encodeMs = m_encodeMs;
    m_diagnostics.totalMs = m_updateMs + m_encodeMs;
}

void Runtime::encodeShadow(CommandSink& sink, const Mat4& shadowViewProjection) {
    for (const std::unique_ptr<Effect>& effect : m_effects) {
        auto* impl = dynamic_cast<EffectImpl*>(effect.get());
        if (impl != nullptr) {
            impl->encodeShadow(sink, shadowViewProjection);
        }
    }
}

ProgramHandle Runtime::motionVectorProgram() {
    if (!m_motionVectorProgramResolved) {
        m_motionVectorProgramResolved = true;
        if (m_shading != nullptr) {
            m_motionVectorProgram =
                m_shading->resolveProgram(RenderOutputDesc{"vfx.motionVectors", false}, m_device.backend());
        }
    }
    return m_motionVectorProgram;
}

ProgramHandle Runtime::shadowProgram() {
    if (!m_shadowProgramResolved) {
        m_shadowProgramResolved = true;
        if (m_shading != nullptr) {
            m_shadowProgram =
                m_shading->resolveProgram(RenderOutputDesc{"vfx.shadow", false}, m_device.backend());
        }
    }
    return m_shadowProgram;
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

bool Runtime::hasShadowOutputs() const {
    for (const std::shared_ptr<const Asset>& asset : m_assets) {
        if (asset == nullptr) {
            continue;
        }
        for (const AssetProgram& program : asset->programs) {
            if (program.output && program.castShadows) {
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
