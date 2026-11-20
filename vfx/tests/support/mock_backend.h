#pragma once

#include "vfx/command_sink.h"
#include "vfx/device.h"
#include "vfx/shading.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace vfx::test {

struct MockCommand {
    enum class Type : std::uint8_t {
        BeginPass,
        EndPass,
        SetStorageBuffer,
        SetTexture,
        SetUniform,
        SetState,
        Dispatch,
        DrawIndirect,
    };

    Type type = Type::BeginPass;
    std::string passName;
    bool simulation = true;
    bool readsSceneDepth = false;
    bool readsSceneColor = false;
    std::uint8_t stage = 0;
    Access access = Access::Read;
    BufferHandle buffer{};
    TextureHandle texture{};
    UniformHandle uniform{};
    SamplerHandle sampler{};
    ProgramHandle program{};
    std::uint16_t count = 0;
    std::uint32_t start = 0;
    std::uint32_t num = 0;
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t z = 0;
    DrawState state{};
    std::vector<std::byte> data;
};

class MockBackend final : public Device, public CommandSink, public ShadingProvider {
  public:
    struct BlobRecord {
        ShaderStage stage = ShaderStage::Compute;
        ShaderBackend backend = ShaderBackend::Dxbc;
        std::size_t size = 0;
    };

    struct BufferSlot {
        BufferDesc desc{};
        std::string name;
        bool alive = false;
        std::uint32_t updateCount = 0;
        std::uint32_t lastUpdateOffset = 0;
        std::size_t lastUpdateSize = 0;
    };

    struct TextureSlot {
        TextureDesc desc{};
        std::string name;
        bool alive = false;
        std::uint32_t updateCount = 0;
        std::uint32_t lastRowPitch = 0;
        std::size_t lastUpdateSize = 0;
    };

    struct ProgramSlot {
        bool alive = false;
        std::vector<BlobRecord> blobs;
    };

    struct UniformSlot {
        std::string name;
        UniformType type = UniformType::Vec4;
        std::uint16_t count = 1;
        bool alive = false;
    };

    struct SamplerSlot {
        std::string name;
        bool alive = false;
    };

    BufferHandle createBuffer(const BufferDesc& desc) override {
        BufferSlot slot;
        slot.desc = desc;
        slot.desc.name = {};
        slot.name.assign(desc.name);
        slot.alive = true;
        m_buffers.push_back(std::move(slot));
        return BufferHandle{static_cast<Index>(m_buffers.size() - 1)};
    }

    void destroyBuffer(BufferHandle handle) override {
        if (BufferSlot* slot = bufferSlot(handle); slot != nullptr) {
            slot->alive = false;
            ++m_destroyedBuffers;
        }
    }

    bool updateBuffer(BufferHandle handle, std::span<const std::byte> data, std::uint32_t offset = 0) override {
        BufferSlot* slot = bufferSlot(handle);
        if (slot == nullptr || !slot->alive) {
            return false;
        }
        if (offset + data.size() > slot->desc.size) {
            return false;
        }
        slot->lastUpdateOffset = offset;
        slot->lastUpdateSize = data.size();
        slot->updateCount++;
        return true;
    }

    TextureHandle createTexture(const TextureDesc& desc) override {
        TextureSlot slot;
        slot.desc = desc;
        slot.desc.name = {};
        slot.name.assign(desc.name);
        slot.alive = true;
        m_textures.push_back(std::move(slot));
        return TextureHandle{static_cast<Index>(m_textures.size() - 1)};
    }

    void destroyTexture(TextureHandle handle) override {
        if (TextureSlot* slot = textureSlot(handle); slot != nullptr) {
            slot->alive = false;
            ++m_destroyedTextures;
        }
    }

    bool updateTexture(TextureHandle handle, std::span<const std::byte> data, std::uint32_t rowPitch) override {
        TextureSlot* slot = textureSlot(handle);
        if (slot == nullptr || !slot->alive) {
            return false;
        }
        slot->lastRowPitch = rowPitch;
        slot->lastUpdateSize = data.size();
        slot->updateCount++;
        return true;
    }

    ProgramHandle createProgram(std::span<const ShaderBlob> blobs) override {
        if (blobs.empty()) {
            return {};
        }
        ProgramSlot slot;
        slot.alive = true;
        for (const ShaderBlob& blob : blobs) {
            slot.blobs.push_back(BlobRecord{blob.stage, blob.backend, blob.data.size()});
        }
        m_programs.push_back(std::move(slot));
        return ProgramHandle{static_cast<Index>(m_programs.size() - 1)};
    }

    void destroyProgram(ProgramHandle handle) override {
        if (ProgramSlot* slot = programSlot(handle); slot != nullptr) {
            slot->alive = false;
            ++m_destroyedPrograms;
        }
    }

    UniformHandle findUniform(std::string_view name, UniformType type, std::uint16_t count = 1) override {
        const std::string key(name);
        const auto iterator = m_uniformsByName.find(key);
        if (iterator != m_uniformsByName.end()) {
            return iterator->second;
        }
        UniformSlot slot;
        slot.name = key;
        slot.type = type;
        slot.count = count;
        slot.alive = true;
        m_uniforms.push_back(std::move(slot));
        const UniformHandle handle{static_cast<Index>(m_uniforms.size() - 1)};
        m_uniformsByName.emplace(key, handle);
        return handle;
    }

    SamplerHandle findSampler(std::string_view name) override {
        const std::string key(name);
        const auto iterator = m_samplersByName.find(key);
        if (iterator != m_samplersByName.end()) {
            return iterator->second;
        }
        SamplerSlot slot;
        slot.name = key;
        slot.alive = true;
        m_samplers.push_back(std::move(slot));
        const SamplerHandle handle{static_cast<Index>(m_samplers.size() - 1)};
        m_samplersByName.emplace(key, handle);
        return handle;
    }

    ShaderBackend backend() const override { return ShaderBackend::Dxbc; }
    Limits limits() const override { return Limits{16, 16}; }

    void beginPass(const PassDesc& desc) override {
        MockCommand command;
        command.type = MockCommand::Type::BeginPass;
        command.passName.assign(desc.name);
        command.simulation = desc.simulation;
        command.readsSceneDepth = desc.readsSceneDepth;
        command.readsSceneColor = desc.readsSceneColor;
        m_commands.push_back(std::move(command));
    }

    void endPass() override { push(MockCommand::Type::EndPass); }

    void setStorageBuffer(std::uint8_t stage, BufferHandle buffer, Access access) override {
        MockCommand command;
        command.type = MockCommand::Type::SetStorageBuffer;
        command.stage = stage;
        command.buffer = buffer;
        command.access = access;
        m_commands.push_back(std::move(command));
    }

    void setTexture(std::uint8_t stage, SamplerHandle sampler, TextureHandle texture) override {
        MockCommand command;
        command.type = MockCommand::Type::SetTexture;
        command.stage = stage;
        command.sampler = sampler;
        command.texture = texture;
        m_commands.push_back(std::move(command));
    }

    void setUniform(UniformHandle uniform, const void* data, std::uint16_t count = 1) override {
        MockCommand command;
        command.type = MockCommand::Type::SetUniform;
        command.uniform = uniform;
        command.count = count;
        if (data != nullptr && count > 0) {
            const std::uint32_t bytes = uniformBytes(uniform) * count;
            if (bytes > 0) {
                const auto* source = static_cast<const std::byte*>(data);
                command.data.assign(source, source + bytes);
            }
        }
        m_commands.push_back(std::move(command));
    }

    void setState(const DrawState& state) override {
        MockCommand command;
        command.type = MockCommand::Type::SetState;
        command.state = state;
        m_commands.push_back(std::move(command));
    }

    void dispatch(ProgramHandle program, std::uint32_t x, std::uint32_t y, std::uint32_t z) override {
        MockCommand command;
        command.type = MockCommand::Type::Dispatch;
        command.program = program;
        command.x = x;
        command.y = y;
        command.z = z;
        m_commands.push_back(std::move(command));
    }

    void drawIndirect(ProgramHandle program, BufferHandle indirect, std::uint32_t start = 0,
                      std::uint32_t num = 1) override {
        MockCommand command;
        command.type = MockCommand::Type::DrawIndirect;
        command.program = program;
        command.buffer = indirect;
        command.start = start;
        command.num = num;
        m_commands.push_back(std::move(command));
    }

    void bindLighting(CommandSink&, const FrameContext&, const LitSettings& settings) override {
        m_lightingBinds++;
        m_lastLitSettings = settings;
    }

    ProgramHandle resolveProgram(const RenderOutputDesc&, ShaderBackend) override { return {}; }

    const std::vector<MockCommand>& commands() const { return m_commands; }
    void clearCommands() { m_commands.clear(); }

    const std::vector<BufferSlot>& buffers() const { return m_buffers; }
    const std::vector<TextureSlot>& textures() const { return m_textures; }
    const std::vector<ProgramSlot>& programs() const { return m_programs; }
    const std::vector<UniformSlot>& uniforms() const { return m_uniforms; }
    const std::vector<SamplerSlot>& samplers() const { return m_samplers; }

    std::size_t liveBufferCount() const { return liveCount(m_buffers); }
    std::size_t liveTextureCount() const { return liveCount(m_textures); }
    std::size_t liveProgramCount() const { return liveCount(m_programs); }
    std::size_t destroyedBufferCount() const { return m_destroyedBuffers; }
    std::size_t destroyedTextureCount() const { return m_destroyedTextures; }
    std::size_t destroyedProgramCount() const { return m_destroyedPrograms; }
    std::uint32_t lightingBinds() const { return m_lightingBinds; }

  private:

    void push(MockCommand::Type type) {
        MockCommand command;
        command.type = type;
        m_commands.push_back(std::move(command));
    }

    BufferSlot* bufferSlot(BufferHandle handle) {
        if (!handle.valid() || handle.index >= m_buffers.size()) {
            return nullptr;
        }
        return &m_buffers[handle.index];
    }

    TextureSlot* textureSlot(TextureHandle handle) {
        if (!handle.valid() || handle.index >= m_textures.size()) {
            return nullptr;
        }
        return &m_textures[handle.index];
    }

    ProgramSlot* programSlot(ProgramHandle handle) {
        if (!handle.valid() || handle.index >= m_programs.size()) {
            return nullptr;
        }
        return &m_programs[handle.index];
    }

    std::uint32_t uniformBytes(UniformHandle handle) const {
        if (!handle.valid() || handle.index >= m_uniforms.size()) {
            return 0;
        }
        switch (m_uniforms[handle.index].type) {
        case UniformType::Sampler:
            return 0;
        case UniformType::Vec4:
            return 16;
        case UniformType::Mat3:
            return 48;
        case UniformType::Mat4:
            return 64;
        }
        return 0;
    }

    template <typename T>
    static std::size_t liveCount(const std::vector<T>& slots) {
        std::size_t count = 0;
        for (const T& slot : slots) {
            if (slot.alive) {
                ++count;
            }
        }
        return count;
    }

    std::vector<MockCommand> m_commands;
    std::vector<BufferSlot> m_buffers;
    std::vector<TextureSlot> m_textures;
    std::vector<ProgramSlot> m_programs;
    std::vector<UniformSlot> m_uniforms;
    std::vector<SamplerSlot> m_samplers;
    std::unordered_map<std::string, UniformHandle> m_uniformsByName;
    std::unordered_map<std::string, SamplerHandle> m_samplersByName;
    std::size_t m_destroyedBuffers = 0;
    std::size_t m_destroyedTextures = 0;
    std::size_t m_destroyedPrograms = 0;
    std::uint32_t m_lightingBinds = 0;
    LitSettings m_lastLitSettings{};
};

} // namespace vfx::test
