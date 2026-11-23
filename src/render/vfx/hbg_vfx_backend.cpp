#include "render/vfx/hbg_vfx_backend.h"

#include "core/log.h"
#include "gfx/state.h"
#include "render/passes/forward_lighting.h"

#include <array>
#include <cstring>
#include <utility>

namespace hbg::vfxbridge {
namespace {

bgfx::Access::Enum toBgfx(::vfx::Access access) {
    switch (access) {
    case ::vfx::Access::Read:
        return bgfx::Access::Read;
    case ::vfx::Access::Write:
        return bgfx::Access::Write;
    case ::vfx::Access::ReadWrite:
        return bgfx::Access::ReadWrite;
    }
    return bgfx::Access::Read;
}

gfx::UniformType toGfx(::vfx::UniformType type) {
    switch (type) {
    case ::vfx::UniformType::Sampler:
        return gfx::UniformType::Sampler;
    case ::vfx::UniformType::Vec4:
        return gfx::UniformType::Vec4;
    case ::vfx::UniformType::Mat3:
        return gfx::UniformType::Mat3;
    case ::vfx::UniformType::Mat4:
        return gfx::UniformType::Mat4;
    }
    return gfx::UniformType::Vec4;
}

gfx::TextureFormat toGfx(::vfx::TextureFormat format) {
    switch (format) {
    case ::vfx::TextureFormat::RGBA8:
        return gfx::TextureFormat::RGBA8;
    case ::vfx::TextureFormat::BGRA8:
        return gfx::TextureFormat::BGRA8;
    case ::vfx::TextureFormat::RGBA16F:
        return gfx::TextureFormat::RGBA16F;
    case ::vfx::TextureFormat::RGBA32F:
        return gfx::TextureFormat::RGBA32F;
    case ::vfx::TextureFormat::RG16F:
        return gfx::TextureFormat::RG16F;
    case ::vfx::TextureFormat::RG32F:
        return gfx::TextureFormat::RG32F;
    case ::vfx::TextureFormat::R8:
        return gfx::TextureFormat::R8;
    case ::vfx::TextureFormat::RG8:
        return gfx::TextureFormat::RG8;
    case ::vfx::TextureFormat::R32U:
        return gfx::TextureFormat::R32U;
    case ::vfx::TextureFormat::R32F:
        return gfx::TextureFormat::R32F;
    case ::vfx::TextureFormat::RG11B10F:
        return gfx::TextureFormat::RG11B10F;
    case ::vfx::TextureFormat::D16:
        return gfx::TextureFormat::D16;
    case ::vfx::TextureFormat::D24S8:
        return gfx::TextureFormat::D24S8;
    case ::vfx::TextureFormat::D32F:
        return gfx::TextureFormat::D32F;
    case ::vfx::TextureFormat::BC1:
        return gfx::TextureFormat::BC1;
    case ::vfx::TextureFormat::BC3:
        return gfx::TextureFormat::BC3;
    case ::vfx::TextureFormat::BC5:
        return gfx::TextureFormat::BC5;
    case ::vfx::TextureFormat::BC7:
        return gfx::TextureFormat::BC7;
    case ::vfx::TextureFormat::Count:
        return gfx::TextureFormat::Count;
    }
    return gfx::TextureFormat::Count;
}

bool buildStorageVertexLayout(std::uint32_t stride, gfx::VertexLayout& layout) {
    if (stride == 0 || stride % sizeof(float) != 0) {
        return false;
    }

    constexpr std::array<bgfx::Attrib::Enum, 16> kAttributes = {
        bgfx::Attrib::Position, bgfx::Attrib::Normal,   bgfx::Attrib::Tangent,  bgfx::Attrib::Bitangent,
        bgfx::Attrib::Color0,   bgfx::Attrib::Color1,   bgfx::Attrib::Color2,   bgfx::Attrib::Color3,
        bgfx::Attrib::Indices,  bgfx::Attrib::Weight,   bgfx::Attrib::TexCoord0, bgfx::Attrib::TexCoord1,
        bgfx::Attrib::TexCoord2, bgfx::Attrib::TexCoord3, bgfx::Attrib::TexCoord4, bgfx::Attrib::TexCoord5,
    };

    std::uint32_t remaining = stride / sizeof(float);
    std::size_t attribute = 0;
    while (remaining > 0 && attribute < kAttributes.size()) {
        const auto componentCount = static_cast<std::uint8_t>(remaining >= 4 ? 4 : remaining);
        layout.add(kAttributes[attribute], componentCount, bgfx::AttribType::Float);
        remaining -= componentCount;
        ++attribute;
    }

    if (remaining != 0) {
        return false;
    }
    layout.end();
    return true;
}

std::uint64_t toBgfx(const ::vfx::DrawState& drawState) {
    std::uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_MSAA;
    if (drawState.depthWrite) {
        state |= BGFX_STATE_WRITE_Z;
    }

    switch (drawState.depthFunc) {
    case ::vfx::CompareFunc::Less:
        state |= BGFX_STATE_DEPTH_TEST_LESS;
        break;
    case ::vfx::CompareFunc::LessEqual:
        state |= BGFX_STATE_DEPTH_TEST_LEQUAL;
        break;
    case ::vfx::CompareFunc::Equal:
        state |= BGFX_STATE_DEPTH_TEST_EQUAL;
        break;
    case ::vfx::CompareFunc::Greater:
        state |= BGFX_STATE_DEPTH_TEST_GREATER;
        break;
    case ::vfx::CompareFunc::Always:
        state |= BGFX_STATE_DEPTH_TEST_ALWAYS;
        break;
    }

    if (!drawState.depthTest) {
        state &= ~BGFX_STATE_DEPTH_TEST_MASK;
        state |= BGFX_STATE_DEPTH_TEST_ALWAYS;
    }
    if (drawState.cullBackFace) {
        state |= BGFX_STATE_CULL_CW;
    }

    switch (drawState.blend) {
    case ::vfx::BlendMode::Alpha:
        state |= BGFX_STATE_BLEND_ALPHA;
        break;
    case ::vfx::BlendMode::Additive:
        state |= BGFX_STATE_BLEND_ADD;
        break;
    case ::vfx::BlendMode::Premultiply:
        state |= BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA);
        break;
    case ::vfx::BlendMode::Opaque:
        break;
    }
    return state;
}

} // namespace

HbgVfxBackend::HbgVfxBackend(gfx::ResourceManager& resources) : m_resources(resources) {}

HbgVfxBackend::~HbgVfxBackend() { destroyAll(); }

void HbgVfxBackend::begin(gfx::Encoder& encoder, bgfx::ViewId view) {
    m_encoder = &encoder;
    m_view = view;
}

void HbgVfxBackend::end() { m_encoder = nullptr; }

::vfx::BufferHandle HbgVfxBackend::createBuffer(const ::vfx::BufferDesc& desc) {
    if (desc.size == 0) {
        HBG_LOGE("HbgVfxBackend::createBuffer '{}' requires a non-zero size", desc.name);
        return {};
    }

    gfx::BufferDesc native;
    native.name = std::string(desc.name);
    gfx::VertexLayout layout;
    switch (desc.usage) {
    case ::vfx::BufferUsage::Storage:
        if (desc.size % 4u != 0) {
            HBG_LOGE("HbgVfxBackend::createBuffer '{}' storage size must be a multiple of 4", desc.name);
            return {};
        }
        native.type = gfx::BufferType::Storage;
        native.count = desc.size / 4u;
        native.flags = desc.initialData != nullptr ? BGFX_BUFFER_COMPUTE_READ : BGFX_BUFFER_COMPUTE_READ_WRITE;
        native.data = desc.initialData;
        native.size = desc.size;
        break;
    case ::vfx::BufferUsage::StorageVertex:
        if (!buildStorageVertexLayout(desc.stride, layout)) {
            HBG_LOGE("HbgVfxBackend::createBuffer '{}' has an unsupported stride {}", desc.name, desc.stride);
            return {};
        }
        if (desc.size % desc.stride != 0) {
            HBG_LOGE("HbgVfxBackend::createBuffer '{}' size must be a multiple of stride {}", desc.name,
                     desc.stride);
            return {};
        }
        native.type = gfx::BufferType::StorageVertex;
        native.count = desc.size / desc.stride;
        native.layout = &layout;
        native.flags = desc.initialData != nullptr ? BGFX_BUFFER_COMPUTE_READ : BGFX_BUFFER_COMPUTE_READ_WRITE;
        native.data = desc.initialData;
        native.size = desc.size;
        break;
    case ::vfx::BufferUsage::Indirect:
        if (desc.size % 32u != 0) {
            HBG_LOGE("HbgVfxBackend::createBuffer '{}' indirect size must be a multiple of 32", desc.name);
            return {};
        }
        native.type = gfx::BufferType::Indirect;
        native.count = desc.size / 32u;
        native.flags = BGFX_BUFFER_NONE;
        break;
    case ::vfx::BufferUsage::Dynamic:
        if (!buildStorageVertexLayout(desc.stride, layout)) {
            HBG_LOGE("HbgVfxBackend::createBuffer '{}' has an unsupported stride {}", desc.name, desc.stride);
            return {};
        }
        if (desc.size % desc.stride != 0) {
            HBG_LOGE("HbgVfxBackend::createBuffer '{}' size must be a multiple of stride {}", desc.name,
                     desc.stride);
            return {};
        }
        native.type = gfx::BufferType::DynamicVertex;
        native.count = desc.size / desc.stride;
        native.layout = &layout;
        native.flags = BGFX_BUFFER_NONE;
        native.data = desc.initialData;
        native.size = desc.size;
        break;
    default:
        return {};
    }

    BufferEntry entry;
    entry.handle = m_resources.createBuffer(native);
    if (!entry.handle.valid()) {
        return {};
    }
    entry.name.assign(desc.name);
    entry.usage = desc.usage;
    entry.stride = desc.stride;
    entry.alive = true;
    m_buffers.push_back(entry);
    return ::vfx::BufferHandle{static_cast<::vfx::Index>(m_buffers.size() - 1)};
}

gfx::BufferHandle HbgVfxBackend::nativeBuffer(::vfx::BufferHandle handle) const {
    if (!handle.valid() || handle.index >= m_buffers.size()) {
        return {};
    }
    const BufferEntry& entry = m_buffers[handle.index];
    return entry.alive ? entry.handle : gfx::BufferHandle{};
}

::vfx::BufferHandle HbgVfxBackend::findBuffer(std::string_view name) const {
    for (std::size_t index = 0; index < m_buffers.size(); ++index) {
        const BufferEntry& entry = m_buffers[index];
        if (entry.alive && entry.name == name) {
            return ::vfx::BufferHandle{static_cast<::vfx::Index>(index)};
        }
    }
    return {};
}

void HbgVfxBackend::destroyBuffer(::vfx::BufferHandle handle) {
    if (!handle.valid() || handle.index >= m_buffers.size()) {
        return;
    }
    BufferEntry& entry = m_buffers[handle.index];
    if (!entry.alive) {
        return;
    }
    m_resources.destroyBuffer(entry.handle);
    entry.alive = false;
}

bool HbgVfxBackend::updateBuffer(::vfx::BufferHandle handle, std::span<const std::byte> data, std::uint32_t offset) {
    if (!handle.valid() || handle.index >= m_buffers.size()) {
        return false;
    }
    const BufferEntry& entry = m_buffers[handle.index];
    if (!entry.alive) {
        return false;
    }
    return m_resources.updateBuffer(entry.handle, data.data(), static_cast<std::uint32_t>(data.size()), offset);
}

::vfx::TextureHandle HbgVfxBackend::importTexture(gfx::TextureHandle handle) {
    if (!handle.valid()) {
        return {};
    }
    const uint16_t index = static_cast<uint16_t>(handle.index);
    const auto existing = m_importedTextures.find(index);
    if (existing != m_importedTextures.end()) {
        return existing->second;
    }
    TextureEntry entry;
    entry.handle = handle;
    entry.alive = true;
    entry.owned = false;
    m_textures.push_back(entry);
    const ::vfx::TextureHandle imported{static_cast<::vfx::Index>(m_textures.size() - 1)};
    m_importedTextures.emplace(index, imported);
    return imported;
}

::vfx::TextureHandle HbgVfxBackend::createTexture(const ::vfx::TextureDesc& desc) {
    gfx::TextureDesc native;
    native.width = desc.width;
    native.height = desc.height;
    native.mipCount = desc.mipCount;
    native.format = toGfx(desc.format);
    native.data = desc.initialData;
    native.dataSize = desc.dataSize;
    native.name = std::string(desc.name);
    native.renderTarget = desc.usage == ::vfx::TextureUsage::RenderTarget;
    native.computeWrite = desc.usage == ::vfx::TextureUsage::Storage;
    if (native.computeWrite) {
        native.flags = BGFX_TEXTURE_COMPUTE_WRITE;
    }

    TextureEntry entry;
    entry.handle = m_resources.createTexture(native);
    if (!entry.handle.valid()) {
        return {};
    }
    entry.width = desc.width;
    entry.height = desc.height;
    entry.alive = true;
    m_textures.push_back(entry);
    return ::vfx::TextureHandle{static_cast<::vfx::Index>(m_textures.size() - 1)};
}

void HbgVfxBackend::destroyTexture(::vfx::TextureHandle handle) {
    if (!handle.valid() || handle.index >= m_textures.size()) {
        return;
    }
    TextureEntry& entry = m_textures[handle.index];
    if (!entry.alive) {
        return;
    }
    m_resources.destroyTexture(entry.handle);
    entry.alive = false;
}

bool HbgVfxBackend::updateTexture(::vfx::TextureHandle handle, std::span<const std::byte> data,
                                  std::uint32_t rowPitch) {
    if (!handle.valid() || handle.index >= m_textures.size()) {
        return false;
    }
    const TextureEntry& entry = m_textures[handle.index];
    if (!entry.alive) {
        return false;
    }
    return m_resources.updateTexture(entry.handle, data.data(), static_cast<std::uint32_t>(data.size()), entry.width,
                                     entry.height, static_cast<std::uint16_t>(rowPitch));
}

::vfx::ProgramHandle HbgVfxBackend::createProgram(std::span<const ::vfx::ShaderBlob> blobs) {
    if (blobs.empty()) {
        return {};
    }

    const ::vfx::ShaderBackend currentBackend = backend();
    const ::vfx::ShaderBlob* compute = nullptr;
    const ::vfx::ShaderBlob* vertex = nullptr;
    const ::vfx::ShaderBlob* fragment = nullptr;
    for (const ::vfx::ShaderBlob& blob : blobs) {
        if (blob.backend != currentBackend || blob.data.empty() || blob.data.data() == nullptr) {
            return {};
        }
        switch (blob.stage) {
        case ::vfx::ShaderStage::Compute:
            if (compute != nullptr) {
                return {};
            }
            compute = &blob;
            break;
        case ::vfx::ShaderStage::Vertex:
            if (vertex != nullptr) {
                return {};
            }
            vertex = &blob;
            break;
        case ::vfx::ShaderStage::Fragment:
            if (fragment != nullptr) {
                return {};
            }
            fragment = &blob;
            break;
        }
    }

    ProgramEntry entry;
    if (compute != nullptr) {
        if (vertex != nullptr || fragment != nullptr) {
            return {};
        }
        entry.handle = m_resources.createComputeProgram(compute->data.data(),
                                                        static_cast<std::uint32_t>(compute->data.size()));
    } else {
        if (vertex == nullptr || fragment == nullptr) {
            return {};
        }
        entry.handle = m_resources.createProgram(
            vertex->data.data(), static_cast<std::uint32_t>(vertex->data.size()), fragment->data.data(),
            static_cast<std::uint32_t>(fragment->data.size()));
    }
    if (!entry.handle.valid()) {
        return {};
    }

    entry.alive = true;
    m_programs.push_back(entry);
    return ::vfx::ProgramHandle{static_cast<::vfx::Index>(m_programs.size() - 1)};
}

void HbgVfxBackend::destroyProgram(::vfx::ProgramHandle handle) {
    if (!handle.valid() || handle.index >= m_programs.size()) {
        return;
    }
    ProgramEntry& entry = m_programs[handle.index];
    if (!entry.alive) {
        return;
    }
    m_resources.destroyProgram(entry.handle);
    entry.alive = false;
}

::vfx::UniformHandle HbgVfxBackend::findUniform(std::string_view name, ::vfx::UniformType type,
                                                std::uint16_t count) {
    const std::string key(name);
    const auto existing = m_uniformsByName.find(key);
    if (existing != m_uniformsByName.end()) {
        return existing->second;
    }

    UniformEntry entry;
    entry.owned = !m_resources.uniformHandle(key).valid();
    entry.handle = m_resources.createUniform({key, toGfx(type), count});
    if (!entry.handle.valid()) {
        return {};
    }
    entry.type = type;
    entry.count = count;
    entry.alive = true;
    m_uniforms.push_back(entry);
    const ::vfx::UniformHandle handle{static_cast<::vfx::Index>(m_uniforms.size() - 1)};
    m_uniformsByName.emplace(key, handle);
    return handle;
}

::vfx::SamplerHandle HbgVfxBackend::findSampler(std::string_view name) {
    const std::string key(name);
    const auto existing = m_samplersByName.find(key);
    if (existing != m_samplersByName.end()) {
        return existing->second;
    }

    SamplerEntry entry;
    entry.owned = !m_resources.uniformHandle(key).valid();
    entry.handle = m_resources.createUniform({key, gfx::UniformType::Sampler, 1});
    if (!entry.handle.valid()) {
        return {};
    }
    entry.alive = true;
    m_samplers.push_back(entry);
    const ::vfx::SamplerHandle handle{static_cast<::vfx::Index>(m_samplers.size() - 1)};
    m_samplersByName.emplace(key, handle);
    return handle;
}

::vfx::ShaderBackend HbgVfxBackend::backend() const {
    const bgfx::Caps* caps = bgfx::getCaps();
    if (caps == nullptr) {
        return ::vfx::ShaderBackend::Dxbc;
    }
    switch (caps->rendererType) {
    case bgfx::RendererType::Vulkan:
        return ::vfx::ShaderBackend::Spirv;
    case bgfx::RendererType::Direct3D11:
    case bgfx::RendererType::Direct3D12:
    case bgfx::RendererType::Noop:
    default:
        return ::vfx::ShaderBackend::Dxbc;
    }
}

::vfx::Limits HbgVfxBackend::limits() const {
    const bgfx::Caps* caps = bgfx::getCaps();
    if (caps == nullptr) {
        return {};
    }
    return ::vfx::Limits{caps->limits.maxTextureSamplers, caps->limits.maxComputeBindings};
}

void HbgVfxBackend::beginPass(const ::vfx::PassDesc& desc) {
    if (m_encoder != nullptr && !desc.name.empty()) {
        m_encoder->setMarker(std::string(desc.name).c_str());
    }
}

void HbgVfxBackend::endPass() {}

void HbgVfxBackend::setStorageBuffer(std::uint8_t stage, ::vfx::BufferHandle buffer, ::vfx::Access access) {
    if (m_encoder == nullptr || !buffer.valid() || buffer.index >= m_buffers.size()) {
        return;
    }
    const BufferEntry& entry = m_buffers[buffer.index];
    if (!entry.alive) {
        return;
    }
    m_encoder->setStorageBuffer(stage, entry.handle, toBgfx(access));
}

void HbgVfxBackend::setTexture(std::uint8_t stage, ::vfx::SamplerHandle sampler, ::vfx::TextureHandle texture) {
    if (m_encoder == nullptr || !sampler.valid() || sampler.index >= m_samplers.size() || !texture.valid() ||
        texture.index >= m_textures.size()) {
        return;
    }
    const SamplerEntry& samplerEntry = m_samplers[sampler.index];
    const TextureEntry& textureEntry = m_textures[texture.index];
    if (!samplerEntry.alive || !textureEntry.alive) {
        return;
    }
    m_encoder->setTexture(stage, samplerEntry.handle, textureEntry.handle);
}

void HbgVfxBackend::setUniform(::vfx::UniformHandle uniform, const void* data, std::uint16_t count) {
    if (m_encoder == nullptr || data == nullptr || !uniform.valid() || uniform.index >= m_uniforms.size()) {
        return;
    }
    const UniformEntry& entry = m_uniforms[uniform.index];
    if (!entry.alive) {
        return;
    }
    m_encoder->setUniform(entry.handle, data, count);
}

void HbgVfxBackend::setState(const ::vfx::DrawState& drawState) {
    if (m_encoder != nullptr) {
        m_encoder->setState(toBgfx(drawState));
    }
}

void HbgVfxBackend::dispatch(::vfx::ProgramHandle program, std::uint32_t x, std::uint32_t y, std::uint32_t z) {
    if (m_encoder == nullptr || !program.valid() || program.index >= m_programs.size()) {
        return;
    }
    const ProgramEntry& entry = m_programs[program.index];
    if (!entry.alive) {
        return;
    }
    m_encoder->dispatch(m_view, entry.handle, x, y, z);
}

void HbgVfxBackend::drawIndirect(::vfx::ProgramHandle program, ::vfx::BufferHandle indirect, std::uint32_t start,
                                 std::uint32_t num) {
    if (m_encoder == nullptr || !program.valid() || program.index >= m_programs.size() || !indirect.valid() ||
        indirect.index >= m_buffers.size()) {
        return;
    }
    const ProgramEntry& programEntry = m_programs[program.index];
    const BufferEntry& indirectEntry = m_buffers[indirect.index];
    if (!programEntry.alive || !indirectEntry.alive || indirectEntry.usage != ::vfx::BufferUsage::Indirect) {
        return;
    }

    const gfx::BufferHandle dummy = ensureDummyVertexBuffer();
    if (!dummy.valid()) {
        return;
    }
    m_encoder->setVertexBuffer(0, dummy);
    m_encoder->submitIndirect(m_view, programEntry.handle, indirectEntry.handle, start, num);
}

void HbgVfxBackend::bindLighting(::vfx::CommandSink&, const ::vfx::FrameContext& frame,
                                 const ::vfx::LitSettings&) {
    ++m_lightingBindCount;
    if (m_encoder == nullptr || frame.lighting == nullptr) {
        return;
    }
    const auto* lighting = static_cast<const ForwardLightingContext*>(frame.lighting);
    bindForwardLighting(*m_encoder, *lighting);
}

::vfx::ProgramHandle HbgVfxBackend::resolveProgram(const ::vfx::RenderOutputDesc&, ::vfx::ShaderBackend) {
    return {};
}

std::size_t HbgVfxBackend::liveBufferCount() const {
    std::size_t count = m_dummyVertexBuffer.valid() ? 1u : 0u;
    for (const BufferEntry& entry : m_buffers) {
        if (entry.alive) {
            ++count;
        }
    }
    return count;
}

std::size_t HbgVfxBackend::liveTextureCount() const {
    std::size_t count = 0;
    for (const TextureEntry& entry : m_textures) {
        if (entry.alive) {
            ++count;
        }
    }
    return count;
}

std::size_t HbgVfxBackend::liveProgramCount() const {
    std::size_t count = 0;
    for (const ProgramEntry& entry : m_programs) {
        if (entry.alive) {
            ++count;
        }
    }
    return count;
}

std::size_t HbgVfxBackend::liveUniformCount() const {
    std::size_t count = 0;
    for (const UniformEntry& entry : m_uniforms) {
        if (entry.alive) {
            ++count;
        }
    }
    return count;
}

std::size_t HbgVfxBackend::liveSamplerCount() const {
    std::size_t count = 0;
    for (const SamplerEntry& entry : m_samplers) {
        if (entry.alive) {
            ++count;
        }
    }
    return count;
}

gfx::BufferHandle HbgVfxBackend::ensureDummyVertexBuffer() {
    if (m_dummyVertexBuffer.valid()) {
        return m_dummyVertexBuffer;
    }

    std::array<float, 6 * 3> vertices{};
    gfx::VertexLayout layout;
    layout.position().end();
    gfx::BufferDesc desc;
    desc.type = gfx::BufferType::Vertex;
    desc.data = vertices.data();
    desc.size = static_cast<std::uint32_t>(vertices.size() * sizeof(float));
    desc.layout = &layout;
    desc.name = "vfx.dummyVertexBuffer";
    m_dummyVertexBuffer = m_resources.createBuffer(desc);
    return m_dummyVertexBuffer;
}

void HbgVfxBackend::destroyAll() {
    if (!m_resources.initialized()) {
        return;
    }

    for (ProgramEntry& entry : m_programs) {
        if (entry.alive) {
            m_resources.destroyProgram(entry.handle);
            entry.alive = false;
        }
    }
    for (BufferEntry& entry : m_buffers) {
        if (entry.alive) {
            m_resources.destroyBuffer(entry.handle);
            entry.alive = false;
        }
    }
    if (m_dummyVertexBuffer.valid()) {
        m_resources.destroyBuffer(m_dummyVertexBuffer);
        m_dummyVertexBuffer = {};
    }
    for (TextureEntry& entry : m_textures) {
        if (entry.alive) {
            if (entry.owned) {
                m_resources.destroyTexture(entry.handle);
            }
            entry.alive = false;
        }
    }
    m_importedTextures.clear();
    for (SamplerEntry& entry : m_samplers) {
        if (entry.alive) {
            if (entry.owned && m_resources.uniform(entry.handle) != nullptr) {
                m_resources.destroyUniform(entry.handle);
            }
            entry.alive = false;
        }
    }
    for (UniformEntry& entry : m_uniforms) {
        if (entry.alive) {
            if (entry.owned && m_resources.uniform(entry.handle) != nullptr) {
                m_resources.destroyUniform(entry.handle);
            }
            entry.alive = false;
        }
    }
    m_uniformsByName.clear();
    m_samplersByName.clear();
}



} // namespace hbg::vfxbridge
