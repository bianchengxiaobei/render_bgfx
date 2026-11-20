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

constexpr std::uint8_t kTaskSpawner = 0;
constexpr std::uint8_t kTaskDeadListInit = 1;

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

    Bounds bounds() const override;
    bool isCulled() const override;

    void update(float deltaSeconds);
    void encodeSimulation(CommandSink& sink, const FrameContext& frame);
    void encodeOutput(CommandSink& sink, const FrameContext& frame);
    std::uint32_t aliveEstimate() const { return m_aliveEstimate; }
    std::uint32_t systemCount() const { return static_cast<std::uint32_t>(m_systems.size()); }
    bool needsSimulation() const { return m_needsReset || (m_playing && !m_paused); }
    bool needsOutput() const { return m_playing && !m_needsReset; }

  private:
    void createResources(std::string& error);
    void releaseResources();
    void refreshBounds();
    void resetCpuState();
    void setUniforms(CommandSink& sink, std::uint32_t systemIndex, bool simulate);
    void bindKernelBuffers(CommandSink& sink, std::uint32_t systemIndex, const AssetProgram& program);
    void dispatchKernel(CommandSink& sink, std::uint32_t systemIndex, std::uint32_t programIndex);
    void bindOutputBuffers(CommandSink& sink, std::uint32_t systemIndex, const AssetProgram& program);
    void bindOutputTexture(CommandSink& sink, const AssetProgram& program);
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
    std::vector<TextureHandle> m_lutTextures;
    std::vector<std::vector<std::byte>> m_lutData;
    TextureHandle m_whiteTexture{};
    UniformHandle m_paramsUniform{};
    UniformHandle m_idsUniform{};
    UniformHandle m_worldUniform{};
    UniformHandle m_invViewUniform{};
    UniformHandle m_viewProjUniform{};
    UniformHandle m_cameraPosUniform{};
    SamplerHandle m_mainSampler{};
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
    m_lutTextures.resize(m_asset->properties.size());
    m_lutData.resize(m_asset->properties.size());
    m_propertyUniforms.resize(m_asset->properties.size());
    for (std::size_t i = 0; i < m_asset->properties.size(); ++i) {
        const AssetProperty& property = m_asset->properties[i];
        m_propertyValues[i] = property.defaultFloat;
        if (!property.lut.empty()) {
            m_lutData[i] = property.lut;
            TextureDesc textureDesc;
            textureDesc.width = static_cast<std::uint16_t>(kLutSampleCount);
            textureDesc.height = 1;
            textureDesc.format = TextureFormat::RGBA16F;
            textureDesc.initialData = m_lutData[i].data();
            textureDesc.dataSize = static_cast<std::uint32_t>(m_lutData[i].size());
            const std::string lutName = "vfx.lut." + property.name;
            textureDesc.name = lutName;
            m_lutTextures[i] = m_device.createTexture(textureDesc);
            if (!m_lutTextures[i].valid() && error.empty()) {
                error = "failed to create LUT texture for property '" + property.name + "'";
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
    m_viewProjUniform = m_device.findUniform("u_global_viewProj", UniformType::Mat4);
    m_cameraPosUniform = m_device.findUniform("u_global_cameraPos", UniformType::Vec4);
    m_mainSampler = m_device.findSampler("s_vfxMain");
    if (error.empty() &&
        (!m_paramsUniform.valid() || !m_idsUniform.valid() || !m_worldUniform.valid() ||
         !m_invViewUniform.valid() || !m_viewProjUniform.valid() || !m_cameraPosUniform.valid() ||
         !m_mainSampler.valid())) {
        error = "failed to create VFX runtime uniforms/sampler";
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
    for (const AssetProgram& program : m_asset->programs) {
        if (program.output) {
            if (program.lit && error.empty()) {
                error = "lit VFX output is not supported until V1.4";
            }
            if (program.sortMode != SortMode::None && error.empty()) {
                error = "sorted VFX output is not supported until V1.2";
            }
        }
    }
    refreshBounds();
    resetCpuState();
    m_needsReset = true;
}

EffectImpl::~EffectImpl() { releaseResources(); }

void EffectImpl::createResources(std::string& error) {
    m_systems.resize(m_asset->systems.size());
    m_programs.resize(m_asset->programs.size());
    const std::size_t currentBackend = backendIndex(m_device.backend());
    for (std::size_t systemIndex = 0; systemIndex < m_asset->systems.size(); ++systemIndex) {
        const AssetSystem& assetSystem = m_asset->systems[systemIndex];
        SystemRuntime& system = m_systems[systemIndex];
        system.buffers.resize(7);
        for (const AssetBuffer& assetBuffer : assetSystem.buffers) {
            BufferUsage usage = BufferUsage::Storage;
            if (assetBuffer.kind == kAttributesBuffer || assetBuffer.kind == kIndirectionBuffer) {
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
    for (const TextureHandle texture : m_lutTextures) {
        if (texture.valid()) {
            m_device.destroyTexture(texture);
        }
    }
    m_lutTextures.clear();
    if (m_whiteTexture.valid()) {
        m_device.destroyTexture(m_whiteTexture);
        m_whiteTexture = {};
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
    m_aliveEstimate = 0;
    m_deltaTime = 0.0f;
    resetCpuState();
    m_needsReset = true;
}

void EffectImpl::reset() {
    resetCpuState();
    m_aliveEstimate = 0;
    m_needsReset = true;
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

void EffectImpl::sendEvent(std::string_view, std::span<const EventAttribute>) {
    report("VFX events are not supported until V1.2");
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
    if (!m_playing || m_paused) {
        m_deltaTime = 0.0f;
        return;
    }
    const float clamped = std::clamp(deltaSeconds, 0.0f, kMaxDeltaSeconds);
    m_deltaTime = std::isfinite(clamped) ? clamped * m_playRate : 0.0f;
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

void EffectImpl::setUniforms(CommandSink& sink, std::uint32_t systemIndex, bool simulate) {
    const SystemRuntime& system = m_systems[systemIndex];
    const Vec4 params{static_cast<float>(system.time), simulate ? m_deltaTime : 0.0f,
                      simulate ? static_cast<float>(system.spawnCount) : 0.0f,
                      static_cast<float>(m_randomSeed)};
    sink.setUniform(m_paramsUniform, &params);
    const Vec4 ids{static_cast<float>(m_nextParticleId), static_cast<float>(systemIndex), 0.0f, 0.0f};
    sink.setUniform(m_idsUniform, &ids);
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
        if (binding.index >= system.buffers.size() || !system.buffers[binding.index].handle.valid()) {
            report("VFX kernel references an unavailable buffer");
            continue;
        }
        sink.setStorageBuffer(binding.stage, system.buffers[binding.index].handle, binding.access);
    }
}

void EffectImpl::dispatchKernel(CommandSink& sink, std::uint32_t systemIndex, std::uint32_t programIndex) {
    const AssetProgram& program = m_asset->programs[programIndex];
    bindKernelBuffers(sink, systemIndex, program);
    const ProgramHandle handle = m_programs[programIndex];
    if (!handle.valid()) {
        report("VFX kernel program is invalid");
        return;
    }
    sink.dispatch(handle, program.groups[0], program.groups[1], program.groups[2]);
    ++m_runtime.m_diagnostics.dispatches;
}

void EffectImpl::encodeSimulation(CommandSink& sink, const FrameContext&) {
    if (!m_needsReset && (!m_playing || m_paused)) {
        return;
    }
    const bool simulate = m_playing && !m_paused;
    if (m_needsReset) {
        m_aliveEstimate = 0;
        m_nextParticleId = 0;
        for (SystemRuntime& system : m_systems) {
            system.aliveEstimate = 0;
        }
    }
    for (std::size_t systemIndex = 0; systemIndex < m_systems.size(); ++systemIndex) {
        SystemRuntime& system = m_systems[systemIndex];
        const std::string passName = "VFX " + m_asset->systems[systemIndex].name + " Simulation";
        sink.beginPass({passName, true, false, false});
        setUniforms(sink, static_cast<std::uint32_t>(systemIndex), simulate);
        for (const std::uint32_t programIndex : system.kernels) {
            const AssetProgram& program = m_asset->programs[programIndex];
            if (program.task == static_cast<std::uint8_t>(kTaskDeadListInit) && !m_needsReset) {
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
    }
    m_needsReset = false;
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
}

void EffectImpl::bindOutputBuffers(CommandSink& sink, std::uint32_t systemIndex,
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

void EffectImpl::bindOutputTexture(CommandSink& sink, const AssetProgram& program) {
    TextureHandle texture = m_whiteTexture;
    for (const AssetBinding& binding : program.fragmentBindings) {
        if (binding.kind != AssetBindingKind::Texture) {
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
            sink.beginPass({passName, false, false, false});
            sink.setUniform(m_worldUniform, &world);
            sink.setUniform(m_invViewUniform, &invView);
            sink.setUniform(m_viewProjUniform, &frame.viewProjection);
            sink.setUniform(m_cameraPosUniform, &cameraPosition);
            bindOutputBuffers(sink, static_cast<std::uint32_t>(systemIndex), program);
            bindOutputTexture(sink, program);
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

void EffectImpl::updateLut(std::size_t propertyIndex) {
    if (propertyIndex >= m_lutData.size()) {
        return;
    }
    const AssetProperty& property = m_asset->properties[propertyIndex];
    if (m_lutTextures[propertyIndex].valid()) {
        if (!m_device.updateTexture(m_lutTextures[propertyIndex], m_lutData[propertyIndex], kLutRowPitch)) {
            report("failed to update VFX LUT texture");
        }
        return;
    }
    TextureDesc desc;
    desc.width = static_cast<std::uint16_t>(kLutSampleCount);
    desc.height = 1;
    desc.format = TextureFormat::RGBA16F;
    desc.initialData = m_lutData[propertyIndex].data();
    desc.dataSize = static_cast<std::uint32_t>(m_lutData[propertyIndex].size());
    const std::string lutName = "vfx.lut." + property.name;
    desc.name = lutName;
    m_lutTextures[propertyIndex] = m_device.createTexture(desc);
    if (!m_lutTextures[propertyIndex].valid()) {
        report("failed to create VFX LUT texture");
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
        return nullptr;
    }
    Effect* result = effect.get();
    m_effects.push_back(std::move(effect));
    if (error != nullptr) {
        error->clear();
    }
    m_diagnostics.effects = static_cast<std::uint32_t>(m_effects.size());
    m_diagnostics.systems = 0;
    for (const std::unique_ptr<Effect>& current : m_effects) {
        const auto* impl = dynamic_cast<const EffectImpl*>(current.get());
        if (impl != nullptr) {
            m_diagnostics.systems += impl->systemCount();
        }
    }
    return result;
}

void Runtime::destroyEffect(Effect* effect) {
    if (effect == nullptr) {
        return;
    }
    for (auto iterator = m_effects.begin(); iterator != m_effects.end(); ++iterator) {
        if (iterator->get() == effect) {
            m_effects.erase(iterator);
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
            return;
        }
    }
}

void Runtime::update(float deltaSeconds) {
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
    m_encodeMs = std::chrono::duration<float, std::milli>(end - start).count();
    m_diagnostics.encodeMs = m_encodeMs;
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
    m_encodeMs = std::chrono::duration<float, std::milli>(end - start).count();
    m_diagnostics.encodeMs = m_encodeMs;
}

void Runtime::setErrorCallback(std::function<void(std::string_view)> callback) {
    m_errorCallback = std::move(callback);
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
