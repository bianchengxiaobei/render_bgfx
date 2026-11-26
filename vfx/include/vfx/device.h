#pragma once

#include "vfx/handles.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace vfx {

enum class BufferUsage : std::uint8_t { Storage, StorageVertex, Indirect, Dynamic };
enum class TextureUsage : std::uint8_t { Sampled, RenderTarget, Storage };
enum class ShaderStage : std::uint8_t { Vertex, Fragment, Compute };
enum class ShaderBackend : std::uint8_t { Dxbc, Spirv };
enum class Access : std::uint8_t { Read, Write, ReadWrite };

struct BufferDesc {
    BufferUsage usage = BufferUsage::Storage;
    std::uint32_t size = 0;
    std::uint32_t stride = 4;
    const void* initialData = nullptr;
    std::string_view name;
};

struct TextureDesc {
    std::uint16_t width = 1;
    std::uint16_t height = 1;
    std::uint8_t mipCount = 1;
    TextureUsage usage = TextureUsage::Sampled;
    TextureFormat format = TextureFormat::RGBA16F;
    const void* initialData = nullptr;
    std::uint32_t dataSize = 0;
    std::string_view name;
};

struct ShaderBlob {
    ShaderStage stage = ShaderStage::Compute;
    ShaderBackend backend = ShaderBackend::Dxbc;
    std::span<const std::byte> data;
};

class Device {
  public:
    virtual ~Device() = default;

    virtual BufferHandle createBuffer(const BufferDesc& desc) = 0;
    virtual void destroyBuffer(BufferHandle handle) = 0;
    virtual bool updateBuffer(BufferHandle handle, std::span<const std::byte> data, std::uint32_t offset = 0) = 0;

    virtual ReadbackHandle readBufferAsync(BufferHandle handle, std::uint32_t size) = 0;
    virtual bool readbackReady(ReadbackHandle handle) const = 0;
    virtual bool copyReadback(ReadbackHandle handle, std::span<std::byte> data) const = 0;
    virtual void releaseReadback(ReadbackHandle handle) = 0;

    virtual TextureHandle createTexture(const TextureDesc& desc) = 0;
    virtual void destroyTexture(TextureHandle handle) = 0;
    virtual bool updateTexture(TextureHandle handle, std::span<const std::byte> data, std::uint32_t rowPitch) = 0;

    virtual ProgramHandle createProgram(std::span<const ShaderBlob> blobs) = 0;
    virtual void destroyProgram(ProgramHandle handle) = 0;

    virtual UniformHandle findUniform(std::string_view name, UniformType type, std::uint16_t count = 1) = 0;
    virtual SamplerHandle findSampler(std::string_view name) = 0;

    virtual ShaderBackend backend() const = 0;
    virtual Limits limits() const = 0;
};

} // namespace vfx
