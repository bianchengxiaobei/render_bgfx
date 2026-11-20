#pragma once

#include "vfx/device.h"
#include "vfx/graph.h"
#include "vfx/types.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace vfx {

enum class AssetBindingKind : std::uint8_t {
    Buffer = 0,
    Texture = 1,
};

struct AssetBinding {
    AssetBindingKind kind = AssetBindingKind::Buffer;
    std::uint8_t stage = 0;
    Access access = Access::Read;
    std::uint32_t index = 0;
};

struct AssetBuffer {
    std::uint8_t kind = 0;
    std::uint32_t size = 0;
    std::uint32_t stride = 0;
};

struct AssetProperty {
    std::string name;
    PropertyType type = PropertyType::Float;
    std::array<float, 4> defaultFloat{};
    std::vector<std::byte> lut;
};

struct AssetBurst {
    float time = 0.0f;
    std::uint32_t count = 0;
    float period = 0.0f;
    std::uint32_t repeat = 0;
};

struct AssetSystem {
    std::string name;
    std::uint32_t requestedCapacity = 0;
    std::uint32_t capacity = 0;
    SystemSpace space = SystemSpace::World;
    Bounds bounds{};
    float spawnerRate = 0.0f;
    std::vector<AssetBurst> bursts;
    std::uint32_t firstKernel = 0;
    std::uint32_t kernelCount = 0;
    std::vector<AssetBuffer> buffers;
};

struct AssetProgram {
    std::uint32_t program = 0;
    std::uint32_t system = 0;
    std::uint8_t task = 0;
    bool output = false;
    std::array<std::uint32_t, 3> groups{1, 1, 1};
    std::vector<AssetBinding> bindings;
    std::vector<AssetBinding> vertexBindings;
    std::vector<AssetBinding> fragmentBindings;
    std::array<std::vector<std::byte>, 2> computeBlobs;
    std::array<std::vector<std::byte>, 2> vertexBlobs;
    std::array<std::vector<std::byte>, 2> fragmentBlobs;
    BlendMode blend = BlendMode::Alpha;
    bool lit = false;
    SortMode sortMode = SortMode::None;
    SortOrder sortOrder = SortOrder::Ascending;
    std::uint32_t textureProperty = kInvalidIndex;
};

struct Asset {
    std::string name;
    std::vector<AssetProperty> properties;
    std::vector<AssetSystem> systems;
    std::vector<AssetProgram> programs;
};

std::shared_ptr<const Asset> loadAssetBinaryFile(const std::filesystem::path& path, std::string& error);
std::shared_ptr<const Asset> parseAssetBinary(std::span<const std::byte> bytes, std::string& error);

} // namespace vfx
