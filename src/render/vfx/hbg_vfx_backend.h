#pragma once

#include "gfx/buffer.h"
#include "gfx/encoder.h"
#include "gfx/resource_manager.h"

#include "vfx/command_sink.h"
#include "vfx/device.h"
#include "vfx/shading.h"

#include <bgfx/bgfx.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace hbg::vfxbridge {

class HbgVfxBackend final : public ::vfx::Device, public ::vfx::CommandSink, public ::vfx::ShadingProvider {
  public:
    explicit HbgVfxBackend(gfx::ResourceManager& resources);
    ~HbgVfxBackend() override;

    HbgVfxBackend(const HbgVfxBackend&) = delete;
    HbgVfxBackend& operator=(const HbgVfxBackend&) = delete;

    ::vfx::Device& device() { return *this; }
    ::vfx::CommandSink& commandSink() { return *this; }
    ::vfx::ShadingProvider& shading() { return *this; }

    void begin(gfx::Encoder& encoder, bgfx::ViewId view);
    void end();

    ::vfx::BufferHandle createBuffer(const ::vfx::BufferDesc& desc) override;
    void destroyBuffer(::vfx::BufferHandle handle) override;
    bool updateBuffer(::vfx::BufferHandle handle, std::span<const std::byte> data, std::uint32_t offset = 0) override;

    ::vfx::TextureHandle createTexture(const ::vfx::TextureDesc& desc) override;
    ::vfx::TextureHandle importTexture(gfx::TextureHandle handle);
    void destroyTexture(::vfx::TextureHandle handle) override;
    bool updateTexture(::vfx::TextureHandle handle, std::span<const std::byte> data,
                       std::uint32_t rowPitch) override;

    ::vfx::ProgramHandle createProgram(std::span<const ::vfx::ShaderBlob> blobs) override;
    void destroyProgram(::vfx::ProgramHandle handle) override;

    ::vfx::UniformHandle findUniform(std::string_view name, ::vfx::UniformType type,
                                     std::uint16_t count = 1) override;
    ::vfx::SamplerHandle findSampler(std::string_view name) override;

    ::vfx::ShaderBackend backend() const override;
    ::vfx::Limits limits() const override;

    void beginPass(const ::vfx::PassDesc& desc) override;
    void endPass() override;
    void setStorageBuffer(std::uint8_t stage, ::vfx::BufferHandle buffer, ::vfx::Access access) override;
    void setTexture(std::uint8_t stage, ::vfx::SamplerHandle sampler, ::vfx::TextureHandle texture) override;
    void setUniform(::vfx::UniformHandle uniform, const void* data, std::uint16_t count = 1) override;
    void setState(const ::vfx::DrawState& state) override;
    void dispatch(::vfx::ProgramHandle program, std::uint32_t x, std::uint32_t y, std::uint32_t z) override;
    void drawIndirect(::vfx::ProgramHandle program, ::vfx::BufferHandle indirect, std::uint32_t start = 0,
                      std::uint32_t num = 1) override;

    void bindLighting(::vfx::CommandSink& sink, const ::vfx::FrameContext& frame,
                      const ::vfx::LitSettings& settings) override;
    ::vfx::ProgramHandle resolveProgram(const ::vfx::RenderOutputDesc& output,
                                        ::vfx::ShaderBackend backend) override;

    std::size_t liveBufferCount() const;
    std::size_t liveTextureCount() const;
    std::size_t liveProgramCount() const;
    std::size_t liveUniformCount() const;
    std::size_t liveSamplerCount() const;
    std::uint32_t lightingBindCount() const { return m_lightingBindCount; }

    gfx::BufferHandle nativeBuffer(::vfx::BufferHandle handle) const;
    ::vfx::BufferHandle findBuffer(std::string_view name) const;

  private:
    struct BufferEntry {
        gfx::BufferHandle handle{};
        std::string name;
        ::vfx::BufferUsage usage = ::vfx::BufferUsage::Storage;
        std::uint32_t stride = 4;
        bool alive = false;
    };

    struct TextureEntry {
        gfx::TextureHandle handle{};
        std::uint16_t width = 0;
        std::uint16_t height = 0;
        bool alive = false;
        bool owned = true;
    };

    struct ProgramEntry {
        gfx::ProgramHandle handle{};
        bool alive = false;
    };

    struct UniformEntry {
        gfx::UniformHandle handle{};
        ::vfx::UniformType type = ::vfx::UniformType::Vec4;
        std::uint16_t count = 1;
        bool alive = false;
        bool owned = true;
    };

    struct SamplerEntry {
        gfx::UniformHandle handle{};
        bool alive = false;
        bool owned = true;
    };

    gfx::BufferHandle ensureDummyVertexBuffer();
    void destroyAll();

    gfx::ResourceManager& m_resources;
    gfx::Encoder* m_encoder = nullptr;
    bgfx::ViewId m_view = 0;
    gfx::BufferHandle m_dummyVertexBuffer{};

    std::vector<BufferEntry> m_buffers;
    std::vector<TextureEntry> m_textures;
    std::unordered_map<uint16_t, ::vfx::TextureHandle> m_importedTextures;
    std::vector<ProgramEntry> m_programs;
    std::vector<UniformEntry> m_uniforms;
    std::vector<SamplerEntry> m_samplers;
    std::unordered_map<std::string, ::vfx::UniformHandle> m_uniformsByName;
    std::unordered_map<std::string, ::vfx::SamplerHandle> m_samplersByName;
    std::uint32_t m_lightingBindCount = 0;
};

} // namespace hbg::vfxbridge
