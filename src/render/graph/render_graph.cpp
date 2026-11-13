#include "render/graph/render_graph.h"

#include "core/log.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <utility>

namespace hbg::render {
namespace {

constexpr uint32_t kFramesInFlight = 2;
constexpr uint8_t kMaxColorAttachments = 8;

bool sameTextureShape(const gfx::TextureDesc& lhs, const gfx::TextureDesc& rhs) {
    return lhs.width == rhs.width && lhs.height == rhs.height && lhs.layers == rhs.layers &&
           lhs.mipCount == rhs.mipCount && lhs.msaaSamples == rhs.msaaSamples && lhs.format == rhs.format &&
           lhs.flags == rhs.flags && lhs.renderTarget == rhs.renderTarget && lhs.depthStencil == rhs.depthStencil &&
           lhs.computeWrite == rhs.computeWrite && lhs.cubeMap == rhs.cubeMap;
}

bool sameBufferShape(const gfx::BufferDesc& lhs, const gfx::BufferDesc& rhs) {
    const uint16_t leftStride = lhs.layout != nullptr ? lhs.layout->stride() : 0;
    const uint16_t rightStride = rhs.layout != nullptr ? rhs.layout->stride() : 0;
    return lhs.type == rhs.type && lhs.size == rhs.size && lhs.count == rhs.count && lhs.flags == rhs.flags &&
           leftStride == rightStride;
}

uint32_t nextGeneration(uint32_t generation) {
    ++generation;
    return generation == 0 ? 1 : generation;
}

} // namespace

RenderGraphContext::RenderGraphContext(gfx::Encoder& encoder, gfx::ResourceManager& resources, bgfx::ViewId viewId,
                                       uint16_t width, uint16_t height, gfx::FrameBufferHandle frameBuffer,
                                       bool backbuffer, const std::vector<gfx::TextureHandle>& textures,
                                       const std::vector<gfx::BufferHandle>& buffers)
    : m_encoder(encoder), m_resources(resources), m_viewId(viewId), m_width(width), m_height(height),
      m_frameBuffer(frameBuffer), m_backbuffer(backbuffer), m_textures(textures), m_buffers(buffers) {}

gfx::TextureHandle RenderGraphContext::texture(RenderGraphTextureHandle handle) const {
    if (!handle.valid() || handle.index >= m_textures.size()) {
        return {};
    }
    return m_textures[handle.index];
}

gfx::BufferHandle RenderGraphContext::buffer(RenderGraphBufferHandle handle) const {
    if (!handle.valid() || handle.index >= m_buffers.size()) {
        return {};
    }
    return m_buffers[handle.index];
}

RenderGraphBuilder::RenderGraphBuilder(RenderGraph& graph, uint32_t passIndex)
    : m_graph(graph), m_passIndex(passIndex) {}

RenderGraphBuilder& RenderGraphBuilder::readTexture(RenderGraphTextureHandle handle) {
    m_graph.addTextureRead(m_passIndex, handle);
    return *this;
}

RenderGraphBuilder& RenderGraphBuilder::writeTexture(RenderGraphTextureHandle handle) {
    m_graph.addTextureWrite(m_passIndex, handle);
    return *this;
}

RenderGraphBuilder& RenderGraphBuilder::setColorAttachment(RenderGraphTextureHandle handle, uint8_t index,
                                                           uint16_t mip) {
    m_graph.setColorAttachment(m_passIndex, handle, index, mip);
    return *this;
}

RenderGraphBuilder& RenderGraphBuilder::setDepthAttachment(RenderGraphTextureHandle handle, uint16_t mip) {
    m_graph.setDepthAttachment(m_passIndex, handle, mip);
    return *this;
}

RenderGraphBuilder& RenderGraphBuilder::setViewSize(uint16_t width, uint16_t height) {
    m_graph.setViewSize(m_passIndex, width, height);
    return *this;
}

RenderGraphBuilder& RenderGraphBuilder::readBuffer(RenderGraphBufferHandle handle) {
    m_graph.addBufferRead(m_passIndex, handle);
    return *this;
}

RenderGraphBuilder& RenderGraphBuilder::writeBuffer(RenderGraphBufferHandle handle) {
    m_graph.addBufferWrite(m_passIndex, handle);
    return *this;
}

RenderGraphBuilder& RenderGraphBuilder::writeBackbuffer() {
    m_graph.setBackbuffer(m_passIndex);
    return *this;
}

RenderGraphBuilder& RenderGraphBuilder::allowCulling(bool enabled) {
    m_graph.setAllowCulling(m_passIndex, enabled);
    return *this;
}

RenderGraphBuilder& RenderGraphBuilder::setRenderFunc(RenderGraphPassFunc func) {
    m_graph.setRenderFunc(m_passIndex, std::move(func));
    return *this;
}

RenderGraph::RenderGraph(std::string name) : m_name(std::move(name)) {}

RenderGraph::~RenderGraph() = default;

void RenderGraph::beginFrame(uint16_t width, uint16_t height, uint32_t frameIndex) {
    m_width = width;
    m_height = height;
    m_frameIndex = frameIndex;
    for (PooledTexture& entry : m_pooledTextures) {
        entry.reservedUntil = UINT32_MAX;
    }
    for (PooledBuffer& entry : m_pooledBuffers) {
        entry.reservedUntil = UINT32_MAX;
    }
    resetRecording();
}

void RenderGraph::resetRecording() {
    m_generation = nextGeneration(m_generation);
    m_textures.clear();
    m_buffers.clear();
    m_passes.clear();
    m_passViewIds.clear();
    m_error.clear();
    m_compiled = false;
    m_diagnostics = {};
}

void RenderGraph::recordError(std::string message) {
    if (m_error.empty()) {
        m_error = std::move(message);
    }
}

RenderGraphTextureHandle RenderGraph::importTexture(gfx::TextureHandle texture, std::string name) {
    TextureResource resource;
    resource.name = name.empty() ? "imported.texture" : std::move(name);
    resource.imported = texture;
    resource.isImported = true;
    resource.generation = m_generation;
    const uint32_t index = static_cast<uint32_t>(m_textures.size());
    m_textures.push_back(std::move(resource));
    return RenderGraphTextureHandle{index, m_generation};
}

RenderGraphTextureHandle RenderGraph::createTexture(const gfx::TextureDesc& desc, std::string name) {
    TextureResource resource;
    resource.name = name.empty() ? desc.name : std::move(name);
    if (resource.name.empty()) {
        resource.name = "transient.texture";
    }
    resource.desc = desc;
    resource.desc.name = resource.name;
    resource.generation = m_generation;
    const uint32_t index = static_cast<uint32_t>(m_textures.size());
    m_textures.push_back(std::move(resource));
    return RenderGraphTextureHandle{index, m_generation};
}

RenderGraphBufferHandle RenderGraph::importBuffer(gfx::BufferHandle buffer, std::string name) {
    BufferResource resource;
    resource.name = name.empty() ? "imported.buffer" : std::move(name);
    resource.imported = buffer;
    resource.isImported = true;
    resource.generation = m_generation;
    const uint32_t index = static_cast<uint32_t>(m_buffers.size());
    m_buffers.push_back(std::move(resource));
    return RenderGraphBufferHandle{index, m_generation};
}

RenderGraphBufferHandle RenderGraph::createBuffer(const gfx::BufferDesc& desc, std::string name) {
    BufferResource resource;
    resource.name = name.empty() ? desc.name : std::move(name);
    if (resource.name.empty()) {
        resource.name = "transient.buffer";
    }
    resource.desc = desc;
    resource.desc.name = resource.name;
    resource.generation = m_generation;
    const uint32_t index = static_cast<uint32_t>(m_buffers.size());
    m_buffers.push_back(std::move(resource));
    return RenderGraphBufferHandle{index, m_generation};
}

RenderGraphBuilder RenderGraph::addRasterPass(std::string name) {
    Pass pass;
    pass.index = static_cast<uint32_t>(m_passes.size());
    pass.name = name.empty() ? "Unnamed Pass" : std::move(name);
    pass.type = RenderGraphPassType::Raster;
    m_passes.push_back(std::move(pass));
    return RenderGraphBuilder(*this, m_passes.back().index);
}

RenderGraphBuilder RenderGraph::addComputePass(std::string name) {
    Pass pass;
    pass.index = static_cast<uint32_t>(m_passes.size());
    pass.name = name.empty() ? "Unnamed Pass" : std::move(name);
    pass.type = RenderGraphPassType::Compute;
    m_passes.push_back(std::move(pass));
    return RenderGraphBuilder(*this, m_passes.back().index);
}

RenderGraph::TextureResource* RenderGraph::textureResource(RenderGraphTextureHandle handle) {
    if (!handle.valid() || handle.index >= m_textures.size() || handle.generation != m_generation) {
        return nullptr;
    }
    return &m_textures[handle.index];
}

const RenderGraph::TextureResource* RenderGraph::textureResource(RenderGraphTextureHandle handle) const {
    if (!handle.valid() || handle.index >= m_textures.size() || handle.generation != m_generation) {
        return nullptr;
    }
    return &m_textures[handle.index];
}

RenderGraph::BufferResource* RenderGraph::bufferResource(RenderGraphBufferHandle handle) {
    if (!handle.valid() || handle.index >= m_buffers.size() || handle.generation != m_generation) {
        return nullptr;
    }
    return &m_buffers[handle.index];
}

const RenderGraph::BufferResource* RenderGraph::bufferResource(RenderGraphBufferHandle handle) const {
    if (!handle.valid() || handle.index >= m_buffers.size() || handle.generation != m_generation) {
        return nullptr;
    }
    return &m_buffers[handle.index];
}

void RenderGraph::addTextureRead(uint32_t passIndex, RenderGraphTextureHandle handle) {
    if (passIndex >= m_passes.size()) {
        return;
    }
    std::vector<RenderGraphTextureHandle>& reads = m_passes[passIndex].textureReads;
    if (std::find(reads.begin(), reads.end(), handle) == reads.end()) {
        reads.push_back(handle);
    }
}

void RenderGraph::addTextureWrite(uint32_t passIndex, RenderGraphTextureHandle handle) {
    if (passIndex >= m_passes.size()) {
        return;
    }
    std::vector<RenderGraphTextureHandle>& writes = m_passes[passIndex].textureWrites;
    if (std::find(writes.begin(), writes.end(), handle) == writes.end()) {
        writes.push_back(handle);
    }
}

void RenderGraph::addBufferRead(uint32_t passIndex, RenderGraphBufferHandle handle) {
    if (passIndex >= m_passes.size()) {
        return;
    }
    std::vector<RenderGraphBufferHandle>& reads = m_passes[passIndex].bufferReads;
    if (std::find(reads.begin(), reads.end(), handle) == reads.end()) {
        reads.push_back(handle);
    }
}

void RenderGraph::addBufferWrite(uint32_t passIndex, RenderGraphBufferHandle handle) {
    if (passIndex >= m_passes.size()) {
        return;
    }
    std::vector<RenderGraphBufferHandle>& writes = m_passes[passIndex].bufferWrites;
    if (std::find(writes.begin(), writes.end(), handle) == writes.end()) {
        writes.push_back(handle);
    }
}

void RenderGraph::setColorAttachment(uint32_t passIndex, RenderGraphTextureHandle handle, uint8_t index, uint16_t mip) {
    if (passIndex >= m_passes.size() || index >= kMaxColorAttachments) {
        recordError("invalid color attachment index");
        return;
    }
    Pass& pass = m_passes[passIndex];
    if (pass.colorAttachments.size() <= index) {
        pass.colorAttachments.resize(static_cast<std::size_t>(index) + 1);
        pass.colorAttachmentMips.resize(static_cast<std::size_t>(index) + 1, 0);
    }
    if (pass.colorAttachments[index].valid() && pass.colorAttachments[index] != handle) {
        recordError("multiple color resources set at the same attachment index");
        return;
    }
    pass.colorAttachments[index] = handle;
    pass.colorAttachmentMips[index] = mip;
    addTextureWrite(passIndex, handle);
}

void RenderGraph::setDepthAttachment(uint32_t passIndex, RenderGraphTextureHandle handle, uint16_t mip) {
    if (passIndex >= m_passes.size()) {
        return;
    }
    Pass& pass = m_passes[passIndex];
    if (pass.hasDepthAttachment && pass.depthAttachment != handle) {
        recordError("multiple depth attachments declared for a pass");
        return;
    }
    pass.depthAttachment = handle;
    pass.depthAttachmentMip = mip;
    pass.hasDepthAttachment = true;
    addTextureWrite(passIndex, handle);
}

void RenderGraph::setViewSize(uint32_t passIndex, uint16_t width, uint16_t height) {
    if (passIndex >= m_passes.size()) {
        return;
    }
    if (width == 0 || height == 0) {
        recordError("invalid pass view size");
        return;
    }
    Pass& pass = m_passes[passIndex];
    pass.viewWidth = width;
    pass.viewHeight = height;
}

void RenderGraph::setBackbuffer(uint32_t passIndex) {
    if (passIndex >= m_passes.size()) {
        return;
    }
    Pass& pass = m_passes[passIndex];
    pass.backbuffer = true;
    pass.sideEffect = true;
    pass.allowCulling = false;
}

void RenderGraph::setAllowCulling(uint32_t passIndex, bool enabled) {
    if (passIndex < m_passes.size()) {
        m_passes[passIndex].allowCulling = enabled;
    }
}

void RenderGraph::setRenderFunc(uint32_t passIndex, RenderGraphPassFunc func) {
    if (passIndex < m_passes.size()) {
        m_passes[passIndex].renderFunc = std::move(func);
    }
}

bool RenderGraph::validatePasses() {
    std::vector<bool> textureWritten(m_textures.size(), false);
    std::vector<bool> bufferWritten(m_buffers.size(), false);

    for (const TextureResource& texture : m_textures) {
        if (texture.isImported && !texture.imported.valid()) {
            recordError("imported texture '" + texture.name + "' is invalid");
            return false;
        }
    }
    for (const BufferResource& buffer : m_buffers) {
        if (buffer.isImported && !buffer.imported.valid()) {
            recordError("imported buffer '" + buffer.name + "' is invalid");
            return false;
        }
    }

    for (Pass& pass : m_passes) {
        if (!pass.renderFunc) {
            recordError("pass '" + pass.name + "' has no render function");
            return false;
        }
        if (pass.backbuffer && (!pass.colorAttachments.empty() || pass.hasDepthAttachment)) {
            recordError("pass '" + pass.name + "' writes the backbuffer and also has attachments");
            return false;
        }
        for (std::size_t index = 0; index < pass.colorAttachments.size(); ++index) {
            if (!pass.colorAttachments[index].valid()) {
                recordError("pass '" + pass.name + "' has a hole in its color attachments");
                return false;
            }
            const TextureResource* resource = textureResource(pass.colorAttachments[index]);
            if (resource == nullptr) {
                recordError("pass '" + pass.name + "' references an invalid color attachment");
                return false;
            }
            const uint16_t mip = index < pass.colorAttachmentMips.size() ? pass.colorAttachmentMips[index] : 0;
            if (mip >= resource->desc.mipCount) {
                recordError("pass '" + pass.name + "' references an invalid color attachment mip");
                return false;
            }
        }
        if (pass.hasDepthAttachment) {
            const TextureResource* resource = textureResource(pass.depthAttachment);
            if (!pass.depthAttachment.valid() || resource == nullptr) {
                recordError("pass '" + pass.name + "' references an invalid depth attachment");
                return false;
            }
            if (pass.depthAttachmentMip >= resource->desc.mipCount) {
                recordError("pass '" + pass.name + "' references an invalid depth attachment mip");
                return false;
            }
        }
        if ((pass.viewWidth == 0) != (pass.viewHeight == 0)) {
            recordError("pass '" + pass.name + "' has an incomplete view size");
            return false;
        }

        for (RenderGraphTextureHandle write : pass.textureWrites) {
            if (textureResource(write) == nullptr) {
                recordError("pass '" + pass.name + "' references an invalid texture write");
                return false;
            }
        }
        for (RenderGraphBufferHandle write : pass.bufferWrites) {
            if (bufferResource(write) == nullptr) {
                recordError("pass '" + pass.name + "' references an invalid buffer write");
                return false;
            }
        }

        for (RenderGraphTextureHandle write : pass.textureWrites) {
            textureWritten[write.index] = true;
        }
        for (RenderGraphBufferHandle write : pass.bufferWrites) {
            bufferWritten[write.index] = true;
        }
        for (RenderGraphTextureHandle read : pass.textureReads) {
            const TextureResource* resource = textureResource(read);
            if (resource == nullptr) {
                recordError("pass '" + pass.name + "' references an invalid texture read");
                return false;
            }
            if (!resource->isImported && !textureWritten[read.index]) {
                recordError("pass '" + pass.name + "' reads transient texture '" + resource->name +
                            "' before it is written");
                return false;
            }
        }
        for (RenderGraphBufferHandle read : pass.bufferReads) {
            const BufferResource* resource = bufferResource(read);
            if (resource == nullptr) {
                recordError("pass '" + pass.name + "' references an invalid buffer read");
                return false;
            }
            if (!resource->isImported && !bufferWritten[read.index]) {
                recordError("pass '" + pass.name + "' reads transient buffer '" + resource->name +
                            "' before it is written");
                return false;
            }
        }
    }
    return m_error.empty();
}

void RenderGraph::cullPasses() {
    for (Pass& pass : m_passes) {
        pass.live = !pass.allowCulling || pass.backbuffer || pass.sideEffect;
    }

    bool changed = true;
    while (changed) {
        changed = false;
        std::vector<bool> neededTextures(m_textures.size(), false);
        std::vector<bool> neededBuffers(m_buffers.size(), false);
        for (const Pass& pass : m_passes) {
            if (!pass.live) {
                continue;
            }
            for (RenderGraphTextureHandle read : pass.textureReads) {
                neededTextures[read.index] = true;
            }
            for (RenderGraphBufferHandle read : pass.bufferReads) {
                neededBuffers[read.index] = true;
            }
        }
        for (Pass& pass : m_passes) {
            if (pass.live) {
                continue;
            }
            for (RenderGraphTextureHandle write : pass.textureWrites) {
                if (neededTextures[write.index]) {
                    pass.live = true;
                    changed = true;
                    break;
                }
            }
            if (pass.live) {
                continue;
            }
            for (RenderGraphBufferHandle write : pass.bufferWrites) {
                if (neededBuffers[write.index]) {
                    pass.live = true;
                    changed = true;
                    break;
                }
            }
        }
    }
}

void RenderGraph::assignViewIds() {
    m_passViewIds.assign(m_passes.size(), 0);
    uint32_t nextView = kRenderGraphViewBase;
    bool backbufferSeen = false;
    bool backbufferAssigned = false;
    for (Pass& pass : m_passes) {
        if (!pass.live) {
            continue;
        }
        if (pass.backbuffer) {
            if (backbufferAssigned) {
                recordError("more than one pass writes the backbuffer");
                return;
            }
            pass.viewId = kRenderGraphBackbufferView;
            m_passViewIds[pass.index] = pass.viewId;
            backbufferSeen = true;
            backbufferAssigned = true;
            continue;
        }
        if (backbufferSeen) {
            recordError("a live pass is recorded after the backbuffer pass");
            return;
        }
        if (nextView >= kRenderGraphBackbufferView) {
            recordError("render graph exhausted available view ids");
            return;
        }
        pass.viewId = static_cast<bgfx::ViewId>(nextView++);
        m_passViewIds[pass.index] = pass.viewId;
    }
}

void RenderGraph::computeLifetimes() {
    for (TextureResource& texture : m_textures) {
        texture.firstUse = UINT32_MAX;
        texture.lastUse = 0;
    }
    for (BufferResource& buffer : m_buffers) {
        buffer.firstUse = UINT32_MAX;
        buffer.lastUse = 0;
    }

    for (const Pass& pass : m_passes) {
        if (!pass.live) {
            continue;
        }
        const auto touchTexture = [&pass](TextureResource& resource) {
            resource.firstUse = std::min(resource.firstUse, pass.index);
            resource.lastUse = std::max(resource.lastUse, pass.index);
        };
        for (RenderGraphTextureHandle read : pass.textureReads) {
            TextureResource* resource = textureResource(read);
            if (resource != nullptr && !resource->isImported) {
                touchTexture(*resource);
            }
        }
        for (RenderGraphTextureHandle write : pass.textureWrites) {
            TextureResource* resource = textureResource(write);
            if (resource != nullptr && !resource->isImported) {
                touchTexture(*resource);
            }
        }
        const auto touchBuffer = [&pass](BufferResource& resource) {
            resource.firstUse = std::min(resource.firstUse, pass.index);
            resource.lastUse = std::max(resource.lastUse, pass.index);
        };
        for (RenderGraphBufferHandle read : pass.bufferReads) {
            BufferResource* resource = bufferResource(read);
            if (resource != nullptr && !resource->isImported) {
                touchBuffer(*resource);
            }
        }
        for (RenderGraphBufferHandle write : pass.bufferWrites) {
            BufferResource* resource = bufferResource(write);
            if (resource != nullptr && !resource->isImported) {
                touchBuffer(*resource);
            }
        }
    }
}

bool RenderGraph::compile() {
    m_compiled = false;
    m_passViewIds.clear();
    m_diagnostics = {};
    m_diagnostics.recordedPasses = static_cast<uint32_t>(m_passes.size());

    if (m_passes.empty()) {
        recordError("render graph has no passes");
        return false;
    }
    if (!m_error.empty()) {
        return false;
    }
    if (!validatePasses()) {
        return false;
    }

    cullPasses();
    assignViewIds();
    if (!m_error.empty()) {
        return false;
    }
    computeLifetimes();

    m_diagnostics.livePasses = 0;
    for (const Pass& pass : m_passes) {
        if (pass.live) {
            ++m_diagnostics.livePasses;
        }
    }
    m_diagnostics.culledPasses = m_diagnostics.recordedPasses - m_diagnostics.livePasses;
    for (const TextureResource& texture : m_textures) {
        if (!texture.isImported && texture.firstUse != UINT32_MAX) {
            ++m_diagnostics.transientTextures;
        }
    }
    m_compiled = true;
    return true;
}

gfx::TextureHandle RenderGraph::acquirePooledTexture(gfx::ResourceManager& resources, const TextureResource& resource) {
    PooledTexture* available = nullptr;
    for (PooledTexture& entry : m_pooledTextures) {
        if (!sameTextureShape(entry.desc, resource.desc)) {
            continue;
        }
        const bool lifetimeFree = entry.reservedUntil == UINT32_MAX || entry.reservedUntil < resource.firstUse;
        const bool frameSafe =
            entry.lastUsedFrame == m_frameIndex || entry.lastUsedFrame + kFramesInFlight <= m_frameIndex;
        if (lifetimeFree && frameSafe) {
            available = &entry;
            break;
        }
    }

    if (available == nullptr) {
        gfx::TextureDesc desc = resource.desc;
        desc.name = resource.name + ".pool";
        PooledTexture entry;
        entry.desc = desc;
        entry.handle = resources.createTexture(desc);
        entry.lastUsedFrame = m_frameIndex;
        entry.reservedUntil = UINT32_MAX;
        if (!entry.handle.valid()) {
            recordError("failed to create transient texture '" + resource.name + "'");
            return {};
        }
        m_pooledTextures.push_back(std::move(entry));
        available = &m_pooledTextures.back();
    }

    available->lastUsedFrame = m_frameIndex;
    available->reservedUntil = resource.lastUse;
    return available->handle;
}

gfx::BufferHandle RenderGraph::acquirePooledBuffer(gfx::ResourceManager& resources, const BufferResource& resource) {
    PooledBuffer* available = nullptr;
    for (PooledBuffer& entry : m_pooledBuffers) {
        if (!sameBufferShape(entry.desc, resource.desc)) {
            continue;
        }
        const bool lifetimeFree = entry.reservedUntil == UINT32_MAX || entry.reservedUntil < resource.firstUse;
        const bool frameSafe =
            entry.lastUsedFrame == m_frameIndex || entry.lastUsedFrame + kFramesInFlight <= m_frameIndex;
        if (lifetimeFree && frameSafe) {
            available = &entry;
            break;
        }
    }
    if (available == nullptr) {
        PooledBuffer entry;
        entry.desc = resource.desc;
        entry.desc.name = resource.name + ".pool";
        entry.handle = resources.createBuffer(entry.desc);
        entry.lastUsedFrame = m_frameIndex;
        entry.reservedUntil = UINT32_MAX;
        if (!entry.handle.valid()) {
            recordError("failed to create transient buffer '" + resource.name + "'");
            return {};
        }
        m_pooledBuffers.push_back(std::move(entry));
        available = &m_pooledBuffers.back();
    }
    available->lastUsedFrame = m_frameIndex;
    available->reservedUntil = resource.lastUse;
    return available->handle;
}

gfx::FrameBufferHandle RenderGraph::acquireFramebuffer(gfx::ResourceManager& resources,
                                                       const std::vector<gfx::TextureHandle>& attachments,
                                                       const std::vector<uint16_t>& mips, const std::string& name) {
    if (attachments.empty()) {
        return {};
    }
    gfx::FrameBufferKey key{attachments, mips};
    for (PooledFramebuffer& entry : m_pooledFramebuffers) {
        if (entry.key == key) {
            entry.lastUsedFrame = m_frameIndex;
            return entry.handle;
        }
    }
    gfx::FrameBufferHandle handle = resources.createFrameBuffer(key, name);
    if (!handle.valid()) {
        recordError("failed to create framebuffer for pass '" + name + "'");
        return {};
    }
    m_pooledFramebuffers.push_back(PooledFramebuffer{std::move(key), handle, m_frameIndex});
    return handle;
}

bool RenderGraph::allocateResources(gfx::ResourceManager& resources, std::vector<gfx::TextureHandle>& textures,
                                    std::vector<gfx::BufferHandle>& buffers) {
    textures.assign(m_textures.size(), {});
    buffers.assign(m_buffers.size(), {});

    for (std::size_t index = 0; index < m_textures.size(); ++index) {
        const TextureResource& resource = m_textures[index];
        if (resource.isImported) {
            textures[index] = resource.imported;
        } else if (resource.firstUse != UINT32_MAX) {
            textures[index] = acquirePooledTexture(resources, resource);
            if (!textures[index].valid()) {
                return false;
            }
        }
    }
    for (std::size_t index = 0; index < m_buffers.size(); ++index) {
        const BufferResource& resource = m_buffers[index];
        if (resource.isImported) {
            buffers[index] = resource.imported;
        } else if (resource.firstUse != UINT32_MAX) {
            buffers[index] = acquirePooledBuffer(resources, resource);
            if (!buffers[index].valid()) {
                return false;
            }
        }
    }
    return true;
}

void RenderGraph::releasePool(gfx::ResourceManager& resources) {
    for (PooledFramebuffer& entry : m_pooledFramebuffers) {
        if (entry.handle.valid()) {
            resources.destroyFrameBuffer(entry.handle);
        }
    }
    m_pooledFramebuffers.clear();
    for (PooledTexture& entry : m_pooledTextures) {
        if (entry.handle.valid()) {
            resources.destroyTexture(entry.handle);
        }
    }
    m_pooledTextures.clear();
    for (PooledBuffer& entry : m_pooledBuffers) {
        if (entry.handle.valid()) {
            resources.destroyBuffer(entry.handle);
        }
    }
    m_pooledBuffers.clear();
}

void RenderGraph::prunePool(gfx::ResourceManager& resources) {
    const bool staleTexture =
        std::any_of(m_pooledTextures.begin(), m_pooledTextures.end(), [this](const PooledTexture& entry) {
            return entry.lastUsedFrame + kFramesInFlight < m_frameIndex;
        });
    if (staleTexture) {
        for (PooledFramebuffer& entry : m_pooledFramebuffers) {
            if (entry.handle.valid()) {
                resources.destroyFrameBuffer(entry.handle);
            }
        }
        m_pooledFramebuffers.clear();
    }

    for (std::size_t index = 0; index < m_pooledTextures.size();) {
        PooledTexture& entry = m_pooledTextures[index];
        if (entry.lastUsedFrame + kFramesInFlight < m_frameIndex) {
            if (entry.handle.valid()) {
                resources.destroyTexture(entry.handle);
            }
            m_pooledTextures.erase(m_pooledTextures.begin() + static_cast<std::ptrdiff_t>(index));
        } else {
            ++index;
        }
    }
    for (std::size_t index = 0; index < m_pooledBuffers.size();) {
        PooledBuffer& entry = m_pooledBuffers[index];
        if (entry.lastUsedFrame + kFramesInFlight < m_frameIndex) {
            if (entry.handle.valid()) {
                resources.destroyBuffer(entry.handle);
            }
            m_pooledBuffers.erase(m_pooledBuffers.begin() + static_cast<std::ptrdiff_t>(index));
        } else {
            ++index;
        }
    }
    for (std::size_t index = 0; index < m_pooledFramebuffers.size();) {
        PooledFramebuffer& entry = m_pooledFramebuffers[index];
        if (entry.lastUsedFrame + kFramesInFlight < m_frameIndex) {
            if (entry.handle.valid()) {
                resources.destroyFrameBuffer(entry.handle);
            }
            m_pooledFramebuffers.erase(m_pooledFramebuffers.begin() + static_cast<std::ptrdiff_t>(index));
        } else {
            ++index;
        }
    }
}

std::vector<std::string> RenderGraph::passNames() const {
    std::vector<std::string> names;
    names.reserve(m_passes.size());
    for (const Pass& pass : m_passes) {
        names.push_back(pass.name);
    }
    return names;
}

bool RenderGraph::execute(gfx::Encoder& encoder, gfx::ResourceManager& resources) {
    if (!m_compiled && !compile()) {
        HBG_LOGE("render graph '{}': {}", m_name, m_error);
        return false;
    }
    if (m_poolWidth != m_width || m_poolHeight != m_height) {
        releasePool(resources);
        m_poolWidth = m_width;
        m_poolHeight = m_height;
    }
    prunePool(resources);

    std::vector<gfx::TextureHandle> textures;
    std::vector<gfx::BufferHandle> buffers;
    if (!allocateResources(resources, textures, buffers)) {
        HBG_LOGE("render graph '{}': {}", m_name, m_error);
        return false;
    }

    for (Pass& pass : m_passes) {
        if (!pass.live) {
            continue;
        }
        std::vector<gfx::TextureHandle> attachments;
        std::vector<uint16_t> attachmentMips;
        for (std::size_t index = 0; index < pass.colorAttachments.size(); ++index) {
            attachments.push_back(textures[pass.colorAttachments[index].index]);
            attachmentMips.push_back(index < pass.colorAttachmentMips.size() ? pass.colorAttachmentMips[index] : 0);
        }
        if (pass.hasDepthAttachment) {
            attachments.push_back(textures[pass.depthAttachment.index]);
            attachmentMips.push_back(pass.depthAttachmentMip);
        }
        gfx::FrameBufferHandle frameBuffer =
            pass.backbuffer ? gfx::FrameBufferHandle{}
                            : acquireFramebuffer(resources, attachments, attachmentMips, pass.name);
        if (!pass.backbuffer && !attachments.empty() && !frameBuffer.valid()) {
            HBG_LOGE("render graph '{}': pass '{}' has no valid framebuffer", m_name, pass.name);
            return false;
        }


        encoder.setViewName(pass.viewId, pass.name.c_str());
        const uint16_t viewWidth = pass.viewWidth != 0 ? pass.viewWidth : m_width;
        const uint16_t viewHeight = pass.viewHeight != 0 ? pass.viewHeight : m_height;
        RenderGraphContext context(encoder, resources, pass.viewId, viewWidth, viewHeight, frameBuffer, pass.backbuffer,
                                   textures, buffers);
        pass.renderFunc(context);
    }

    m_diagnostics.pooledTextures = static_cast<uint32_t>(m_pooledTextures.size());
    m_diagnostics.framebuffers = static_cast<uint32_t>(m_pooledFramebuffers.size());
    return true;
}

void RenderGraph::shutdown(gfx::ResourceManager& resources) {
    releasePool(resources);
    m_textures.clear();
    m_buffers.clear();
    m_passes.clear();
    m_passViewIds.clear();
    m_poolWidth = 0;
    m_poolHeight = 0;
    m_compiled = false;
}

} // namespace hbg::render
