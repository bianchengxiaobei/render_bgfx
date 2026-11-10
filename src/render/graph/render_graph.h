#pragma once

#include "core/handle.h"
#include "gfx/buffer.h"
#include "gfx/encoder.h"
#include "gfx/framebuffer_cache.h"
#include "gfx/resource_manager.h"
#include "gfx/texture.h"

#include <bgfx/bgfx.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace hbg::render {

struct RenderGraphTextureTag {};
using RenderGraphTextureHandle = Handle<RenderGraphTextureTag>;

struct RenderGraphBufferTag {};
using RenderGraphBufferHandle = Handle<RenderGraphBufferTag>;

inline constexpr bgfx::ViewId kRenderGraphViewBase = 8;
inline constexpr bgfx::ViewId kRenderGraphBackbufferView = 4094;
inline constexpr bgfx::ViewId kDebugUiView = 4095;

enum class RenderGraphPassType : uint8_t {
    Raster,
    Compute,
};

struct RenderGraphDiagnostics {
    uint32_t recordedPasses = 0;
    uint32_t livePasses = 0;
    uint32_t culledPasses = 0;
    uint32_t transientTextures = 0;
    uint32_t pooledTextures = 0;
    uint32_t framebuffers = 0;
};

using RenderGraphPassFunc = std::function<void(class RenderGraphContext&)>;

class RenderGraphContext {
  public:
    gfx::Encoder& encoder() const { return m_encoder; }
    gfx::ResourceManager& resources() const { return m_resources; }
    bgfx::ViewId viewId() const { return m_viewId; }
    uint16_t width() const { return m_width; }
    uint16_t height() const { return m_height; }
    bool backbuffer() const { return m_backbuffer; }

    gfx::FrameBufferHandle frameBuffer() const { return m_frameBuffer; }
    gfx::TextureHandle texture(RenderGraphTextureHandle handle) const;
    gfx::BufferHandle buffer(RenderGraphBufferHandle handle) const;

  private:
    friend class RenderGraph;

    RenderGraphContext(gfx::Encoder& encoder, gfx::ResourceManager& resources, bgfx::ViewId viewId, uint16_t width,
                       uint16_t height, gfx::FrameBufferHandle frameBuffer, bool backbuffer,
                       const std::vector<gfx::TextureHandle>& textures, const std::vector<gfx::BufferHandle>& buffers);

    gfx::Encoder& m_encoder;
    gfx::ResourceManager& m_resources;
    bgfx::ViewId m_viewId = 0;
    uint16_t m_width = 0;
    uint16_t m_height = 0;
    gfx::FrameBufferHandle m_frameBuffer;
    bool m_backbuffer = false;
    const std::vector<gfx::TextureHandle>& m_textures;
    const std::vector<gfx::BufferHandle>& m_buffers;
};

class RenderGraphBuilder {
  public:
    RenderGraphBuilder(const RenderGraphBuilder&) = delete;
    RenderGraphBuilder& operator=(const RenderGraphBuilder&) = delete;

    RenderGraphBuilder& readTexture(RenderGraphTextureHandle handle);
    RenderGraphBuilder& writeTexture(RenderGraphTextureHandle handle);
    RenderGraphBuilder& setColorAttachment(RenderGraphTextureHandle handle, uint8_t index = 0, uint16_t mip = 0);
    RenderGraphBuilder& setDepthAttachment(RenderGraphTextureHandle handle, uint16_t mip = 0);

    RenderGraphBuilder& readBuffer(RenderGraphBufferHandle handle);
    RenderGraphBuilder& writeBuffer(RenderGraphBufferHandle handle);

    RenderGraphBuilder& writeBackbuffer();
    RenderGraphBuilder& allowCulling(bool enabled);
    RenderGraphBuilder& setRenderFunc(RenderGraphPassFunc func);

  private:
    friend class RenderGraph;

    RenderGraphBuilder(RenderGraph& graph, uint32_t passIndex);

    RenderGraph& m_graph;
    uint32_t m_passIndex = 0;
};

class RenderGraph {
  public:
    explicit RenderGraph(std::string name = "RenderGraph");
    ~RenderGraph();

    RenderGraph(const RenderGraph&) = delete;
    RenderGraph& operator=(const RenderGraph&) = delete;

    const std::string& name() const { return m_name; }

    void beginFrame(uint16_t width, uint16_t height, uint32_t frameIndex);

    RenderGraphTextureHandle importTexture(gfx::TextureHandle texture, std::string name = {});
    RenderGraphTextureHandle createTexture(const gfx::TextureDesc& desc, std::string name = {});
    RenderGraphBufferHandle importBuffer(gfx::BufferHandle buffer, std::string name = {});
    RenderGraphBufferHandle createBuffer(const gfx::BufferDesc& desc, std::string name = {});

    RenderGraphBuilder addRasterPass(std::string name);
    RenderGraphBuilder addComputePass(std::string name);

    bool compile();
    bool execute(gfx::Encoder& encoder, gfx::ResourceManager& resources);

    void shutdown(gfx::ResourceManager& resources);

    bool compiled() const { return m_compiled; }
    const std::string& lastError() const { return m_error; }
    const RenderGraphDiagnostics& diagnostics() const { return m_diagnostics; }
    const std::vector<bgfx::ViewId>& passViewIds() const { return m_passViewIds; }

  private:
    friend class RenderGraphBuilder;

    struct TextureResource {
        std::string name;
        gfx::TextureDesc desc;
        gfx::TextureHandle imported;
        bool isImported = false;
        uint32_t generation = 0;
        uint32_t firstUse = UINT32_MAX;
        uint32_t lastUse = 0;
    };

    struct BufferResource {
        std::string name;
        gfx::BufferDesc desc;
        gfx::BufferHandle imported;
        bool isImported = false;
        uint32_t generation = 0;
        uint32_t firstUse = UINT32_MAX;
        uint32_t lastUse = 0;
    };

    struct Pass {
        uint32_t index = 0;
        std::string name;
        RenderGraphPassType type = RenderGraphPassType::Raster;
        std::vector<RenderGraphTextureHandle> textureReads;
        std::vector<RenderGraphTextureHandle> textureWrites;
        std::vector<RenderGraphBufferHandle> bufferReads;
        std::vector<RenderGraphBufferHandle> bufferWrites;
        std::vector<RenderGraphTextureHandle> colorAttachments;
        std::vector<uint16_t> colorAttachmentMips;
        RenderGraphTextureHandle depthAttachment;
        uint16_t depthAttachmentMip = 0;
        bool hasDepthAttachment = false;
        bool backbuffer = false;
        bool allowCulling = true;
        bool sideEffect = false;
        bool live = false;
        RenderGraphPassFunc renderFunc;
        bgfx::ViewId viewId = 0;
    };

    struct PooledTexture {
        gfx::TextureDesc desc;
        gfx::TextureHandle handle;
        uint32_t lastUsedFrame = 0;
        uint32_t reservedUntil = UINT32_MAX;
    };

    struct PooledFramebuffer {
        gfx::FrameBufferKey key;
        gfx::FrameBufferHandle handle;
        uint32_t lastUsedFrame = 0;
    };

    struct PooledBuffer {
        gfx::BufferDesc desc;
        gfx::BufferHandle handle;
        uint32_t lastUsedFrame = 0;
        uint32_t reservedUntil = UINT32_MAX;
    };

    void resetRecording();
    void recordError(std::string message);

    TextureResource* textureResource(RenderGraphTextureHandle handle);
    const TextureResource* textureResource(RenderGraphTextureHandle handle) const;
    BufferResource* bufferResource(RenderGraphBufferHandle handle);
    const BufferResource* bufferResource(RenderGraphBufferHandle handle) const;

    void addTextureRead(uint32_t passIndex, RenderGraphTextureHandle handle);
    void addTextureWrite(uint32_t passIndex, RenderGraphTextureHandle handle);
    void addBufferRead(uint32_t passIndex, RenderGraphBufferHandle handle);
    void addBufferWrite(uint32_t passIndex, RenderGraphBufferHandle handle);
    void setColorAttachment(uint32_t passIndex, RenderGraphTextureHandle handle, uint8_t index, uint16_t mip);
    void setDepthAttachment(uint32_t passIndex, RenderGraphTextureHandle handle, uint16_t mip);
    void setBackbuffer(uint32_t passIndex);
    void setAllowCulling(uint32_t passIndex, bool enabled);
    void setRenderFunc(uint32_t passIndex, RenderGraphPassFunc func);

    bool validatePasses();
    void cullPasses();
    void assignViewIds();
    void computeLifetimes();
    bool allocateResources(gfx::ResourceManager& resources, std::vector<gfx::TextureHandle>& textures,
                           std::vector<gfx::BufferHandle>& buffers);
    gfx::FrameBufferHandle acquireFramebuffer(gfx::ResourceManager& resources,
                                              const std::vector<gfx::TextureHandle>& attachments,
                                              const std::vector<uint16_t>& mips, const std::string& name);
    gfx::TextureHandle acquirePooledTexture(gfx::ResourceManager& resources, const TextureResource& resource);
    gfx::BufferHandle acquirePooledBuffer(gfx::ResourceManager& resources, const BufferResource& resource);
    void releasePool(gfx::ResourceManager& resources);
    void prunePool(gfx::ResourceManager& resources);

    std::string m_name;
    std::string m_error;
    uint32_t m_generation = 0;
    uint32_t m_frameIndex = 0;
    uint16_t m_width = 0;
    uint16_t m_height = 0;
    uint16_t m_poolWidth = 0;
    uint16_t m_poolHeight = 0;

    std::vector<TextureResource> m_textures;
    std::vector<BufferResource> m_buffers;
    std::vector<Pass> m_passes;
    std::vector<bgfx::ViewId> m_passViewIds;
    std::vector<PooledTexture> m_pooledTextures;
    std::vector<PooledFramebuffer> m_pooledFramebuffers;
    std::vector<PooledBuffer> m_pooledBuffers;

    RenderGraphDiagnostics m_diagnostics;
    bool m_compiled = false;
};

} // namespace hbg::render
