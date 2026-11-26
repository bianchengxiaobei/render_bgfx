#include "asset/animation.h"
#include "asset/gltf_loader.h"
#include "asset/material.h"
#include "asset/mesh.h"
#include "asset/scene_loader.h"
#include "asset/texture_loader.h"

#include <bimg/bimg.h>
#include <bimg/encode.h>
#include <bx/allocator.h>
#include <bx/readerwriter.h>
#include <doctest/doctest.h>

#if defined(HBG_HAS_DRACO) && HBG_HAS_DRACO
#include <draco/attributes/point_attribute.h>
#include <draco/compression/encode.h>
#include <draco/core/encoder_buffer.h>
#include <draco/mesh/mesh.h>
#endif

#include <algorithm>
#include <array>
#include <cfloat>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using namespace hbg;
using namespace hbg::asset;

namespace {

constexpr const char* kPngDataUri =
    "data:image/png;base64,"
    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII=";

constexpr uint8_t kDecalPng[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x04, 0x00, 0x00, 0x00, 0xb5, 0x1c, 0x0c,
    0x02, 0x00, 0x00, 0x00, 0x0b, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0x64, 0xf8, 0x0f, 0x00,
    0x01, 0x05, 0x01, 0x01, 0x27, 0x18, 0xe3, 0x66, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44,
    0xae, 0x42, 0x60, 0x82,
};

constexpr char kEtc1sOpaqueKtx2Hex[] =
    "ab4b5458203230bb0d0a1a0a000000000100000010000000100000000000000000000000010000000500000001000000"
    "c80000002c000000f40000002400000018010000000000002f010000000000004d020000000000000800000000000000"
    "00000000000000004a020000000000000300000000000000000000000000000049020000000000000100000000000000"
    "000000000000000048020000000000000100000000000000000000000000000047020000000000000100000000000000"
    "00000000000000002c0000000000000002002800a301020003030000000000000000000000003f000000000000000000"
    "ffffffff1f0000004b545877726974657200426173697320556e6976657273616c20312e3530000017000d0050000000"
    "320000003500000000000000000000000000000008000000000000000000000000000000000000000300000000000000"
    "000000000000000000000000010000000000000000000000000000000000000001000000000000000000000000000000"
    "0000000001000000000000000000000010401c0001000041d8be84868200510008001004622378acf3160d4c00710004"
    "00008060f351724000a6000000000082203ca51984103591204842e4462a44bd11713cb9e4dbbe8297a65a7bdade770b"
    "080626070880300c00c02b36bf427de83e011750c8166ed6290a04a4fe9f379cb41707341da1e77e22ffabadaa1a6b7c"
    "d30c00c144000000000000f25fed0298130000000004c0ac810727e06dc4000040c04a7cb988426984b3090360060000"
    "000000002940001ee6069a0814f20030c2a1355a01";

constexpr char kUastcAlphaKtx2Hex[] =
    "ab4b5458203230bb0d0a1a0a000000000100000010000000100000000000000000000000010000000500000002000000"
    "c80000002c000000f40000002400000000000000000000000000000000000000ac010000000000005200000000000000"
    "00010000000000006301000000000000490000000000000040000000000000004a010000000000001900000000000000"
    "100000000000000031010000000000001900000000000000100000000000000018010000000000001900000000000000"
    "10000000000000002c0000000000000002002800a601020003030000000000000000000000007f030000000000000000"
    "ffffffff1f0000004b545877726974657200426173697320556e6976657273616c20312e3530000028b52ffd20108100"
    "009757151870b4f402000000000000000028b52ffd201081000060430a828dfb65ea3233fcfffcfffcff28b52ffd2010"
    "8100008093ea03046a656202330033ccffccff28b52ffd204001020020e96a4846a8650a1332dcfedcfe1032109039c5"
    "a020667671cf70cf70cf70cf06b0656282218ac290c1098ff0088ff03091a1c0a0546576d13fd03fd03fd03f28b52ffd"
    "60000045020004031010193b44c06d8055d951101019bb45c06da057bc01c26d7c569c00c26d5c54d93844ccd9b845cc"
    "d9b901ced99900ce0900075002380238021c710812805dc02e33f7d43a2f";

uint8_t hexValue(char value) {
    if (value >= '0' && value <= '9') {
        return static_cast<uint8_t>(value - '0');
    }
    if (value >= 'a' && value <= 'f') {
        return static_cast<uint8_t>(value - 'a' + 10);
    }
    if (value >= 'A' && value <= 'F') {
        return static_cast<uint8_t>(value - 'A' + 10);
    }
    return 0xFFu;
}

std::vector<uint8_t> decodeHex(std::string_view text) {
    if ((text.size() & 1u) != 0u) {
        return {};
    }
    std::vector<uint8_t> data;
    data.reserve(text.size() / 2u);
    for (std::size_t index = 0; index < text.size(); index += 2u) {
        const uint8_t high = hexValue(text[index]);
        const uint8_t low = hexValue(text[index + 1u]);
        if (high > 0x0Fu || low > 0x0Fu) {
            return {};
        }
        data.push_back(static_cast<uint8_t>((high << 4u) | low));
    }
    return data;
}

void writeFile(const std::filesystem::path& path, const void* data, std::size_t size) {
    std::ofstream file(path, std::ios::binary);
    file.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
}

std::vector<Mat4> computeSkinMatrices(const render::RenderScene& scene, const render::RenderObject& object) {
    std::vector<Mat4> matrices;
    if (object.skinIndex < 0 || static_cast<std::size_t>(object.skinIndex) >= scene.skins.size()) {
        return matrices;
    }
    const Skin& skin = scene.skins[static_cast<std::size_t>(object.skinIndex)];
    const std::vector<Mat4> worlds = evaluateNodeWorlds(scene);
    const Mat4 meshWorld = object.nodeIndex >= 0 && static_cast<std::size_t>(object.nodeIndex) < worlds.size()
                               ? worlds[static_cast<std::size_t>(object.nodeIndex)]
                               : object.world;
    const Mat4 inverseMeshWorld = inverse(meshWorld);
    matrices.assign(skin.joints.size(), Mat4::identity());
    for (std::size_t index = 0; index < skin.joints.size(); ++index) {
        const uint32_t joint = skin.joints[index];
        if (joint >= worlds.size() || index >= skin.inverseBindMatrices.size()) {
            continue;
        }
        matrices[index] = inverseMeshWorld * worlds[joint] * skin.inverseBindMatrices[index];
    }
    return matrices;
}

std::filesystem::path makeFixtureDirectory() {
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "hbg_gltf_test";
    std::filesystem::create_directories(directory);
    return directory;
}

ImageAsset makeMaskImage(uint32_t width, uint32_t height, const std::vector<std::array<uint8_t, 4>>& pixels) {
    ImageAsset image;
    image.width = width;
    image.height = height;
    image.format = gfx::TextureFormat::RGBA8;
    image.pixels.reserve(pixels.size() * 4u);
    for (const auto& pixel : pixels) {
        image.pixels.insert(image.pixels.end(), pixel.begin(), pixel.end());
    }
    return image;
}

std::vector<uint8_t> makeKtx2Bc1() {
    bx::DefaultAllocator allocator;
    bx::Error error;
    bimg::ImageContainer* image =
        bimg::imageAlloc(&allocator, bimg::TextureFormat::BC1, 8, 8, 0, 1, false, true);
    if (image == nullptr) {
        return {};
    }

    std::array<uint8_t, 8 * 8 * 4> rgba{};
    for (uint32_t y = 0; y < 8; ++y) {
        for (uint32_t x = 0; x < 8; ++x) {
            const std::size_t offset = (static_cast<std::size_t>(y) * 8 + x) * 4;
            rgba[offset + 0] = static_cast<uint8_t>(x * 32);
            rgba[offset + 1] = static_cast<uint8_t>(y * 32);
            rgba[offset + 2] = 128;
            rgba[offset + 3] = 255;
        }
    }
    bimg::imageEncodeFromRgba8(&allocator, image->m_data, rgba.data(), 8, 8, 1, bimg::TextureFormat::BC1,
                               bimg::Quality::Fastest, &error);

    bimg::ImageMip mip;
    std::array<uint8_t, 4 * 4 * 4> mipRgba{};
    for (std::size_t index = 0; index < mipRgba.size(); index += 4) {
        mipRgba[index + 0] = 96;
        mipRgba[index + 1] = 160;
        mipRgba[index + 2] = 128;
        mipRgba[index + 3] = 255;
    }
    if (bimg::imageGetRawData(*image, 0, 1, image->m_data, image->m_size, mip)) {
        bimg::imageEncodeFromRgba8(&allocator, const_cast<uint8_t*>(mip.m_data), mipRgba.data(), 4, 4, 1,
                                   bimg::TextureFormat::BC1, bimg::Quality::Fastest, &error);
    }

    bx::MemoryBlock block(&allocator);
    bx::MemoryWriter writer(&block);
    bimg::imageWriteKtx2(&writer, *image, image->m_data, image->m_size, &error);
    const auto* data = static_cast<const uint8_t*>(block.more());
    std::vector<uint8_t> result;
    if (data != nullptr && block.getSize() > 0) {
        result.assign(data, data + block.getSize());
    }
    bimg::imageFree(image);
    return result;
}

std::vector<uint8_t> makeKtx2Rgba8Mips() {
    bx::DefaultAllocator allocator;
    bx::Error error;
    bimg::ImageContainer* image =
        bimg::imageAlloc(&allocator, bimg::TextureFormat::RGBA8, 8, 8, 0, 1, false, true);
    if (image == nullptr) {
        return {};
    }

    for (uint8_t mipLevel = 0; mipLevel < image->m_numMips; ++mipLevel) {
        bimg::ImageMip mip;
        if (!bimg::imageGetRawData(*image, 0, mipLevel, image->m_data, image->m_size, mip)) {
            bimg::imageFree(image);
            return {};
        }
        uint8_t* pixels = const_cast<uint8_t*>(mip.m_data);
        for (uint32_t y = 0; y < mip.m_height; ++y) {
            for (uint32_t x = 0; x < mip.m_width; ++x) {
                const std::size_t offset = (static_cast<std::size_t>(y) * mip.m_width + x) * 4u;
                pixels[offset + 0] = static_cast<uint8_t>(32u + mipLevel * 32u);
                pixels[offset + 1] = static_cast<uint8_t>(x * 16u);
                pixels[offset + 2] = static_cast<uint8_t>(y * 16u);
                pixels[offset + 3] = 255;
            }
        }
    }

    bx::MemoryBlock block(&allocator);
    bx::MemoryWriter writer(&block);
    bimg::imageWriteKtx2(&writer, *image, image->m_data, image->m_size, &error);
    const auto* data = static_cast<const uint8_t*>(block.more());
    std::vector<uint8_t> result;
    if (data != nullptr && block.getSize() > 0) {
        result.assign(data, data + block.getSize());
    }
    bimg::imageFree(image);
    return result;
}

std::vector<uint8_t> makeKtx1Rgba8Mips() {
    std::vector<uint8_t> bytes;
    const auto appendU32 = [&bytes](uint32_t value) {
        bytes.push_back(static_cast<uint8_t>(value & 0xFFu));
        bytes.push_back(static_cast<uint8_t>((value >> 8u) & 0xFFu));
        bytes.push_back(static_cast<uint8_t>((value >> 16u) & 0xFFu));
        bytes.push_back(static_cast<uint8_t>((value >> 24u) & 0xFFu));
    };

    static constexpr uint8_t kIdentifier[12] = {0xAB, 0x4B, 0x54, 0x58, 0x20, 0x31,
                                                0x31, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};
    bytes.insert(bytes.end(), kIdentifier, kIdentifier + sizeof(kIdentifier));
    appendU32(0x04030201u);
    appendU32(0x1401u);
    appendU32(1u);
    appendU32(0x1908u);
    appendU32(0x8058u);
    appendU32(0x1908u);
    appendU32(8u);
    appendU32(8u);
    appendU32(0u);
    appendU32(0u);
    appendU32(1u);
    appendU32(4u);
    appendU32(0u);

    uint32_t size = 8;
    for (uint32_t mipLevel = 0; mipLevel < 4u; ++mipLevel) {
        const uint32_t imageSize = size * size * 4u;
        appendU32(imageSize);
        for (uint32_t texel = 0; texel < size * size; ++texel) {
            bytes.push_back(static_cast<uint8_t>(32u + mipLevel * 32u));
            bytes.push_back(static_cast<uint8_t>(texel & 0xFFu));
            bytes.push_back(static_cast<uint8_t>((texel >> 8u) & 0xFFu));
            bytes.push_back(255u);
        }
        size = std::max(1u, size / 2u);
    }
    return bytes;
}

std::vector<uint8_t> makeTriangleBuffer() {
    std::vector<float> values = {
        0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f,
        0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
    };
    std::vector<uint8_t> bytes;
    bytes.resize(values.size() * sizeof(float));
    std::memcpy(bytes.data(), values.data(), bytes.size());
    const std::array<uint16_t, 3> indices = {0, 1, 2};
    const std::size_t indexOffset = bytes.size();
    bytes.resize(bytes.size() + sizeof(indices));
    std::memcpy(bytes.data() + indexOffset, indices.data(), sizeof(indices));
    return bytes;
}

std::vector<uint8_t> makeAnimatedBuffer() {
    std::vector<uint8_t> bytes = makeTriangleBuffer();
    bytes.resize(104, 0);
    const auto appendFloats = [&](std::initializer_list<float> values) {
        const std::size_t offset = bytes.size();
        bytes.resize(offset + values.size() * sizeof(float));
        std::memcpy(bytes.data() + offset, values.begin(), values.size() * sizeof(float));
    };
    appendFloats({0.0f, 1.0f});
    appendFloats({0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f});
    appendFloats({0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.70710678f, 0.70710678f});
    return bytes;
}

std::string makeAnimatedGltf() {
    return R"({
        "asset":{"version":"2.0"},
        "buffers":[{"uri":"animated.bin","byteLength":168}],
        "bufferViews":[
            {"buffer":0,"byteOffset":0,"byteLength":36},
            {"buffer":0,"byteOffset":36,"byteLength":36},
            {"buffer":0,"byteOffset":72,"byteLength":24},
            {"buffer":0,"byteOffset":96,"byteLength":6},
            {"buffer":0,"byteOffset":104,"byteLength":8},
            {"buffer":0,"byteOffset":112,"byteLength":24},
            {"buffer":0,"byteOffset":136,"byteLength":32}
        ],
        "accessors":[
            {"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},
            {"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},
            {"bufferView":2,"componentType":5126,"count":3,"type":"VEC2"},
            {"bufferView":3,"componentType":5123,"count":3,"type":"SCALAR"},
            {"bufferView":4,"componentType":5126,"count":2,"type":"SCALAR","min":[0.0],"max":[1.0]},
            {"bufferView":5,"componentType":5126,"count":2,"type":"VEC3"},
            {"bufferView":6,"componentType":5126,"count":2,"type":"VEC4"}
        ],
        "meshes":[{"name":"Animated","primitives":[
            {"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2},"indices":3}
        ]}],
        "nodes":[
            {"name":"MeshNode","mesh":0},
            {"name":"Root","children":[0]}
        ],
        "animations":[{
            "name":"Move",
            "samplers":[
                {"input":4,"output":5,"interpolation":"LINEAR"},
                {"input":4,"output":6,"interpolation":"LINEAR"}
            ],
            "channels":[
                {"sampler":0,"target":{"node":1,"path":"translation"}},
                {"sampler":1,"target":{"node":1,"path":"rotation"}}
            ]
        }],
        "scenes":[{"nodes":[1]}],
        "scene":0
    })";
}

std::vector<uint8_t> makeCubicBuffer() {
    std::vector<uint8_t> bytes = makeTriangleBuffer();
    bytes.resize(104, 0);
    const auto appendFloats = [&](std::initializer_list<float> values) {
        const std::size_t offset = bytes.size();
        bytes.resize(offset + values.size() * sizeof(float));
        std::memcpy(bytes.data() + offset, values.begin(), values.size() * sizeof(float));
    };
    appendFloats({0.0f, 1.0f});
    appendFloats({
        0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f,
        2.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f,
    });
    return bytes;
}

std::string makeCubicGltf() {
    return R"({
        "asset":{"version":"2.0"},
        "buffers":[{"uri":"cubic.bin","byteLength":184}],
        "bufferViews":[
            {"buffer":0,"byteOffset":0,"byteLength":36},
            {"buffer":0,"byteOffset":36,"byteLength":36},
            {"buffer":0,"byteOffset":72,"byteLength":24},
            {"buffer":0,"byteOffset":96,"byteLength":6},
            {"buffer":0,"byteOffset":104,"byteLength":8},
            {"buffer":0,"byteOffset":112,"byteLength":72}
        ],
        "accessors":[
            {"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},
            {"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},
            {"bufferView":2,"componentType":5126,"count":3,"type":"VEC2"},
            {"bufferView":3,"componentType":5123,"count":3,"type":"SCALAR"},
            {"bufferView":4,"componentType":5126,"count":2,"type":"SCALAR","min":[0.0],"max":[1.0]},
            {"bufferView":5,"componentType":5126,"count":6,"type":"VEC3"}
        ],
        "meshes":[{"name":"Cubic","primitives":[
            {"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2},"indices":3}
        ]}],
        "nodes":[
            {"name":"MeshNode","mesh":0},
            {"name":"Root","children":[0]}
        ],
        "animations":[{
            "name":"CubicMove",
            "samplers":[
                {"input":4,"output":5,"interpolation":"CUBICSPLINE"}
            ],
            "channels":[
                {"sampler":0,"target":{"node":1,"path":"translation"}}
            ]
        }],
        "scenes":[{"nodes":[1]}],
        "scene":0
    })";
}

std::vector<uint8_t> makeMorphBuffer() {
    std::vector<uint8_t> bytes = makeTriangleBuffer();
    bytes.resize(104, 0);
    const auto appendFloats = [&](std::initializer_list<float> values) {
        const std::size_t offset = bytes.size();
        bytes.resize(offset + values.size() * sizeof(float));
        std::memcpy(bytes.data() + offset, values.begin(), values.size() * sizeof(float));
    };
    appendFloats({0.0f, 1.0f});
    appendFloats({0.0f, 1.0f});
    appendFloats({2.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f});
    appendFloats({1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f});
    return bytes;
}

std::string makeMorphGltf() {
    return R"({
        "asset":{"version":"2.0"},
        "buffers":[{"uri":"morph.bin","byteLength":192}],
        "bufferViews":[
            {"buffer":0,"byteOffset":0,"byteLength":36},
            {"buffer":0,"byteOffset":36,"byteLength":36},
            {"buffer":0,"byteOffset":72,"byteLength":24},
            {"buffer":0,"byteOffset":96,"byteLength":6},
            {"buffer":0,"byteOffset":104,"byteLength":8},
            {"buffer":0,"byteOffset":112,"byteLength":8},
            {"buffer":0,"byteOffset":120,"byteLength":36},
            {"buffer":0,"byteOffset":156,"byteLength":36}
        ],
        "accessors":[
            {"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0.0,0.0,0.0],"max":[1.0,1.0,0.0]},
            {"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},
            {"bufferView":2,"componentType":5126,"count":3,"type":"VEC2"},
            {"bufferView":3,"componentType":5123,"count":3,"type":"SCALAR"},
            {"bufferView":4,"componentType":5126,"count":2,"type":"SCALAR","min":[0.0],"max":[1.0]},
            {"bufferView":5,"componentType":5126,"count":2,"type":"SCALAR","min":[0.0],"max":[1.0]},
            {"bufferView":6,"componentType":5126,"count":3,"type":"VEC3","min":[2.0,0.0,0.0],"max":[2.0,0.0,0.0]},
            {"bufferView":7,"componentType":5126,"count":3,"type":"VEC3"}
        ],
        "meshes":[{
            "name":"Morph",
            "weights":[0.0],
            "extras":{"targetNames":["Wide"]},
            "primitives":[
                {"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2},"indices":3,
                 "targets":[{"POSITION":6,"NORMAL":7}]}
            ]
        }],
        "nodes":[
            {"name":"MeshNode","mesh":0,"weights":[0.25]},
            {"name":"Root","children":[0]}
        ],
        "animations":[{
            "name":"MorphWeights",
            "samplers":[{"input":4,"output":5,"interpolation":"LINEAR"}],
            "channels":[{"sampler":0,"target":{"node":0,"path":"weights"}}]
        }],
        "scenes":[{"nodes":[1]}],
        "scene":0
    })";
}

std::vector<uint8_t> makeMorphCubicBuffer() {
    std::vector<uint8_t> bytes = makeTriangleBuffer();
    bytes.resize(104, 0);
    const auto appendFloats = [&](std::initializer_list<float> values) {
        const std::size_t offset = bytes.size();
        bytes.resize(offset + values.size() * sizeof(float));
        std::memcpy(bytes.data() + offset, values.begin(), values.size() * sizeof(float));
    };
    appendFloats({0.0f, 1.0f});
    appendFloats({0.0f, 0.0f, 2.0f, 0.0f, 0.0f, 0.0f});
    appendFloats({2.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f});
    appendFloats({0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f});
    return bytes;
}

std::string makeMorphCubicGltf() {
    return R"({
        "asset":{"version":"2.0"},
        "buffers":[{"uri":"morph_cubic.bin","byteLength":208}],
        "bufferViews":[
            {"buffer":0,"byteOffset":0,"byteLength":36},
            {"buffer":0,"byteOffset":36,"byteLength":36},
            {"buffer":0,"byteOffset":72,"byteLength":24},
            {"buffer":0,"byteOffset":96,"byteLength":6},
            {"buffer":0,"byteOffset":104,"byteLength":8},
            {"buffer":0,"byteOffset":112,"byteLength":24},
            {"buffer":0,"byteOffset":136,"byteLength":36},
            {"buffer":0,"byteOffset":172,"byteLength":36}
        ],
        "accessors":[
            {"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0.0,0.0,0.0],"max":[1.0,1.0,0.0]},
            {"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},
            {"bufferView":2,"componentType":5126,"count":3,"type":"VEC2"},
            {"bufferView":3,"componentType":5123,"count":3,"type":"SCALAR"},
            {"bufferView":4,"componentType":5126,"count":2,"type":"SCALAR","min":[0.0],"max":[1.0]},
            {"bufferView":5,"componentType":5126,"count":6,"type":"SCALAR"},
            {"bufferView":6,"componentType":5126,"count":3,"type":"VEC3","min":[2.0,0.0,0.0],"max":[2.0,0.0,0.0]},
            {"bufferView":7,"componentType":5126,"count":3,"type":"VEC3"}
        ],
        "meshes":[{
            "name":"MorphCubic",
            "weights":[0.0],
            "primitives":[
                {"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2},"indices":3,
                 "targets":[{"POSITION":6,"NORMAL":7}]}
            ]
        }],
        "nodes":[
            {"name":"MeshNode","mesh":0},
            {"name":"Root","children":[0]}
        ],
        "animations":[{
            "name":"MorphCubicWeights",
            "samplers":[{"input":4,"output":5,"interpolation":"CUBICSPLINE"}],
            "channels":[{"sampler":0,"target":{"node":0,"path":"weights"}}]
        }],
        "scenes":[{"nodes":[1]}],
        "scene":0
    })";
}

std::vector<uint8_t> makeSkinBuffer() {
    std::vector<uint8_t> bytes;
    const auto appendFloats = [&](std::initializer_list<float> values) {
        const std::size_t offset = bytes.size();
        bytes.resize(offset + values.size() * sizeof(float));
        std::memcpy(bytes.data() + offset, values.begin(), values.size() * sizeof(float));
    };
    const auto appendU16 = [&](std::initializer_list<uint16_t> values) {
        const std::size_t offset = bytes.size();
        bytes.resize(offset + values.size() * sizeof(uint16_t));
        std::memcpy(bytes.data() + offset, values.begin(), values.size() * sizeof(uint16_t));
    };
    appendFloats({0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 2.0f, 0.0f, 0.0f});
    appendFloats({0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f});
    appendFloats({0.0f, 0.0f, 0.5f, 0.5f, 1.0f, 0.0f});
    appendU16({0, 1, 2});
    bytes.resize(104, 0);
    appendU16({0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0});
    appendFloats({0.25f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f});
    appendFloats({
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    });
    appendFloats({
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        -1.0f, 0.0f, 0.0f, 1.0f,
    });
    appendFloats({0.0f, 1.0f});
    appendFloats({0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.70710678f, 0.70710678f});
    appendU16({1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
    appendFloats({0.25f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f});
    appendU16({0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
    appendFloats({0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f});
    return bytes;
}

std::string makeSkinGltf() {
    return R"({
        "asset":{"version":"2.0"},
        "buffers":[{"uri":"skin.bin","byteLength":488}],
        "bufferViews":[
            {"buffer":0,"byteOffset":0,"byteLength":36},
            {"buffer":0,"byteOffset":36,"byteLength":36},
            {"buffer":0,"byteOffset":72,"byteLength":24},
            {"buffer":0,"byteOffset":96,"byteLength":6},
            {"buffer":0,"byteOffset":104,"byteLength":24},
            {"buffer":0,"byteOffset":128,"byteLength":48},
            {"buffer":0,"byteOffset":176,"byteLength":128},
            {"buffer":0,"byteOffset":304,"byteLength":8},
            {"buffer":0,"byteOffset":312,"byteLength":32},
            {"buffer":0,"byteOffset":344,"byteLength":24},
            {"buffer":0,"byteOffset":368,"byteLength":48},
            {"buffer":0,"byteOffset":416,"byteLength":24},
            {"buffer":0,"byteOffset":440,"byteLength":48}
        ],
        "accessors":[
            {"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0.0,0.0,0.0],"max":[2.0,1.0,0.0]},
            {"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},
            {"bufferView":2,"componentType":5126,"count":3,"type":"VEC2"},
            {"bufferView":3,"componentType":5123,"count":3,"type":"SCALAR"},
            {"bufferView":4,"componentType":5123,"count":3,"type":"VEC4"},
            {"bufferView":5,"componentType":5126,"count":3,"type":"VEC4"},
            {"bufferView":6,"componentType":5126,"count":2,"type":"MAT4"},
            {"bufferView":7,"componentType":5126,"count":2,"type":"SCALAR","min":[0.0],"max":[1.0]},
            {"bufferView":8,"componentType":5126,"count":2,"type":"VEC4"},
            {"bufferView":9,"componentType":5123,"count":3,"type":"VEC4"},
            {"bufferView":10,"componentType":5126,"count":3,"type":"VEC4"},
            {"bufferView":11,"componentType":5123,"count":3,"type":"VEC4"},
            {"bufferView":12,"componentType":5126,"count":3,"type":"VEC4"}
        ],
        "materials":[{"name":"SkinMat","pbrMetallicRoughness":{"baseColorFactor":[0.8,0.2,0.1,1.0]}}],
        "meshes":[{
            "name":"SkinTriangle",
            "primitives":[{
                "attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2,"JOINTS_0":4,"WEIGHTS_0":5,"JOINTS_1":9,"WEIGHTS_1":10,"JOINTS_2":11,"WEIGHTS_2":12},
                "indices":3,"material":0
            }]
        }],
        "skins":[{"name":"Skin","inverseBindMatrices":6,"joints":[1,2],"skeleton":0}],
        "nodes":[
            {"name":"Root","children":[1,3]},
            {"name":"Joint0","children":[2]},
            {"name":"Joint1","translation":[1.0,0.0,0.0]},
            {"name":"MeshNode","mesh":0,"skin":0,"translation":[10.0,0.0,0.0]}
        ],
        "animations":[{
            "name":"Bend",
            "samplers":[{"input":7,"output":8,"interpolation":"LINEAR"}],
            "channels":[{"sampler":0,"target":{"node":2,"path":"rotation"}}]
        }],
        "scenes":[{"nodes":[0]}],
        "scene":0
    })";
}

#if defined(HBG_HAS_DRACO) && HBG_HAS_DRACO
bool encodeDracoSkin(std::vector<uint8_t>& compressed) {
    draco::Mesh mesh;
    mesh.set_num_points(3);
    mesh.SetNumFaces(1);
    draco::Mesh::Face face = {
        {draco::PointIndex(0), draco::PointIndex(1), draco::PointIndex(2)},
    };
    mesh.SetFace(draco::FaceIndex(0), face);

    const float positions[] = {-1.0f, -1.0f, 0.0f, 1.0f, -1.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    auto position = std::make_unique<draco::PointAttribute>();
    position->Init(draco::GeometryAttribute::POSITION, 3, draco::DT_FLOAT32, false, 3);
    for (uint32_t vertex = 0; vertex < 3; ++vertex) {
        position->SetAttributeValue(draco::AttributeValueIndex(vertex), &positions[vertex * 3]);
    }
    mesh.AddAttribute(std::move(position));

    const float normals[] = {0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f};
    auto normal = std::make_unique<draco::PointAttribute>();
    normal->Init(draco::GeometryAttribute::NORMAL, 3, draco::DT_FLOAT32, false, 3);
    for (uint32_t vertex = 0; vertex < 3; ++vertex) {
        normal->SetAttributeValue(draco::AttributeValueIndex(vertex), &normals[vertex * 3]);
    }
    mesh.AddAttribute(std::move(normal));

    const uint16_t joints[] = {0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
    auto jointsAttribute = std::make_unique<draco::PointAttribute>();
    jointsAttribute->Init(draco::GeometryAttribute::GENERIC, 4, draco::DT_UINT16, false, 3);
    for (uint32_t vertex = 0; vertex < 3; ++vertex) {
        jointsAttribute->SetAttributeValue(draco::AttributeValueIndex(vertex), &joints[vertex * 4]);
    }
    mesh.AddAttribute(std::move(jointsAttribute));

    const float weights[] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f};
    auto weightsAttribute = std::make_unique<draco::PointAttribute>();
    weightsAttribute->Init(draco::GeometryAttribute::GENERIC, 4, draco::DT_FLOAT32, false, 3);
    for (uint32_t vertex = 0; vertex < 3; ++vertex) {
        weightsAttribute->SetAttributeValue(draco::AttributeValueIndex(vertex), &weights[vertex * 4]);
    }
    mesh.AddAttribute(std::move(weightsAttribute));

    draco::Encoder encoder;
    encoder.SetSpeedOptions(5, 5);
    encoder.SetAttributeQuantization(draco::GeometryAttribute::POSITION, 14);
    draco::EncoderBuffer buffer;
    const draco::Status status = encoder.EncodeMeshToBuffer(mesh, &buffer);
    if (!status.ok()) {
        return false;
    }
    compressed.assign(buffer.data(), buffer.data() + buffer.size());
    return !compressed.empty();
}

bool encodeDracoTriangle(std::vector<uint8_t>& compressed) {
    draco::Mesh mesh;
    mesh.set_num_points(3);
    mesh.SetNumFaces(1);
    draco::Mesh::Face face = {
        {draco::PointIndex(0), draco::PointIndex(1), draco::PointIndex(2)},
    };
    mesh.SetFace(draco::FaceIndex(0), face);

    const float positions[] = {
        -1.0f, -1.0f, 0.0f,
        1.0f, -1.0f, 0.0f,
        0.0f, 1.0f, 0.0f,
    };
    auto position = std::make_unique<draco::PointAttribute>();
    position->Init(draco::GeometryAttribute::POSITION, 3, draco::DT_FLOAT32, false, 3);
    for (uint32_t vertex = 0; vertex < 3; ++vertex) {
        position->SetAttributeValue(draco::AttributeValueIndex(vertex), &positions[vertex * 3]);
    }
    mesh.AddAttribute(std::move(position));

    auto normal = std::make_unique<draco::PointAttribute>();
    normal->Init(draco::GeometryAttribute::NORMAL, 3, draco::DT_FLOAT32, false, 3);
    const float normalValue[] = {0.0f, 0.0f, 1.0f};
    for (uint32_t vertex = 0; vertex < 3; ++vertex) {
        normal->SetAttributeValue(draco::AttributeValueIndex(vertex), normalValue);
    }
    mesh.AddAttribute(std::move(normal));

    auto texcoord = std::make_unique<draco::PointAttribute>();
    texcoord->Init(draco::GeometryAttribute::TEX_COORD, 2, draco::DT_FLOAT32, false, 3);
    const float texcoords[] = {
        0.0f, 0.0f,
        1.0f, 0.0f,
        0.5f, 1.0f,
    };
    for (uint32_t vertex = 0; vertex < 3; ++vertex) {
        texcoord->SetAttributeValue(draco::AttributeValueIndex(vertex), &texcoords[vertex * 2]);
    }
    mesh.AddAttribute(std::move(texcoord));

    draco::Encoder encoder;
    encoder.SetSpeedOptions(5, 5);
    encoder.SetAttributeQuantization(draco::GeometryAttribute::POSITION, 14);
    draco::EncoderBuffer buffer;
    const draco::Status status = encoder.EncodeMeshToBuffer(mesh, &buffer);
    if (!status.ok()) {
        return false;
    }
    compressed.assign(buffer.data(), buffer.data() + buffer.size());
    return !compressed.empty();
}

std::vector<uint8_t> makeGlb(const std::string& json, const std::vector<uint8_t>& binary) {
    std::string jsonPadded = json;
    while (jsonPadded.size() % 4u != 0u) {
        jsonPadded.push_back(' ');
    }
    std::vector<uint8_t> binaryPadded = binary;
    while (binaryPadded.size() % 4u != 0u) {
        binaryPadded.push_back(0);
    }

    std::vector<uint8_t> glb;
    const auto appendUint32 = [&](uint32_t value) {
        glb.push_back(static_cast<uint8_t>(value & 0xFFu));
        glb.push_back(static_cast<uint8_t>((value >> 8u) & 0xFFu));
        glb.push_back(static_cast<uint8_t>((value >> 16u) & 0xFFu));
        glb.push_back(static_cast<uint8_t>((value >> 24u) & 0xFFu));
    };
    glb.insert(glb.end(), {'g', 'l', 'T', 'F'});
    appendUint32(2u);
    appendUint32(static_cast<uint32_t>(12u + 8u + jsonPadded.size() + 8u + binaryPadded.size()));
    appendUint32(static_cast<uint32_t>(jsonPadded.size()));
    appendUint32(0x4E4F534Au);
    glb.insert(glb.end(), jsonPadded.begin(), jsonPadded.end());
    appendUint32(static_cast<uint32_t>(binaryPadded.size()));
    appendUint32(0x004E4942u);
    glb.insert(glb.end(), binaryPadded.begin(), binaryPadded.end());
    return glb;
}

std::string makeDracoSkinGltf(std::size_t compressedSize) {
    return R"({
        "asset":{"version":"2.0"},
        "extensionsUsed":["KHR_draco_mesh_compression"],
        "extensionsRequired":["KHR_draco_mesh_compression"],
        "buffers":[{"uri":"draco_skin.bin","byteLength":)" +
           std::to_string(compressedSize) + R"(}],
        "bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":)" +
           std::to_string(compressedSize) + R"(}],
        "accessors":[
            {"componentType":5126,"count":3,"type":"VEC3","min":[-1.0,-1.0,0.0],"max":[1.0,1.0,0.0]},
            {"componentType":5126,"count":3,"type":"VEC3"},
            {"componentType":5123,"count":3,"type":"VEC4"},
            {"componentType":5126,"count":3,"type":"VEC4"},
            {"componentType":5123,"count":3,"type":"SCALAR"}
        ],
        "meshes":[{
            "name":"DracoSkinTriangle",
            "primitives":[{
                "attributes":{"POSITION":0,"NORMAL":1,"JOINTS_0":2,"WEIGHTS_0":3},
                "indices":4,
                "material":0,
                "extensions":{"KHR_draco_mesh_compression":{
                    "bufferView":0,
                    "attributes":{"POSITION":0,"NORMAL":1,"JOINTS_0":2,"WEIGHTS_0":3}
                }}
            }]
        }],
        "materials":[{"name":"DracoMat","pbrMetallicRoughness":{"baseColorFactor":[0.2,0.4,0.8,1.0]}}],
        "skins":[{"name":"Skin","joints":[1,2]}],
        "nodes":[
            {"name":"Root","children":[1,2,3]},
            {"name":"Joint0"},
            {"name":"Joint1"},
            {"name":"MeshNode","mesh":0,"skin":0}
        ],
        "scenes":[{"nodes":[0]}],
        "scene":0
    })";
}

std::string makeDracoGltf(std::size_t compressedSize) {
    return R"({
        "asset":{"version":"2.0"},
        "extensionsUsed":["KHR_draco_mesh_compression"],
        "extensionsRequired":["KHR_draco_mesh_compression"],
        "buffers":[{"uri":"draco.bin","byteLength":)" +
           std::to_string(compressedSize) + R"(}],
        "bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":)" +
           std::to_string(compressedSize) + R"(}],
        "accessors":[
            {"componentType":5126,"count":3,"type":"VEC3"},
            {"componentType":5126,"count":3,"type":"VEC2"},
            {"componentType":5126,"count":3,"type":"VEC3","min":[-1.0,-1.0,0.0],"max":[1.0,1.0,0.0]},
            {"componentType":5123,"count":3,"type":"SCALAR"}
        ],
        "meshes":[{
            "name":"DracoTriangle",
            "primitives":[{
                "attributes":{"POSITION":2,"NORMAL":0,"TEXCOORD_0":1},
                "indices":3,
                "material":0,
                "extensions":{"KHR_draco_mesh_compression":{
                    "bufferView":0,
                    "attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2}
                }}
            }]
        }],
        "materials":[{"name":"DracoMat","pbrMetallicRoughness":{"baseColorFactor":[0.2,0.4,0.8,1.0]}}],
        "nodes":[{"name":"DracoNode","mesh":0}],
        "scenes":[{"nodes":[0]}],
        "scene":0
    })";
}
#endif

std::string makeGltf(const std::string& imageUri, bool withUv, bool withNormals = true,
                     const std::string& samplerJson = {}, const std::string& basisuUri = {}) {
    std::string attributes = R"("POSITION":0)";
    if (withNormals) {
        attributes += R"(,"NORMAL":1)";
    }
    if (withUv) {
        attributes += R"(,"TEXCOORD_0":2)";
    }
    const bool hasBasisu = withUv && !basisuUri.empty();
    const std::string texture = withUv ? R"(,"baseColorTexture":{"index":0})" : "";
    std::string textureSource = R"({"source":0)";
    if (!samplerJson.empty()) {
        textureSource += R"(,"sampler":0)";
    }
    if (hasBasisu) {
        textureSource += R"(,"extensions":{"KHR_texture_basisu":{"source":1}})";
    }
    textureSource += "}";
    const std::string samplers = samplerJson.empty() ? "" : R"(,"samplers":[)" + samplerJson + R"(])";
    std::string images =
        withUv ? R"(,"images":[{"uri":")" + imageUri + R"("}],"textures":[)" + textureSource + R"(])" : "";
    if (hasBasisu) {
        images = R"(,"images":[{"uri":")" + imageUri + R"("},{"uri":")" + basisuUri +
                 R"("}],"textures":[)" + textureSource + R"(])";
    }
    std::string extensionsUsed = R"("KHR_lights_punctual")";
    if (hasBasisu) {
        extensionsUsed += R"(,"KHR_texture_basisu")";
    }
    const std::string material = R"({
        "name":"MatA",
        "alphaMode":"MASK",
        "alphaCutoff":0.25,
        "doubleSided":true,
        "pbrMetallicRoughness":{
            "baseColorFactor":[0.8,0.2,0.1,1.0],
            "metallicFactor":0.1,
            "roughnessFactor":0.7)" +
                                 texture + R"(
        }
    })";
    const std::string meshPrimitives = R"([
        {"attributes":{)" + attributes +
                                       R"(},"indices":3,"material":0},
        {"attributes":{)" + attributes +
                                       R"(},"indices":3,"material":1}
    ])";
    return R"({
        "asset":{"version":"2.0"},
        "extensionsUsed":[)" +
           extensionsUsed + R"(],
        "buffers":[{"uri":"triangle.bin","byteLength":102}],
        "bufferViews":[
            {"buffer":0,"byteOffset":0,"byteLength":36},
            {"buffer":0,"byteOffset":36,"byteLength":36},
            {"buffer":0,"byteOffset":72,"byteLength":24},
            {"buffer":0,"byteOffset":96,"byteLength":6}
        ],
        "accessors":[
            {"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},
            {"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},
            {"bufferView":2,"componentType":5126,"count":3,"type":"VEC2"},
            {"bufferView":3,"componentType":5123,"count":3,"type":"SCALAR"}
        ],
        "materials":[)" +
           material + R"(,)" + material + R"(],
        "meshes":[{"name":"Triangle","primitives":)" +
           meshPrimitives + R"(}],
        "cameras":[{"type":"perspective","perspective":{"yfov":0.7,"znear":0.1,"zfar":100.0}}],
        "extensions":{"KHR_lights_punctual":{"lights":[{"type":"point","color":[1.0,0.5,0.25],"intensity":5.0,"range":10.0}]}},
        "nodes":[
            {"name":"MeshNode","mesh":0,"translation":[1.0,2.0,3.0]},
            {"name":"CameraNode","camera":0,"translation":[0.0,1.0,5.0]},
            {"name":"LightNode","translation":[2.0,3.0,4.0],"extensions":{"KHR_lights_punctual":{"light":0}}},
            {"name":"Root","translation":[10.0,0.0,0.0],"children":[0]}
        ],
        "scenes":[{"nodes":[3,1,2]}],
        "scene":0
        )" +
           images + samplers + R"(
    })";
}

} // namespace

TEST_CASE("glTF loader reads mesh material camera and light") {
    const std::filesystem::path directory = makeFixtureDirectory();
    const std::filesystem::path bufferPath = directory / "triangle.bin";
    const std::filesystem::path gltfPath = directory / "triangle.gltf";
    const std::filesystem::path sidecarPath = directory / "triangle.hdrp.json";

    const std::vector<uint8_t> buffer = makeTriangleBuffer();
    writeFile(bufferPath, buffer.data(), buffer.size());
    const std::string json = makeGltf(kPngDataUri, true);
    writeFile(gltfPath, json.data(), json.size());

    const std::string sidecar = R"({
        "hdrp":{"renderQueue":2451,"maskMap":"separate","transparentBackface":true,"features":{"clearCoat":true,"subsurfaceScattering":true},"clearCoat":0.4,"clearCoatRoughness":0.12,"transmission":0.3,"thickness":0.5,"ior":1.4,"iridescence":0.2,"anisotropy":0.6,"anisotropyRotation":0.25,"subsurfaceMask":0.7},
        "lights":[{
            "name":"LightNode",
            "instanceId":305419896,
            "sourceRadius":0.3,
            "sourceLength":0.4,
            "lightSourceAngle":12.0,
            "shadowType":"hard",
            "barnDoorAngle":40.0,
            "barnDoorLength":2.5,
            "falloff":"bakery-physical",
            "useColorTemperature":true,
            "colorTemperature":2000.0,
            "bakeMode":"mixed",
            "mixedMode":"indirectOnly",
            "lightingChannel":2,
            "shadowTranslucency":0.35,
            "enablePenumbra":true,
            "penumbraTint":[0.1,0.2,0.3,0.4],
            "penumbraLuma":0.75,
            "shadow":{
                "dimmer":0.4,
                "fadeDistance":42.0,
                "nearPlane":0.25,
                "resolution":1024,
                "tint":[0.1,0.2,0.3],
                "penumbraTint":true,
                "updateMode":"onEnable",
                "contactShadows":false,
                "softness":0.8,
                "blockerSamples":12,
                "filterSamples":20,
                "evsmExponent":28.0,
                "evsmLightLeakBias":0.2,
                "evsmVarianceBias":0.0005,
                "evsmBlurPasses":3
            }
        }]
    })";
    writeFile(sidecarPath, sidecar.data(), sidecar.size());

    const SceneLoadResult result = SceneLoader::load(gltfPath);
    REQUIRE_MESSAGE(result.ok(), result.error);
    const render::RenderScene& scene = *result.scene;

    REQUIRE(scene.meshes.size() == 1);
    const Mesh& mesh = scene.meshes[0];
    CHECK(mesh.vertices.size() == 6);
    CHECK(mesh.indices.size() == 6);
    CHECK(mesh.submeshes.size() == 2);
    CHECK(mesh.submeshes[0].indexCount == 3);
    CHECK(mesh.submeshes[1].indexCount == 3);
    CHECK(mesh.hasNormals);
    CHECK(mesh.hasTangents);
    CHECK(mesh.hasUv0);
    CHECK(mesh.submeshes[0].materialIndex == 0);
    CHECK(mesh.submeshes[1].materialIndex == 0);

    bool nonZeroTangent = false;
    for (const MeshVertex& vertex : mesh.vertices) {
        CHECK(vertex.previousPosition == vertex.position);
        if (vertex.tangent.x != 0.0f || vertex.tangent.y != 0.0f || vertex.tangent.z != 0.0f) {
            nonZeroTangent = true;
        }
    }
    CHECK(nonZeroTangent);

    REQUIRE(scene.materials.size() == 1);
    const Material& material = scene.materials[0];
    CHECK(material.name == "MatA");
    CHECK_FALSE(material.maskMapPacked);
    CHECK(material.alphaMode == AlphaMode::Mask);
    CHECK(material.renderQueue == 2451);
    CHECK((material.features & MaterialFeatureClearCoat) != 0);
    CHECK((material.features & MaterialFeatureSubsurface) != 0);
    CHECK(material.clearCoat == doctest::Approx(0.4f));
    CHECK(material.clearCoatRoughness == doctest::Approx(0.12f));
    CHECK(material.transmission == doctest::Approx(0.3f));
    CHECK(material.thickness == doctest::Approx(0.5f));
    CHECK(material.ior == doctest::Approx(1.4f));
    CHECK(material.iridescence == doctest::Approx(0.2f));
    CHECK(material.anisotropy == doctest::Approx(0.6f));
    CHECK(material.anisotropyRotation == doctest::Approx(0.25f));
    CHECK(material.subsurfaceMask == doctest::Approx(0.7f));
    CHECK(material.baseColorFactor == Vec4(0.8f, 0.2f, 0.1f, 1.0f));
    CHECK(material.metallic == doctest::Approx(0.1f));
    CHECK(material.roughness == doctest::Approx(0.7f));
    CHECK(material.doubleSided);
    CHECK(material.transparentBackface);
    CHECK(material.baseColorTexture.sampler.wrapS == TextureWrap::Repeat);
    CHECK(material.baseColorTexture.sampler.wrapT == TextureWrap::Repeat);
    CHECK(material.baseColorTexture.sampler.minFilter == TextureFilter::Linear);
    CHECK(material.baseColorTexture.sampler.magFilter == TextureFilter::Linear);

    REQUIRE(scene.images.size() == 1);
    CHECK(scene.images[0].width == 1);
    CHECK(scene.images[0].height == 1);
    CHECK(scene.images[0].format == gfx::TextureFormat::RGBA8);

    REQUIRE(scene.objects.size() == 1);
    CHECK(scene.objects[0].world[12] == doctest::Approx(11.0f));
    CHECK(scene.objects[0].world[13] == doctest::Approx(2.0f));
    CHECK(scene.objects[0].world[14] == doctest::Approx(3.0f));
    CHECK(scene.objects[0].worldBounds.min.x == doctest::Approx(11.0f));
    CHECK(scene.objects[0].worldBounds.max.x == doctest::Approx(12.0f));

    REQUIRE(scene.cameras.size() == 1);
    CHECK(scene.cameras[0].projection == render::Camera::Projection::Perspective);
    CHECK(scene.cameras[0].yfov == doctest::Approx(0.7f));

    REQUIRE(scene.lights.size() == 1);
    CHECK(scene.lights[0].type == render::LightType::Point);
    CHECK(scene.lights[0].instanceId == 305419896u);
    CHECK(scene.lights[0].intensity == doctest::Approx(5.0f));
    CHECK(scene.lights[0].range == doctest::Approx(10.0f));
    CHECK(scene.lights[0].sourceRadius == doctest::Approx(0.3f));
    CHECK(scene.lights[0].sourceLength == doctest::Approx(0.4f));
    CHECK(scene.lights[0].lightSourceAngle == doctest::Approx(12.0f));
    CHECK(scene.lights[0].barnDoorAngle == doctest::Approx(40.0f));
    CHECK(scene.lights[0].barnDoorLength == doctest::Approx(2.5f));
    CHECK(scene.lights[0].falloffMode == render::LightFalloffMode::BakeryPhysical);
    CHECK(scene.lights[0].useColorTemperature);
    CHECK(scene.lights[0].colorTemperature == doctest::Approx(2000.0f));
    CHECK(scene.lights[0].bakeMode == render::LightBakeMode::Mixed);
    CHECK(scene.lights[0].mixedMode == render::LightMixedMode::IndirectOnly);
    CHECK(scene.lights[0].lightingChannel == 2);
    CHECK(scene.lights[0].castShadows);
    CHECK(scene.lights[0].shadowType == render::LightShadowType::Hard);
    CHECK(scene.lights[0].shadowConfigured);
    CHECK(scene.lights[0].shadowTranslucency == doctest::Approx(0.35f));
    CHECK(scene.lights[0].enablePenumbra);
    CHECK(scene.lights[0].penumbraTint == Vec4(0.1f, 0.2f, 0.3f, 0.4f));
    CHECK(scene.lights[0].penumbraLuma == doctest::Approx(0.75f));
    CHECK(scene.lights[0].shadow.dimmer == doctest::Approx(0.4f));
    CHECK(scene.lights[0].shadow.fadeDistance == doctest::Approx(42.0f));
    CHECK(scene.lights[0].shadow.nearPlane == doctest::Approx(0.25f));
    CHECK(scene.lights[0].shadow.resolution == 1024);
    CHECK(scene.lights[0].shadow.tint == Vec3(0.1f, 0.2f, 0.3f));
    CHECK(scene.lights[0].shadow.penumbraTint);
    CHECK(scene.lights[0].shadow.updateMode == render::ShadowUpdateMode::OnEnable);
    CHECK_FALSE(scene.lights[0].shadow.contactShadows);
    CHECK(scene.lights[0].shadow.softness == doctest::Approx(0.8f));
    CHECK(scene.lights[0].shadow.blockerSamples == 12);
    CHECK(scene.lights[0].shadow.filterSamples == 20);
    CHECK(scene.lights[0].shadow.evsmExponent == doctest::Approx(28.0f));
    CHECK(scene.lights[0].shadow.evsmLightLeakBias == doctest::Approx(0.2f));
    CHECK(scene.lights[0].shadow.evsmVarianceBias == doctest::Approx(0.0005f));
    CHECK(scene.lights[0].shadow.evsmBlurPasses == 3u);
}

TEST_CASE("glTF loader reads node hierarchy and applies animation") {
    const std::filesystem::path directory = makeFixtureDirectory();
    const std::filesystem::path bufferPath = directory / "animated.bin";
    const std::filesystem::path gltfPath = directory / "animated.gltf";

    const std::vector<uint8_t> buffer = makeAnimatedBuffer();
    writeFile(bufferPath, buffer.data(), buffer.size());
    const std::string json = makeAnimatedGltf();
    writeFile(gltfPath, json.data(), json.size());

    SceneLoadResult result = SceneLoader::load(gltfPath);
    REQUIRE_MESSAGE(result.ok(), result.error);
    render::RenderScene& scene = *result.scene;

    REQUIRE(scene.nodes.size() == 2);
    CHECK(scene.nodes[0].parent == 1);
    CHECK(scene.nodes[1].parent == -1);
    REQUIRE(scene.animations.size() == 1);
    CHECK(scene.animations[0].name == "Move");
    REQUIRE(scene.animations[0].channels.size() == 2);
    CHECK(scene.animations[0].duration == doctest::Approx(1.0f));
    REQUIRE(scene.objects.size() == 1);
    CHECK(scene.objects[0].nodeIndex == 0);
    CHECK_FALSE(scene.objects[0].staticShadowCaster);

    REQUIRE(applyAnimation(scene, 0.5f));
    CHECK(transformPoint(scene.objects[0].world, Vec3(0.0f, 0.0f, 0.0f)).y == doctest::Approx(0.5f));
    const Vec3 halfway = transformDirection(scene.objects[0].world, Vec3(1.0f, 0.0f, 0.0f));
    CHECK(halfway.x == doctest::Approx(0.70710678f));
    CHECK(halfway.y == doctest::Approx(0.70710678f));

    REQUIRE(applyAnimation(scene, 1.0f, false));
    CHECK(transformPoint(scene.objects[0].world, Vec3(0.0f, 0.0f, 0.0f)).y == doctest::Approx(1.0f));
    const Vec3 rotated = transformDirection(scene.objects[0].world, Vec3(1.0f, 0.0f, 0.0f));
    CHECK(rotated.x == doctest::Approx(0.0f).epsilon(0.0001));
    CHECK(rotated.y == doctest::Approx(1.0f).epsilon(0.0001));

    REQUIRE(applyAnimation(scene, 1.25f));
    CHECK(transformPoint(scene.objects[0].world, Vec3(0.0f, 0.0f, 0.0f)).y == doctest::Approx(0.25f));
    REQUIRE(applyAnimation(scene, 1.25f, false));
    CHECK(transformPoint(scene.objects[0].world, Vec3(0.0f, 0.0f, 0.0f)).y == doctest::Approx(1.0f));
}

TEST_CASE("glTF loader applies cubic spline animation") {
    const std::filesystem::path directory = makeFixtureDirectory();
    const std::filesystem::path bufferPath = directory / "cubic.bin";
    const std::filesystem::path gltfPath = directory / "cubic.gltf";

    const std::vector<uint8_t> buffer = makeCubicBuffer();
    writeFile(bufferPath, buffer.data(), buffer.size());
    const std::string json = makeCubicGltf();
    writeFile(gltfPath, json.data(), json.size());

    SceneLoadResult result = SceneLoader::load(gltfPath);
    REQUIRE_MESSAGE(result.ok(), result.error);
    render::RenderScene& scene = *result.scene;

    REQUIRE(scene.animations.size() == 1u);
    REQUIRE(scene.animations[0].channels.size() == 1u);
    const AnimationChannel& channel = scene.animations[0].channels[0];
    CHECK(channel.interpolation == AnimationInterpolation::CubicSpline);
    REQUIRE(channel.inTangents.size() == 2u);
    REQUIRE(channel.outTangents.size() == 2u);
    CHECK(channel.outTangents[0].x == doctest::Approx(2.0f));
    REQUIRE(scene.objects.size() == 1u);

    REQUIRE(applyAnimation(scene, 0.5f));
    CHECK(transformPoint(scene.objects[0].world, Vec3(0.0f, 0.0f, 0.0f)).x == doctest::Approx(0.25f));
    CHECK(transformPoint(scene.objects[0].world, Vec3(0.0f, 0.0f, 0.0f)).y == doctest::Approx(0.0f));

    REQUIRE(applyAnimation(scene, 1.0f, false));
    CHECK(transformPoint(scene.objects[0].world, Vec3(0.0f, 0.0f, 0.0f)).x == doctest::Approx(0.0f));
}

TEST_CASE("glTF loader applies morph targets and weight animation") {
    const std::filesystem::path directory = makeFixtureDirectory();
    const std::filesystem::path bufferPath = directory / "morph.bin";
    const std::filesystem::path gltfPath = directory / "morph.gltf";

    const std::vector<uint8_t> buffer = makeMorphBuffer();
    writeFile(bufferPath, buffer.data(), buffer.size());
    const std::string json = makeMorphGltf();
    writeFile(gltfPath, json.data(), json.size());

    SceneLoadResult result = SceneLoader::load(gltfPath);
    REQUIRE_MESSAGE(result.ok(), result.error);
    render::RenderScene& scene = *result.scene;

    REQUIRE(scene.meshes.size() == 2u);
    REQUIRE(scene.objects.size() == 1u);
    const uint32_t meshIndex = scene.objects[0].meshIndex;
    REQUIRE(meshIndex == 1u);
    const Mesh& mesh = scene.meshes[meshIndex];
    REQUIRE(mesh.morphTargets.size() == 1u);
    CHECK(mesh.morphTargets[0].name == "Wide");
    CHECK(mesh.morphWeights.size() == 1u);
    CHECK(mesh.morphWeights[0] == doctest::Approx(0.0f));
    CHECK(mesh.morphTargets[0].hasNormals);

    REQUIRE(scene.nodes.size() == 2u);
    CHECK(scene.nodes[0].baseWeights.size() == 1u);
    CHECK(scene.nodes[0].baseWeights[0] == doctest::Approx(0.25f));
    CHECK(scene.objects[0].morphWeights.size() == 1u);
    CHECK(scene.objects[0].morphWeights[0] == doctest::Approx(0.25f));

    std::vector<MeshVertex> deformed;
    mesh.applyMorphWeights({0.5f}, deformed);
    REQUIRE(deformed.size() == mesh.vertices.size());
    CHECK(deformed[0].position.x == doctest::Approx(1.0f));
    CHECK(deformed[0].normal.x == doctest::Approx(0.4472136f));
    CHECK(deformed[0].normal.z == doctest::Approx(0.8944272f));
    CHECK(Mesh::computeBounds(deformed).max.x > Mesh::computeBounds(mesh.vertices).max.x);

    REQUIRE(scene.animations.size() == 1u);
    REQUIRE(scene.animations[0].channels.size() == 1u);
    CHECK(scene.animations[0].channels[0].path == AnimationPath::Weights);
    CHECK(scene.animations[0].channels[0].targetIndex == 0u);

    REQUIRE(applyAnimation(scene, 0.5f, false));
    CHECK(scene.objects[0].morphWeights[0] == doctest::Approx(0.5f));
}

TEST_CASE("glTF loader applies cubic morph weight animation") {
    const std::filesystem::path directory = makeFixtureDirectory();
    const std::filesystem::path bufferPath = directory / "morph_cubic.bin";
    const std::filesystem::path gltfPath = directory / "morph_cubic.gltf";

    const std::vector<uint8_t> buffer = makeMorphCubicBuffer();
    writeFile(bufferPath, buffer.data(), buffer.size());
    const std::string json = makeMorphCubicGltf();
    writeFile(gltfPath, json.data(), json.size());

    SceneLoadResult result = SceneLoader::load(gltfPath);
    REQUIRE_MESSAGE(result.ok(), result.error);
    render::RenderScene& scene = *result.scene;
    REQUIRE(scene.meshes.size() == 2u);
    REQUIRE(scene.objects.size() == 1u);
    REQUIRE(scene.animations.size() == 1u);
    REQUIRE(scene.animations[0].channels.size() == 1u);
    const AnimationChannel& channel = scene.animations[0].channels[0];
    CHECK(channel.path == AnimationPath::Weights);
    CHECK(channel.targetIndex == 0u);
    REQUIRE(channel.outTangents.size() == 2u);
    CHECK(channel.outTangents[0].x == doctest::Approx(2.0f));

    REQUIRE(applyAnimation(scene, 0.5f, false));
    CHECK(scene.objects[0].morphWeights[0] == doctest::Approx(0.25f));
}

#if defined(HBG_HAS_DRACO) && HBG_HAS_DRACO
TEST_CASE("glTF loader decodes KHR_draco_mesh_compression") {
    std::vector<uint8_t> compressed;
    REQUIRE(encodeDracoTriangle(compressed));

    const std::filesystem::path directory = makeFixtureDirectory();
    const std::filesystem::path bufferPath = directory / "draco.bin";
    const std::filesystem::path gltfPath = directory / "draco.gltf";
    writeFile(bufferPath, compressed.data(), compressed.size());
    const std::string json = makeDracoGltf(compressed.size());
    writeFile(gltfPath, json.data(), json.size());

    const SceneLoadResult result = SceneLoader::load(gltfPath);
    REQUIRE_MESSAGE(result.ok(), result.error);
    const render::RenderScene& scene = *result.scene;
    REQUIRE(scene.objects.size() == 1u);
    const render::RenderObject& object = scene.objects[0];
    const Mesh& mesh = scene.meshes[object.meshIndex];
    REQUIRE(mesh.vertices.size() == 3u);
    REQUIRE(mesh.indices.size() == 3u);

    Vec3 minimum(FLT_MAX, FLT_MAX, FLT_MAX);
    Vec3 maximum(-FLT_MAX, -FLT_MAX, -FLT_MAX);
    for (const MeshVertex& vertex : mesh.vertices) {
        minimum.x = std::min(minimum.x, vertex.position.x);
        minimum.y = std::min(minimum.y, vertex.position.y);
        minimum.z = std::min(minimum.z, vertex.position.z);
        maximum.x = std::max(maximum.x, vertex.position.x);
        maximum.y = std::max(maximum.y, vertex.position.y);
        maximum.z = std::max(maximum.z, vertex.position.z);
        CHECK(vertex.normal.z == doctest::Approx(1.0f).epsilon(0.001));
    }
    CHECK(minimum.x == doctest::Approx(-1.0f).epsilon(0.001));
    CHECK(minimum.y == doctest::Approx(-1.0f).epsilon(0.001));
    CHECK(maximum.x == doctest::Approx(1.0f).epsilon(0.001));
    CHECK(maximum.y == doctest::Approx(1.0f).epsilon(0.001));

    bool seen[3] = {false, false, false};
    for (const uint32_t index : mesh.indices) {
        REQUIRE(index < 3u);
        CHECK_FALSE(seen[index]);
        seen[index] = true;
    }
    CHECK(seen[0]);
    CHECK(seen[1]);
    CHECK(seen[2]);

    bool sawUv0 = false;
    bool sawUv1 = false;
    bool sawUv2 = false;
    for (const MeshVertex& vertex : mesh.vertices) {
        if (vertex.texcoord0.x == doctest::Approx(0.0f).epsilon(0.01) &&
            vertex.texcoord0.y == doctest::Approx(0.0f).epsilon(0.01)) {
            sawUv0 = true;
        } else if (vertex.texcoord0.x == doctest::Approx(1.0f).epsilon(0.01) &&
                   vertex.texcoord0.y == doctest::Approx(0.0f).epsilon(0.01)) {
            sawUv1 = true;
        } else if (vertex.texcoord0.x == doctest::Approx(0.5f).epsilon(0.01) &&
                   vertex.texcoord0.y == doctest::Approx(1.0f).epsilon(0.01)) {
            sawUv2 = true;
        }
    }
    CHECK(sawUv0);
    CHECK(sawUv1);
    CHECK(sawUv2);
}

TEST_CASE("glTF loader reads Draco attribute ids from GLB JSON") {
    std::vector<uint8_t> compressed;
    REQUIRE(encodeDracoTriangle(compressed));

    std::string json = makeDracoGltf(compressed.size());
    const std::string uri = "\"uri\":\"draco.bin\",";
    const std::size_t uriPosition = json.find(uri);
    REQUIRE(uriPosition != std::string::npos);
    json.erase(uriPosition, uri.size());

    const std::filesystem::path directory = makeFixtureDirectory();
    const std::filesystem::path glbPath = directory / "draco.glb";
    const std::vector<uint8_t> glb = makeGlb(json, compressed);
    writeFile(glbPath, glb.data(), glb.size());

    const SceneLoadResult result = SceneLoader::load(glbPath);
    REQUIRE_MESSAGE(result.ok(), result.error);
    const render::RenderScene& scene = *result.scene;
    REQUIRE(scene.objects.size() == 1u);
    const Mesh& mesh = scene.meshes[scene.objects[0].meshIndex];
    REQUIRE(mesh.vertices.size() == 3u);
    REQUIRE(mesh.indices.size() == 3u);
    CHECK(mesh.vertices[0].normal.z == doctest::Approx(1.0f).epsilon(0.001));
}

TEST_CASE("glTF loader fails when required Draco data is corrupt") {
    std::vector<uint8_t> compressed;
    REQUIRE(encodeDracoTriangle(compressed));
    compressed.front() ^= 0xFFu;

    const std::filesystem::path directory = makeFixtureDirectory();
    const std::filesystem::path bufferPath = directory / "draco_corrupt.bin";
    const std::filesystem::path gltfPath = directory / "draco_corrupt.gltf";
    writeFile(bufferPath, compressed.data(), compressed.size());
    std::string json = makeDracoGltf(compressed.size());
    const std::string uri = "\"uri\":\"draco.bin\",";
    const std::size_t uriPosition = json.find(uri);
    REQUIRE(uriPosition != std::string::npos);
    json.replace(uriPosition, uri.size(), "\"uri\":\"draco_corrupt.bin\",");
    writeFile(gltfPath, json.data(), json.size());

    const SceneLoadResult result = SceneLoader::load(gltfPath);
    CHECK_FALSE(result.ok());
}

TEST_CASE("glTF loader decodes Draco skin attributes") {
    std::vector<uint8_t> compressed;
    REQUIRE(encodeDracoSkin(compressed));

    const std::filesystem::path directory = makeFixtureDirectory();
    const std::filesystem::path bufferPath = directory / "draco_skin.bin";
    const std::filesystem::path gltfPath = directory / "draco_skin.gltf";
    writeFile(bufferPath, compressed.data(), compressed.size());
    const std::string json = makeDracoSkinGltf(compressed.size());
    writeFile(gltfPath, json.data(), json.size());

    const SceneLoadResult result = SceneLoader::load(gltfPath);
    REQUIRE_MESSAGE(result.ok(), result.error);
    const render::RenderScene& scene = *result.scene;
    REQUIRE(scene.skins.size() == 1u);
    REQUIRE(scene.objects.size() == 1u);
    const render::RenderObject& object = scene.objects[0];
    CHECK(object.skinIndex == 0);
    const Mesh& mesh = scene.meshes[object.meshIndex];
    REQUIRE(mesh.skinVertices.size() == 3u);
    REQUIRE(mesh.skinInfluences.size() == 12u);

    for (std::size_t vertex = 0; vertex < 3; ++vertex) {
        const SkinVertex& skin = mesh.skinVertices[vertex];
        REQUIRE(skin.influenceCount == 4u);
        const std::size_t offset = skin.influenceOffset;
        CHECK(mesh.skinInfluences[offset + 0].weight == doctest::Approx(1.0f));
        CHECK(mesh.skinInfluences[offset + 0].joint == (vertex == 0 ? 0 : 1));
        CHECK(mesh.skinInfluences[offset + 1].weight == doctest::Approx(0.0f));
    }
}
#endif

TEST_CASE("glTF loader applies skin joints and inverse bind matrices") {
    const std::filesystem::path directory = makeFixtureDirectory();
    const std::filesystem::path bufferPath = directory / "skin.bin";
    const std::filesystem::path gltfPath = directory / "skin.gltf";

    const std::vector<uint8_t> buffer = makeSkinBuffer();
    writeFile(bufferPath, buffer.data(), buffer.size());
    const std::string json = makeSkinGltf();
    writeFile(gltfPath, json.data(), json.size());

    SceneLoadResult result = SceneLoader::load(gltfPath);
    REQUIRE_MESSAGE(result.ok(), result.error);
    render::RenderScene& scene = *result.scene;

    REQUIRE(scene.skins.size() == 1u);
    REQUIRE(scene.skins[0].joints.size() == 2u);
    CHECK(scene.skins[0].joints[0] == 1u);
    CHECK(scene.skins[0].joints[1] == 2u);
    REQUIRE(scene.skins[0].inverseBindMatrices.size() == 2u);
    CHECK(scene.skins[0].inverseBindMatrices[1][12] == doctest::Approx(-1.0f));

    REQUIRE(scene.objects.size() == 1u);
    const render::RenderObject& object = scene.objects[0];
    CHECK(object.skinIndex == 0);
    CHECK(object.world[12] == doctest::Approx(10.0f));
    CHECK(object.worldBounds.max.x == doctest::Approx(2.0f));
    CHECK_FALSE(object.staticShadowCaster);
    REQUIRE(scene.meshes.size() == 2u);
    const Mesh& mesh = scene.meshes[object.meshIndex];
    REQUIRE(mesh.skinVertices.size() == 3u);
    REQUIRE(mesh.skinInfluences.size() == 36u);
    CHECK(mesh.skinVertices[0].influenceCount == 12u);
    const std::size_t firstOffset = mesh.skinVertices[0].influenceOffset;
    CHECK(mesh.skinInfluences[firstOffset + 4].joint == 1);
    CHECK(mesh.skinInfluences[firstOffset + 0].weight == doctest::Approx(0.5f));
    CHECK(mesh.skinInfluences[firstOffset + 4].weight == doctest::Approx(0.5f));
    const std::size_t lastOffset = mesh.skinVertices[2].influenceOffset;
    CHECK(mesh.skinInfluences[lastOffset + 0].joint == 1);
    CHECK(mesh.skinInfluences[lastOffset + 0].weight == doctest::Approx(1.0f));

    std::vector<MeshVertex> deformed;
    REQUIRE(applyAnimation(scene, 0.0f, false));
    deformed.clear();
    mesh.applySkinning(computeSkinMatrices(scene, object), deformed);
    REQUIRE(deformed.size() == 3u);
    const Vec3 bindVertex = transformPoint(object.world, deformed[2].position);
    CHECK(bindVertex.x == doctest::Approx(2.0f));
    CHECK(bindVertex.y == doctest::Approx(0.0f));

    REQUIRE(applyAnimation(scene, 1.0f, false));
    deformed.clear();
    mesh.applySkinning(computeSkinMatrices(scene, object), deformed);
    const Vec3 jointVertex = transformPoint(object.world, deformed[1].position);
    CHECK(jointVertex.x == doctest::Approx(0.0f));
    CHECK(jointVertex.y == doctest::Approx(0.0f));
    const Vec3 tipVertex = transformPoint(object.world, deformed[2].position);
    CHECK(tipVertex.x == doctest::Approx(1.0f));
    CHECK(tipVertex.y == doctest::Approx(1.0f));
    CHECK(deformed[2].normal.x == doctest::Approx(0.0f).epsilon(0.0001));
    CHECK(deformed[2].normal.y == doctest::Approx(1.0f).epsilon(0.0001));
}

TEST_CASE("mesh applies linear blend skinning") {
    Mesh mesh;
    MeshVertex vertex;
    vertex.position = Vec3(0.0f, 0.0f, 0.0f);
    vertex.normal = Vec3(0.0f, 0.0f, 1.0f);
    mesh.vertices.push_back(vertex);
    mesh.skinVertices.resize(1);
    mesh.skinVertices[0].influenceCount = 2;
    mesh.skinInfluences = {{0, 0.5f}, {1, 0.5f}};

    Mat4 translated = Mat4::identity();
    translated[12] = 2.0f;
    const std::vector<Mat4> matrices = {Mat4::identity(), translated};
    std::vector<MeshVertex> deformed;
    mesh.applySkinning(matrices, deformed);
    REQUIRE(deformed.size() == 1u);
    CHECK(deformed[0].position.x == doctest::Approx(1.0f));
    CHECK(deformed[0].position.y == doctest::Approx(0.0f));
    CHECK(deformed[0].normal.z == doctest::Approx(1.0f));
}

TEST_CASE("mesh normalizes non-unit skin weights") {
    Mesh mesh;
    MeshVertex vertex;
    vertex.position = Vec3(0.0f, 0.0f, 0.0f);
    vertex.normal = Vec3(0.0f, 0.0f, 1.0f);
    mesh.vertices.push_back(vertex);
    mesh.skinVertices.resize(1);
    mesh.skinVertices[0].influenceCount = 2;
    mesh.skinInfluences = {{0, 0.25f}, {1, 0.25f}};

    Mat4 translated = Mat4::identity();
    translated[12] = 2.0f;
    const std::vector<Mat4> matrices = {Mat4::identity(), translated};
    std::vector<MeshVertex> deformed;
    mesh.applySkinning(matrices, deformed);
    REQUIRE(deformed.size() == 1u);
    CHECK(deformed[0].position.x == doctest::Approx(1.0f));
    CHECK(deformed[0].normal.z == doctest::Approx(1.0f));
}

TEST_CASE("mesh applies linear blend skinning across three influence sets") {
    Mesh mesh;
    MeshVertex vertex;
    vertex.position = Vec3(0.0f, 0.0f, 0.0f);
    vertex.normal = Vec3(0.0f, 0.0f, 1.0f);
    mesh.vertices.push_back(vertex);
    mesh.skinVertices.resize(1);
    mesh.skinVertices[0].influenceCount = 3;
    mesh.skinInfluences = {{0, 0.5f}, {1, 0.25f}, {2, 0.25f}};

    Mat4 translatedX = Mat4::identity();
    translatedX[12] = 2.0f;
    Mat4 translatedY = Mat4::identity();
    translatedY[13] = 4.0f;
    const std::vector<Mat4> matrices = {Mat4::identity(), translatedX, translatedY};
    std::vector<MeshVertex> deformed;
    mesh.applySkinning(matrices, deformed);
    REQUIRE(deformed.size() == 1u);
    CHECK(deformed[0].position.x == doctest::Approx(0.5f));
    CHECK(deformed[0].position.y == doctest::Approx(1.0f));
    CHECK(deformed[0].normal.z == doctest::Approx(1.0f));
}

TEST_CASE("mesh computes deformed bounds") {
    Mesh mesh;
    MeshVertex first;
    first.position = Vec3(0.0f, 0.0f, 0.0f);
    MeshVertex last;
    last.position = Vec3(2.0f, 0.0f, 0.0f);
    mesh.vertices = {first, last};
    mesh.skinVertices.resize(2);
    mesh.skinVertices[0] = {0, 1};
    mesh.skinVertices[1] = {1, 1};
    mesh.skinInfluences = {{0, 1.0f}, {0, 1.0f}};

    const Bounds baseBounds = Mesh::computeBounds(mesh.vertices);
    REQUIRE(baseBounds.valid());
    CHECK(baseBounds.min.x == doctest::Approx(0.0f));
    CHECK(baseBounds.max.x == doctest::Approx(2.0f));

    Mat4 translated = Mat4::identity();
    translated[12] = 3.0f;
    std::vector<MeshVertex> deformed;
    mesh.applySkinning({translated}, deformed);
    const Bounds deformedBounds = Mesh::computeBounds(deformed);
    REQUIRE(deformedBounds.valid());
    CHECK(deformedBounds.min.x == doctest::Approx(3.0f));
    CHECK(deformedBounds.max.x == doctest::Approx(5.0f));
    const Bounds skinnedBounds = mesh.computeSkinnedBounds({translated});
    REQUIRE(skinnedBounds.valid());
    CHECK(skinnedBounds.min.x == doctest::Approx(deformedBounds.min.x));
    CHECK(skinnedBounds.max.x == doctest::Approx(deformedBounds.max.x));
}

TEST_CASE("glTF loader applies texture sampler wrap and filter") {
    const std::filesystem::path directory = makeFixtureDirectory();
    const std::filesystem::path bufferPath = directory / "triangle_sampler.bin";
    const std::filesystem::path gltfPath = directory / "triangle_sampler.gltf";

    const std::vector<uint8_t> buffer = makeTriangleBuffer();
    writeFile(bufferPath, buffer.data(), buffer.size());
    const std::string json = makeGltf(
        kPngDataUri, true, true,
        R"({"wrapS":33071,"wrapT":33648,"magFilter":9728,"minFilter":9984})");
    writeFile(gltfPath, json.data(), json.size());

    const SceneLoadResult result = SceneLoader::load(gltfPath);
    REQUIRE_MESSAGE(result.ok(), result.error);
    REQUIRE(result.scene->materials.size() == 1);
    const Material& material = result.scene->materials[0];
    CHECK(material.maskMapPacked);
    CHECK(material.baseColorTexture.sampler.wrapS == TextureWrap::Clamp);
    CHECK(material.baseColorTexture.sampler.wrapT == TextureWrap::Mirror);
    CHECK(material.baseColorTexture.sampler.minFilter == TextureFilter::Nearest);
    CHECK(material.baseColorTexture.sampler.magFilter == TextureFilter::Nearest);

    const uint64_t flags = TextureLoader::samplerFlags(material.baseColorTexture.sampler);
    CHECK((flags & BGFX_SAMPLER_U_CLAMP) != 0);
    CHECK((flags & BGFX_SAMPLER_V_MIRROR) != 0);
    CHECK((flags & BGFX_SAMPLER_MIN_POINT) != 0);
    CHECK((flags & BGFX_SAMPLER_MAG_POINT) != 0);
}

TEST_CASE("material key distinguishes sampler state") {
    Material material;
    material.baseColorTexture.sampler.wrapS = TextureWrap::Repeat;
    const MaterialKey repeatKey = makeMaterialKey(material);
    material.baseColorTexture.sampler.wrapS = TextureWrap::Clamp;
    const MaterialKey clampKey = makeMaterialKey(material);
    CHECK_FALSE(repeatKey == clampKey);
    const MaterialKeyHash hash;
    CHECK(hash(repeatKey) != hash(clampKey));
}

TEST_CASE("texture loader packs glTF occlusion roughness metallic mask") {
    const ImageAsset metallicRoughness = makeMaskImage(2, 2, {
        {255, 10, 20, 255},
        {255, 30, 40, 255},
        {255, 50, 60, 255},
        {255, 70, 80, 255},
    });
    const ImageAsset occlusion = makeMaskImage(2, 2, {
        {1, 255, 255, 255},
        {2, 255, 255, 255},
        {3, 255, 255, 255},
        {4, 255, 255, 255},
    });

    ImageAsset packed;
    std::string error;
    REQUIRE_MESSAGE(TextureLoader::packOrmMask(&occlusion, &metallicRoughness, packed, &error), error);
    CHECK(packed.width == 2u);
    CHECK(packed.height == 2u);
    CHECK(packed.mipCount == 1u);
    CHECK(packed.format == gfx::TextureFormat::RGBA8);
    REQUIRE(packed.pixels.size() == 16u);
    CHECK(packed.pixels[0] == 1u);
    CHECK(packed.pixels[1] == 10u);
    CHECK(packed.pixels[2] == 20u);
    CHECK(packed.pixels[3] == 255u);
    CHECK(packed.pixels[4] == 2u);
    CHECK(packed.pixels[5] == 30u);
    CHECK(packed.pixels[6] == 40u);
    CHECK(packed.pixels[12] == 4u);
    CHECK(packed.pixels[13] == 70u);
    CHECK(packed.pixels[14] == 80u);
}

TEST_CASE("texture loader fills missing mask channels with neutral values") {
    const ImageAsset metallicRoughness = makeMaskImage(1, 1, {{255, 64, 128, 255}});
    const ImageAsset occlusion = makeMaskImage(1, 1, {{32, 255, 255, 255}});

    ImageAsset packed;
    REQUIRE(TextureLoader::packOrmMask(&occlusion, nullptr, packed));
    CHECK(packed.pixels[0] == 32u);
    CHECK(packed.pixels[1] == 255u);
    CHECK(packed.pixels[2] == 255u);
    CHECK(packed.pixels[3] == 255u);

    REQUIRE(TextureLoader::packOrmMask(nullptr, &metallicRoughness, packed));
    CHECK(packed.pixels[0] == 255u);
    CHECK(packed.pixels[1] == 64u);
    CHECK(packed.pixels[2] == 128u);
    CHECK(packed.pixels[3] == 255u);
}

TEST_CASE("texture loader resamples and rejects unsupported mask sources") {
    const ImageAsset metallicRoughness = makeMaskImage(2, 1, {
        {255, 0, 0, 255},
        {255, 255, 255, 255},
    });
    const ImageAsset occlusion = makeMaskImage(1, 1, {{64, 255, 255, 255}});

    ImageAsset packed;
    REQUIRE(TextureLoader::packOrmMask(&occlusion, &metallicRoughness, packed));
    REQUIRE(packed.width == 2u);
    REQUIRE(packed.height == 1u);
    CHECK(packed.pixels[0] == 64u);
    CHECK(packed.pixels[1] == 0u);
    CHECK(packed.pixels[2] == 0u);
    CHECK(packed.pixels[4] == 64u);
    CHECK(packed.pixels[5] == 255u);
    CHECK(packed.pixels[6] == 255u);

    ImageAsset compressed = metallicRoughness;
    compressed.format = gfx::TextureFormat::BC1;
    compressed.pixels.assign(8u, 0u);
    std::string error;
    CHECK_FALSE(TextureLoader::packOrmMask(nullptr, &compressed, packed, &error));
    CHECK_FALSE(error.empty());
    CHECK_FALSE(TextureLoader::packOrmMask(nullptr, nullptr, packed, &error));
}

TEST_CASE("texture loader composes layered 2D array, 3D, and cubemap sources") {
    const ImageAsset red = makeMaskImage(1, 1, {{255, 0, 0, 255}});
    const ImageAsset green = makeMaskImage(1, 1, {{0, 255, 0, 255}});
    const ImageAsset blue = makeMaskImage(1, 1, {{0, 0, 255, 255}});

    ImageAsset array;
    REQUIRE_MESSAGE(TextureLoader::composeLayered(
                        {&red, &green}, LayeredTextureLayout::Texture2DArray, array),
                    "2D array composition failed");
    CHECK(array.width == 1u);
    CHECK(array.height == 1u);
    CHECK(array.layers == 2u);
    CHECK(array.depth == 1u);
    CHECK_FALSE(array.is3D);
    CHECK_FALSE(array.cubeMap);
    REQUIRE(array.pixels.size() == 8u);
    CHECK(array.pixels[0] == 255u);
    CHECK(array.pixels[4] == 0u);

    ImageAsset volume;
    REQUIRE(TextureLoader::composeLayered({&red, &green, &blue}, LayeredTextureLayout::Texture3D,
                                          volume));
    CHECK(volume.layers == 1u);
    CHECK(volume.depth == 3u);
    CHECK(volume.is3D);
    CHECK_FALSE(volume.cubeMap);
    REQUIRE(volume.pixels.size() == 12u);

    ImageAsset cube;
    REQUIRE(TextureLoader::composeLayered(
        {&red, &green, &blue, &red, &green, &blue}, LayeredTextureLayout::Cubemap, cube));
    CHECK(cube.cubeMap);
    CHECK_FALSE(cube.is3D);
    CHECK(cube.layers == 1u);
    REQUIRE(cube.pixels.size() == 24u);

    std::string error;
    CHECK_FALSE(TextureLoader::composeLayered({&red, &green}, LayeredTextureLayout::Cubemap, cube,
                                              &error));
    CHECK_FALSE(error.empty());
    const ImageAsset wide = makeMaskImage(2, 1, {{255, 0, 0, 255}, {0, 255, 0, 255}});
    CHECK_FALSE(TextureLoader::composeLayered({&red, &wide}, LayeredTextureLayout::Texture2DArray,
                                              array, &error));
    ImageAsset mip = red;
    mip.mipCount = 2;
    mip.pixels.resize(8u);
    CHECK_FALSE(TextureLoader::composeLayered({&red, &mip}, LayeredTextureLayout::Texture2DArray,
                                              array, &error));
}

TEST_CASE("scene sidecar converts a punctual light to an area light") {
    const std::filesystem::path directory = makeFixtureDirectory();
    const std::filesystem::path bufferPath = directory / "triangle_area.bin";
    const std::filesystem::path gltfPath = directory / "triangle_area.gltf";
    const std::filesystem::path sidecarPath = directory / "triangle_area.hdrp.json";

    const std::vector<uint8_t> buffer = makeTriangleBuffer();
    writeFile(bufferPath, buffer.data(), buffer.size());
    const std::string json = makeGltf(kPngDataUri, true);
    writeFile(gltfPath, json.data(), json.size());

    const std::string sidecar = R"({
        "lights":[{
            "name":"LightNode",
            "type":"area",
            "shape":"rectangle",
            "width":2.0,
            "height":1.0,
            "range":8.0,
            "areaShadowCone":0.6,
            "cookieScale":[2.0,3.0],
            "cookieOffset":[0.25,0.5]
        }]
    })";
    writeFile(sidecarPath, sidecar.data(), sidecar.size());

    const SceneLoadResult result = SceneLoader::load(gltfPath);
    REQUIRE_MESSAGE(result.ok(), result.error);
    const render::RenderScene& scene = *result.scene;
    REQUIRE(scene.lights.size() == 1);
    const render::Light& light = scene.lights[0];
    CHECK(light.type == render::LightType::Area);
    CHECK(light.areaShape == render::LightAreaShape::Rectangle);
    CHECK(light.areaWidth == doctest::Approx(2.0f));
    CHECK(light.areaHeight == doctest::Approx(1.0f));
    CHECK(light.areaShadowCone == doctest::Approx(0.6f));
    CHECK_FALSE(light.castShadows);
    CHECK(light.cookieScale == Vec2(2.0f, 3.0f));
    CHECK(light.cookieOffset == Vec2(0.25f, 0.5f));
}

TEST_CASE("scene sidecar enables rectangle area shadows only when shadow is authored") {
    const std::filesystem::path directory = makeFixtureDirectory();
    const std::filesystem::path bufferPath = directory / "triangle_area_shadow.bin";
    const std::filesystem::path gltfPath = directory / "triangle_area_shadow.gltf";
    const std::filesystem::path sidecarPath = directory / "triangle_area_shadow.hdrp.json";

    const std::vector<uint8_t> buffer = makeTriangleBuffer();
    writeFile(bufferPath, buffer.data(), buffer.size());
    const std::string json = makeGltf(kPngDataUri, true);
    writeFile(gltfPath, json.data(), json.size());

    const std::string sidecar = R"({
        "lights":[{
            "name":"LightNode",
            "type":"area",
            "shape":"rectangle",
            "width":2.0,
            "height":1.0,
            "range":8.0,
            "shadow":{"resolution":256,"softness":0.4}
        }]
    })";
    writeFile(sidecarPath, sidecar.data(), sidecar.size());

    const SceneLoadResult result = SceneLoader::load(gltfPath);
    REQUIRE_MESSAGE(result.ok(), result.error);
    const render::RenderScene& scene = *result.scene;
    REQUIRE(scene.lights.size() == 1);
    const render::Light& light = scene.lights[0];
    CHECK(light.type == render::LightType::Area);
    CHECK(light.areaShape == render::LightAreaShape::Rectangle);
    CHECK(light.castShadows);
    CHECK(light.shadowConfigured);
    CHECK(light.shadow.resolution == 256);
    CHECK(light.areaShadowCone == doctest::Approx(120.0f));
}

TEST_CASE("scene sidecar loads a tube area light as a line") {
    const std::filesystem::path directory = makeFixtureDirectory();
    const std::filesystem::path bufferPath = directory / "triangle_tube.bin";
    const std::filesystem::path gltfPath = directory / "triangle_tube.gltf";
    const std::filesystem::path sidecarPath = directory / "triangle_tube.hdrp.json";

    const std::vector<uint8_t> buffer = makeTriangleBuffer();
    writeFile(bufferPath, buffer.data(), buffer.size());
    const std::string json = makeGltf(kPngDataUri, true);
    writeFile(gltfPath, json.data(), json.size());

    const std::string sidecar = R"({
        "lights":[{
            "name":"LightNode",
            "type":"area",
            "shape":"tube",
            "width":3.0,
            "height":2.0,
            "intensity":7.0,
            "range":9.0
        }]
    })";
    writeFile(sidecarPath, sidecar.data(), sidecar.size());

    const SceneLoadResult result = SceneLoader::load(gltfPath);
    REQUIRE_MESSAGE(result.ok(), result.error);
    REQUIRE(result.scene->lights.size() == 1);
    const render::Light& light = result.scene->lights[0];
    CHECK(light.type == render::LightType::Area);
    CHECK(light.areaShape == render::LightAreaShape::Tube);
    CHECK(light.areaWidth == doctest::Approx(3.0f));
    CHECK(light.areaHeight == doctest::Approx(0.0f));
    CHECK(light.intensity == doctest::Approx(7.0f));
}

TEST_CASE("scene sidecar disables an unsupported disc area light") {
    const std::filesystem::path directory = makeFixtureDirectory();
    const std::filesystem::path bufferPath = directory / "triangle_disc.bin";
    const std::filesystem::path gltfPath = directory / "triangle_disc.gltf";
    const std::filesystem::path sidecarPath = directory / "triangle_disc.hdrp.json";

    const std::vector<uint8_t> buffer = makeTriangleBuffer();
    writeFile(bufferPath, buffer.data(), buffer.size());
    const std::string json = makeGltf(kPngDataUri, true);
    writeFile(gltfPath, json.data(), json.size());

    const std::string sidecar = R"({
        "lights":[{
            "name":"LightNode",
            "type":"area",
            "shape":"disc",
            "width":4.0,
            "height":4.0,
            "intensity":7.0,
            "range":9.0
        }]
    })";
    writeFile(sidecarPath, sidecar.data(), sidecar.size());

    const SceneLoadResult result = SceneLoader::load(gltfPath);
    REQUIRE_MESSAGE(result.ok(), result.error);
    REQUIRE(result.scene->lights.size() == 1);
    const render::Light& light = result.scene->lights[0];
    CHECK(light.type == render::LightType::Area);
    CHECK(light.areaShape == render::LightAreaShape::Disc);
    CHECK(light.intensity == doctest::Approx(0.0f));
}

TEST_CASE("scene sidecar loads static decal data and textures") {
    const std::filesystem::path directory = makeFixtureDirectory();
    const std::filesystem::path bufferPath = directory / "triangle_decals.bin";
    const std::filesystem::path gltfPath = directory / "triangle_decals.gltf";
    const std::filesystem::path sidecarPath = directory / "triangle_decals.hdrp.json";
    const std::filesystem::path imagePath = directory / "decal.png";

    const std::vector<uint8_t> buffer = makeTriangleBuffer();
    writeFile(bufferPath, buffer.data(), buffer.size());
    writeFile(imagePath, kDecalPng, sizeof(kDecalPng));
    std::string json = makeGltf("", false, false);
    const std::size_t uri = json.find("\"uri\":\"triangle.bin\"");
    REQUIRE(uri != std::string::npos);
    json.replace(uri, std::string("\"uri\":\"triangle.bin\"").size(), "\"uri\":\"triangle_decals.bin\"");
    writeFile(gltfPath, json.data(), json.size());

    const std::string sidecar = R"({
        "decals": [{
            "name": "floor",
            "texture": "decal.png",
            "normalTexture": "decal.png",
            "maskTexture": "decal.png",
            "position": [1.0, 2.0, 3.0],
            "rotation": [0.0, 90.0, 0.0],
            "size": [2.0, 4.0, 6.0],
            "baseColor": [0.8, 0.7, 0.6, 0.5],
            "opacity": 0.75,
            "angleFade": 30.0,
            "normalBlend": 0.5,
            "metallic": 0.2,
            "ao": 0.8,
            "roughness": 0.4,
            "layerMask": 5,
            "channelMask": 17
        }]
    })";
    writeFile(sidecarPath, sidecar.data(), sidecar.size());

    const SceneLoadResult result = SceneLoader::load(gltfPath);
    REQUIRE_MESSAGE(result.ok(), result.error);
    const render::RenderScene& scene = *result.scene;
    REQUIRE(scene.images.size() == 3);
    REQUIRE(scene.decals.size() == 1);
    const render::Decal& decal = scene.decals[0];
    CHECK(decal.name == "floor");
    CHECK(decal.albedoImage == 0);
    CHECK(decal.normalImage == 1);
    CHECK(decal.maskImage == 2);
    CHECK(decal.position == Vec3(1.0f, 2.0f, 3.0f));
    CHECK(decal.rotation == Vec3(0.0f, 90.0f, 0.0f));
    CHECK(decal.size == Vec3(2.0f, 4.0f, 6.0f));
    CHECK(decal.baseColor == Vec4(0.8f, 0.7f, 0.6f, 0.5f));
    CHECK(decal.opacity == doctest::Approx(0.75f));
    CHECK(decal.angleFade == doctest::Approx(30.0f));
    CHECK(decal.normalBlend == doctest::Approx(0.5f));
    CHECK(decal.metallic == doctest::Approx(0.2f));
    CHECK(decal.ambientOcclusion == doctest::Approx(0.8f));
    CHECK(decal.roughness == doctest::Approx(0.4f));
    CHECK(decal.layerMask == 5u);
    CHECK(decal.channelMask == 17u);
    CHECK(decal.enabled);
}

TEST_CASE("scene sidecar loads static reflection probe data and textures") {
    const std::filesystem::path directory = makeFixtureDirectory();
    const std::filesystem::path bufferPath = directory / "triangle_probes.bin";
    const std::filesystem::path gltfPath = directory / "triangle_probes.gltf";
    const std::filesystem::path sidecarPath = directory / "triangle_probes.hdrp.json";
    const std::filesystem::path imagePath = directory / "probe.png";

    const std::vector<uint8_t> buffer = makeTriangleBuffer();
    writeFile(bufferPath, buffer.data(), buffer.size());
    writeFile(imagePath, kDecalPng, sizeof(kDecalPng));
    std::string json = makeGltf("", false, false);
    const std::size_t uri = json.find("\"uri\":\"triangle.bin\"");
    REQUIRE(uri != std::string::npos);
    json.replace(uri, std::string("\"uri\":\"triangle.bin\"").size(), "\"uri\":\"triangle_probes.bin\"");
    writeFile(gltfPath, json.data(), json.size());

    const std::string sidecar = R"({
        "probes": [{
            "name": "room",
            "texture": "probe.png",
            "position": [1.0, 2.0, 3.0],
            "rotation": [0.0, 90.0, 0.0],
            "boxExtents": [2.0, 3.0, 4.0],
            "blendDistance": 1.5,
            "intensity": 0.75
        }, {
            "texture": "probe.png",
            "enabled": false
        }]
    })";
    writeFile(sidecarPath, sidecar.data(), sidecar.size());

    const SceneLoadResult result = SceneLoader::load(gltfPath);
    REQUIRE_MESSAGE(result.ok(), result.error);
    const render::RenderScene& scene = *result.scene;
    REQUIRE(scene.images.size() == 2);
    REQUIRE(scene.probes.size() == 2);
    const render::ReflectionProbe& probe = scene.probes[0];
    CHECK(probe.name == "room");
    CHECK(probe.image == 0);
    CHECK(probe.position == Vec3(1.0f, 2.0f, 3.0f));
    CHECK(probe.rotation == Vec3(0.0f, 90.0f, 0.0f));
    CHECK(probe.boxExtents == Vec3(2.0f, 3.0f, 4.0f));
    CHECK(probe.blendDistance == doctest::Approx(1.5f));
    CHECK(probe.intensity == doctest::Approx(0.75f));
    CHECK(probe.enabled);
    CHECK(scene.probes[1].image == 1);
    CHECK_FALSE(scene.probes[1].enabled);
}

TEST_CASE("scene sidecar loads local fog volumes and density masks") {
    const std::filesystem::path directory = makeFixtureDirectory();
    const std::filesystem::path bufferPath = directory / "triangle_local_fog.bin";
    const std::filesystem::path gltfPath = directory / "triangle_local_fog.gltf";
    const std::filesystem::path sidecarPath = directory / "triangle_local_fog.hdrp.json";
    const std::filesystem::path imagePath = directory / "fog_mask.png";

    const std::vector<uint8_t> buffer = makeTriangleBuffer();
    writeFile(bufferPath, buffer.data(), buffer.size());
    writeFile(imagePath, kDecalPng, sizeof(kDecalPng));
    std::string json = makeGltf("", false, false);
    const std::size_t uri = json.find("\"uri\":\"triangle.bin\"");
    REQUIRE(uri != std::string::npos);
    json.replace(uri, std::string("\"uri\":\"triangle.bin\"").size(), "\"uri\":\"triangle_local_fog.bin\"");
    writeFile(gltfPath, json.data(), json.size());

    const std::string sidecar = R"({
        "localFogVolumes": [{
            "name": "mist",
            "shape": "box",
            "position": [1.0, 2.0, 3.0],
            "rotation": [0.0, 90.0, 0.0],
            "size": [4.0, 2.0, 6.0],
            "albedo": [0.8, 0.7, 0.6],
            "meanFreePath": 12.0,
            "anisotropy": 0.25,
            "positiveFade": [0.5, 0.75, 1.0],
            "negativeFade": [0.25, 0.5, 0.75],
            "invertFade": true,
            "falloffMode": "exponential",
            "distanceFadeStart": 5.0,
            "distanceFadeEnd": 40.0,
            "maskTexture": "fog_mask.png",
            "maskPlane": "yz",
            "maskTiling": [2.0, 3.0, 1.0],
            "maskScroll": [0.1, 0.2, 0.0],
            "maskIntensity": 0.75,
            "maskInvert": true
        }, {
            "name": "sphere",
            "shape": "sphere",
            "position": [0.0, 1.0, 0.0],
            "size": [2.0, 2.0, 2.0],
            "enabled": false
        }]
    })";
    writeFile(sidecarPath, sidecar.data(), sidecar.size());

    const SceneLoadResult result = SceneLoader::load(gltfPath);
    REQUIRE_MESSAGE(result.ok(), result.error);
    const render::RenderScene& scene = *result.scene;
    REQUIRE(scene.images.size() == 1);
    REQUIRE(scene.localFogVolumes.size() == 2);
    const render::LocalFogVolume& volume = scene.localFogVolumes[0];
    CHECK(volume.name == "mist");
    CHECK(volume.shape == render::LocalFogShape::Box);
    CHECK(volume.maskImage == 0);
    CHECK(volume.center == Vec3(1.0f, 2.0f, 3.0f));
    CHECK(volume.rotation == Vec3(0.0f, 90.0f, 0.0f));
    CHECK(volume.size == Vec3(4.0f, 2.0f, 6.0f));
    CHECK(volume.albedo == Vec3(0.8f, 0.7f, 0.6f));
    CHECK(volume.meanFreePath == doctest::Approx(12.0f));
    CHECK(volume.anisotropy == doctest::Approx(0.25f));
    CHECK(volume.positiveFade == Vec3(0.5f, 0.75f, 1.0f));
    CHECK(volume.negativeFade == Vec3(0.25f, 0.5f, 0.75f));
    CHECK(volume.invertFade);
    CHECK(volume.falloff == render::LocalFogFalloff::Exponential);
    CHECK(volume.distanceFadeStart == doctest::Approx(5.0f));
    CHECK(volume.distanceFadeEnd == doctest::Approx(40.0f));
    CHECK(volume.maskPlane == render::LocalFogMaskPlane::YZ);
    CHECK(volume.maskTiling == Vec3(2.0f, 3.0f, 1.0f));
    CHECK(volume.maskScroll == Vec3(0.1f, 0.2f, 0.0f));
    CHECK(volume.maskIntensity == doctest::Approx(0.75f));
    CHECK(volume.maskInvert);

    CHECK(scene.localFogVolumes[1].shape == render::LocalFogShape::Sphere);
    CHECK_FALSE(scene.localFogVolumes[1].enabled);
    CHECK(scene.localFogVolumes[1].maskImage == -1);
}

TEST_CASE("scene sidecar binds lightmap atlas rect and ST") {
    const std::filesystem::path directory = makeFixtureDirectory();
    const std::filesystem::path bufferPath = directory / "triangle_lightmap.bin";
    const std::filesystem::path gltfPath = directory / "triangle_lightmap.gltf";
    const std::filesystem::path sidecarPath = directory / "triangle_lightmap.hdrp.json";

    const std::vector<uint8_t> buffer = makeTriangleBuffer();
    writeFile(bufferPath, buffer.data(), buffer.size());
    std::string json = makeGltf(kPngDataUri, true);
    const std::size_t uri = json.find("\"uri\":\"triangle.bin\"");
    REQUIRE(uri != std::string::npos);
    json.replace(uri, std::string("\"uri\":\"triangle.bin\"").size(), "\"uri\":\"triangle_lightmap.bin\"");
    writeFile(gltfPath, json.data(), json.size());

    const std::string sidecar = R"({
        "lightmap": {
            "version": 1,
            "texelPerUnit": 6.0,
            "atlases": [{"file":"lm0","size":64,"layers":3,"format":"rgba16f"}],
            "objects": {
                "MeshNode": {"atlas":0,"rect":[2,2,32,32],"contributeGI":true}
            }
        }
    })";
    writeFile(sidecarPath, sidecar.data(), sidecar.size());

    const SceneLoadResult result = SceneLoader::load(gltfPath);
    REQUIRE_MESSAGE(result.ok(), result.error);
    const render::RenderScene& scene = *result.scene;
    REQUIRE(scene.lightmapAtlases.size() == 1);
    CHECK(scene.lightmapAtlases[0].file == "lm0");
    CHECK(scene.lightmapAtlases[0].size == 64);
    CHECK(scene.lightmapBaseDirectory == directory);
    REQUIRE(scene.objects.size() == 1);
    const render::RenderObject& object = scene.objects[0];
    CHECK(object.hasLightmap());
    CHECK(object.lightmapAtlas == 0u);
    CHECK(object.lightmapSize == Vec2(32.0f, 32.0f));
    CHECK(object.lightmapST.x == doctest::Approx(0.5f));
    CHECK(object.lightmapST.y == doctest::Approx(0.5f));
    CHECK(object.lightmapST.z == doctest::Approx(0.03125f));
    CHECK(object.lightmapST.w == doctest::Approx(0.03125f));
}

TEST_CASE("scene sidecar rejects an out-of-bounds lightmap rect") {
    const std::filesystem::path directory = makeFixtureDirectory();
    const std::filesystem::path bufferPath = directory / "triangle_lightmap_bad.bin";
    const std::filesystem::path gltfPath = directory / "triangle_lightmap_bad.gltf";
    const std::filesystem::path sidecarPath = directory / "triangle_lightmap_bad.hdrp.json";

    const std::vector<uint8_t> buffer = makeTriangleBuffer();
    writeFile(bufferPath, buffer.data(), buffer.size());
    std::string json = makeGltf(kPngDataUri, true);
    const std::size_t uri = json.find("\"uri\":\"triangle.bin\"");
    REQUIRE(uri != std::string::npos);
    json.replace(uri, std::string("\"uri\":\"triangle.bin\"").size(), "\"uri\":\"triangle_lightmap_bad.bin\"");
    writeFile(gltfPath, json.data(), json.size());

    const std::string sidecar = R"({
        "lightmap": {
            "version": 1,
            "texelPerUnit": 6.0,
            "atlases": [{"file":"lm0","size":64,"layers":3,"format":"rgba16f"}],
            "objects": {"MeshNode":{"atlas":0,"rect":[63,0,4,4]}}
        }
    })";
    writeFile(sidecarPath, sidecar.data(), sidecar.size());

    const SceneLoadResult result = SceneLoader::load(gltfPath);
    CHECK_FALSE(result.ok());
    CHECK(result.error.find("outside its atlas") != std::string::npos);
}

TEST_CASE("glTF loader handles primitives without uv or normals") {
    const std::filesystem::path directory = makeFixtureDirectory();
    const std::filesystem::path bufferPath = directory / "triangle_no_uv.bin";
    const std::filesystem::path gltfPath = directory / "triangle_no_uv.gltf";

    const std::vector<uint8_t> buffer = makeTriangleBuffer();
    writeFile(bufferPath, buffer.data(), buffer.size());
    std::string json = makeGltf("", false, false);
    const std::size_t uri = json.find("\"uri\":\"triangle.bin\"");
    REQUIRE(uri != std::string::npos);
    json.replace(uri, std::string("\"uri\":\"triangle.bin\"").size(), "\"uri\":\"triangle_no_uv.bin\"");
    writeFile(gltfPath, json.data(), json.size());

    const SceneLoadResult result = SceneLoader::load(gltfPath);
    REQUIRE_MESSAGE(result.ok(), result.error);
    const render::RenderScene& scene = *result.scene;

    REQUIRE(scene.meshes.size() == 1);
    CHECK_FALSE(scene.meshes[0].hasUv0);
    CHECK_FALSE(scene.meshes[0].hasTangents);
    CHECK(scene.meshes[0].hasNormals);
}

TEST_CASE("scene sidecar loads VFX instances and properties") {
    const std::filesystem::path directory = makeFixtureDirectory();
    const std::filesystem::path bufferPath = directory / "triangle_vfx.bin";
    const std::filesystem::path gltfPath = directory / "triangle_vfx.gltf";
    const std::filesystem::path sidecarPath = directory / "triangle_vfx.hdrp.json";

    const std::vector<uint8_t> buffer = makeTriangleBuffer();
    writeFile(bufferPath, buffer.data(), buffer.size());
    std::string json = makeGltf(kPngDataUri, true);
    const std::size_t uri = json.find("\"uri\":\"triangle.bin\"");
    REQUIRE(uri != std::string::npos);
    json.replace(uri, std::string("\"uri\":\"triangle.bin\"").size(), "\"uri\":\"triangle_vfx.bin\"");
    writeFile(gltfPath, json.data(), json.size());

    const std::string sidecar = R"({
        "vfx": [{
            "name": "Sparks",
            "asset": "effects/spark.vfxb",
            "position": [1.0, 2.0, 3.0],
            "rotation": [0.0, 90.0, 0.0],
            "scale": [2.0, 2.0, 2.0],
            "play": false,
            "playRate": 0.5,
            "seed": 42,
            "properties": [
                {"name": "Tint", "type": "color", "value": [1.0, 0.0, 0.0, 1.0]},
                {"name": "Size", "type": "float", "value": 0.3},
                {"name": "Direction", "type": "vector3", "value": [0.0, 1.0, 0.0]}
            ]
        }]
    })";
    writeFile(sidecarPath, sidecar.data(), sidecar.size());

    const SceneLoadResult result = SceneLoader::load(gltfPath);
    REQUIRE_MESSAGE(result.ok(), result.error);
    const render::RenderScene& scene = *result.scene;
    REQUIRE(scene.vfxInstances.size() == 1u);
    const render::VfxInstance& instance = scene.vfxInstances.front();
    CHECK(instance.name == "Sparks");
    CHECK(instance.asset == directory / "effects" / "spark.vfxb");
    CHECK(instance.position.x == doctest::Approx(1.0f));
    CHECK(instance.position.y == doctest::Approx(2.0f));
    CHECK(instance.position.z == doctest::Approx(3.0f));
    CHECK(instance.rotation.y == doctest::Approx(90.0f));
    CHECK(instance.scale.x == doctest::Approx(2.0f));
    CHECK_FALSE(instance.playOnAwake);
    CHECK(instance.playRate == doctest::Approx(0.5f));
    CHECK(instance.randomSeed == 42u);
    REQUIRE(instance.properties.size() == 3u);
    CHECK(instance.properties[0].name == "Tint");
    CHECK(instance.properties[0].kind == render::VfxPropertyKind::Color);
    CHECK(instance.properties[0].values[0] == doctest::Approx(1.0f));
    CHECK(instance.properties[1].kind == render::VfxPropertyKind::Float);
    CHECK(instance.properties[1].values[0] == doctest::Approx(0.3f));
    CHECK(instance.properties[2].kind == render::VfxPropertyKind::Vector3);
}

TEST_CASE("texture loader preserves KTX2 block-compressed mips") {
    const std::vector<uint8_t> bytes = makeKtx2Bc1();
    REQUIRE_FALSE(bytes.empty());

    ImageAsset image;
    std::string error;
    REQUIRE_MESSAGE(TextureLoader::loadFromMemory(bytes.data(), bytes.size(), image, &error), error);
    CHECK(image.width == 8u);
    CHECK(image.height == 8u);
    CHECK(image.mipCount == 4u);
    CHECK(image.format == gfx::TextureFormat::BC1);
    CHECK(image.isCompressed());
    CHECK(image.pixels.size() == 56u);
}

TEST_CASE("texture loader preserves uncompressed KTX2 mips") {
    const std::vector<uint8_t> bytes = makeKtx2Rgba8Mips();
    REQUIRE_FALSE(bytes.empty());

    ImageAsset image;
    std::string error;
    REQUIRE_MESSAGE(TextureLoader::loadFromMemory(bytes.data(), bytes.size(), image, &error), error);
    CHECK(image.width == 8u);
    CHECK(image.height == 8u);
    CHECK(image.mipCount == 4u);
    CHECK(image.format == gfx::TextureFormat::RGBA8);
    CHECK_FALSE(image.isCompressed());
    CHECK(image.pixels.size() == 340u);
}

TEST_CASE("texture loader preserves uncompressed KTX1 mips") {
    const std::vector<uint8_t> bytes = makeKtx1Rgba8Mips();
    REQUIRE_FALSE(bytes.empty());

    ImageAsset image;
    std::string error;
    REQUIRE_MESSAGE(TextureLoader::loadFromMemory(bytes.data(), bytes.size(), image, &error), error);
    CHECK(image.width == 8u);
    CHECK(image.height == 8u);
    CHECK(image.mipCount == 4u);
    CHECK(image.format == gfx::TextureFormat::RGBA8);
    CHECK_FALSE(image.isCompressed());
    CHECK(image.pixels.size() == 340u);
}

#if defined(HBG_HAS_BASISU) && HBG_HAS_BASISU
TEST_CASE("texture loader transcodes BasisU ETC1S KTX2") {
    const std::vector<uint8_t> bytes = decodeHex(kEtc1sOpaqueKtx2Hex);
    REQUIRE_FALSE(bytes.empty());

    ImageAsset image;
    std::string error;
    REQUIRE_MESSAGE(TextureLoader::loadFromMemory(bytes.data(), bytes.size(), image, &error), error);
    CHECK(image.width == 16u);
    CHECK(image.height == 16u);
    CHECK(image.mipCount == 5u);
    CHECK(image.format == gfx::TextureFormat::BC1);
    CHECK(image.isCompressed());
    CHECK(image.pixels.size() == 184u);
    CHECK(std::any_of(image.pixels.begin(), image.pixels.end(), [](uint8_t value) { return value != 0; }));

    std::vector<uint8_t> truncated(bytes.begin(), bytes.begin() + 100);
    ImageAsset rejected;
    std::string rejectError;
    CHECK_FALSE(TextureLoader::loadFromMemory(truncated.data(), truncated.size(), rejected, &rejectError));
    CHECK_FALSE(rejectError.empty());
}

TEST_CASE("texture loader transcodes BasisU UASTC alpha KTX2") {
    const std::vector<uint8_t> bytes = decodeHex(kUastcAlphaKtx2Hex);
    REQUIRE_FALSE(bytes.empty());

    ImageAsset image;
    std::string error;
    REQUIRE_MESSAGE(TextureLoader::loadFromMemory(bytes.data(), bytes.size(), image, &error), error);
    CHECK(image.width == 16u);
    CHECK(image.height == 16u);
    CHECK(image.mipCount == 5u);
    CHECK(image.format == gfx::TextureFormat::BC7);
    CHECK(image.isCompressed());
    CHECK(image.pixels.size() == 368u);
    CHECK(std::any_of(image.pixels.begin(), image.pixels.end(), [](uint8_t value) { return value != 0; }));
}

TEST_CASE("glTF loader reads BasisU supercompressed KHR_texture_basisu KTX2") {
    const std::filesystem::path directory = makeFixtureDirectory();
    const std::filesystem::path bufferPath = directory / "triangle_basisu.bin";
    const std::filesystem::path imagePath = directory / "basisu.ktx2";
    const std::filesystem::path gltfPath = directory / "triangle_basisu.gltf";

    const std::vector<uint8_t> buffer = makeTriangleBuffer();
    writeFile(bufferPath, buffer.data(), buffer.size());
    const std::vector<uint8_t> ktx2 = decodeHex(kEtc1sOpaqueKtx2Hex);
    REQUIRE_FALSE(ktx2.empty());
    writeFile(imagePath, ktx2.data(), ktx2.size());

    std::string json = makeGltf(kPngDataUri, true, true, {}, "basisu.ktx2");
    const std::size_t uri = json.find("\"uri\":\"triangle.bin\"");
    REQUIRE(uri != std::string::npos);
    json.replace(uri, std::string("\"uri\":\"triangle.bin\"").size(), "\"uri\":\"triangle_basisu.bin\"");
    writeFile(gltfPath, json.data(), json.size());

    const SceneLoadResult result = SceneLoader::load(gltfPath);
    REQUIRE_MESSAGE(result.ok(), result.error);
    const render::RenderScene& scene = *result.scene;
    REQUIRE(scene.images.size() == 1u);
    CHECK(scene.images[0].format == gfx::TextureFormat::BC1);
    CHECK(scene.images[0].mipCount == 5u);
    CHECK(scene.images[0].pixels.size() == 184u);
    REQUIRE(scene.materials.size() == 1u);
    CHECK(scene.materials[0].baseColorTexture.imageIndex == 0);
}
#endif

TEST_CASE("glTF loader reads KHR_texture_basisu KTX2 textures") {
    const std::filesystem::path directory = makeFixtureDirectory();
    const std::filesystem::path bufferPath = directory / "triangle_ktx2.bin";
    const std::filesystem::path imagePath = directory / "checker.ktx2";
    const std::filesystem::path gltfPath = directory / "triangle_ktx2.gltf";

    const std::vector<uint8_t> buffer = makeTriangleBuffer();
    writeFile(bufferPath, buffer.data(), buffer.size());
    const std::vector<uint8_t> ktx2 = makeKtx2Bc1();
    REQUIRE_FALSE(ktx2.empty());
    writeFile(imagePath, ktx2.data(), ktx2.size());

    std::string json = makeGltf(kPngDataUri, true, true, {}, "checker.ktx2");
    const std::size_t uri = json.find("\"uri\":\"triangle.bin\"");
    REQUIRE(uri != std::string::npos);
    json.replace(uri, std::string("\"uri\":\"triangle.bin\"").size(), "\"uri\":\"triangle_ktx2.bin\"");
    writeFile(gltfPath, json.data(), json.size());

    const SceneLoadResult result = SceneLoader::load(gltfPath);
    REQUIRE_MESSAGE(result.ok(), result.error);
    const render::RenderScene& scene = *result.scene;
    REQUIRE(scene.images.size() == 1u);
    CHECK(scene.images[0].format == gfx::TextureFormat::BC1);
    CHECK(scene.images[0].mipCount == 4u);
    REQUIRE(scene.materials.size() == 1u);
    CHECK(scene.materials[0].baseColorTexture.imageIndex == 0);
}

TEST_CASE("glTF loader falls back when KTX2 decode fails") {
    const std::filesystem::path directory = makeFixtureDirectory();
    const std::filesystem::path bufferPath = directory / "triangle_ktx2_fallback.bin";
    const std::filesystem::path imagePath = directory / "invalid.ktx2";
    const std::filesystem::path gltfPath = directory / "triangle_ktx2_fallback.gltf";

    const std::vector<uint8_t> buffer = makeTriangleBuffer();
    writeFile(bufferPath, buffer.data(), buffer.size());
    const std::array<uint8_t, 4> invalid = {0x00, 0x01, 0x02, 0x03};
    writeFile(imagePath, invalid.data(), invalid.size());

    std::string json = makeGltf(kPngDataUri, true, true, {}, "invalid.ktx2");
    const std::size_t uri = json.find("\"uri\":\"triangle.bin\"");
    REQUIRE(uri != std::string::npos);
    json.replace(uri, std::string("\"uri\":\"triangle.bin\"").size(),
                 "\"uri\":\"triangle_ktx2_fallback.bin\"");
    writeFile(gltfPath, json.data(), json.size());

    const SceneLoadResult result = SceneLoader::load(gltfPath);
    REQUIRE_MESSAGE(result.ok(), result.error);
    const render::RenderScene& scene = *result.scene;
    REQUIRE(scene.images.size() == 1u);
    CHECK(scene.images[0].format == gfx::TextureFormat::RGBA8);
    REQUIRE(scene.materials.size() == 1u);
    CHECK(scene.materials[0].baseColorTexture.imageIndex == 0);
}

TEST_CASE("glTF sampler preserves mip filtering") {
    const std::filesystem::path directory = makeFixtureDirectory();
    const std::filesystem::path bufferPath = directory / "triangle_mip_sampler.bin";
    const std::filesystem::path gltfPath = directory / "triangle_mip_sampler.gltf";

    const std::vector<uint8_t> buffer = makeTriangleBuffer();
    writeFile(bufferPath, buffer.data(), buffer.size());
    std::string json = makeGltf(kPngDataUri, true, true, R"({"minFilter":9985,"magFilter":9729})");
    const std::size_t uri = json.find("\"uri\":\"triangle.bin\"");
    REQUIRE(uri != std::string::npos);
    json.replace(uri, std::string("\"uri\":\"triangle.bin\"").size(),
                 "\"uri\":\"triangle_mip_sampler.bin\"");
    writeFile(gltfPath, json.data(), json.size());

    const SceneLoadResult result = SceneLoader::load(gltfPath);
    REQUIRE_MESSAGE(result.ok(), result.error);
    REQUIRE(result.scene->materials.size() == 1u);
    const TextureSampler& sampler = result.scene->materials[0].baseColorTexture.sampler;
    CHECK(sampler.minFilter == TextureFilter::Linear);
    CHECK(sampler.magFilter == TextureFilter::Linear);
    CHECK(sampler.mipFilter == TextureMipFilter::Nearest);
    CHECK((TextureLoader::samplerFlags(sampler) & BGFX_SAMPLER_MIP_POINT) != 0u);
}

TEST_CASE("scene sidecar marks dynamic shadow casters") {
    const std::filesystem::path directory = makeFixtureDirectory();
    const std::filesystem::path bufferPath = directory / "triangle_dynamic_shadow.bin";
    const std::filesystem::path gltfPath = directory / "triangle_dynamic_shadow.gltf";
    const std::filesystem::path sidecarPath = directory / "triangle_dynamic_shadow.hdrp.json";

    const std::vector<uint8_t> buffer = makeTriangleBuffer();
    writeFile(bufferPath, buffer.data(), buffer.size());
    std::string json = makeGltf(kPngDataUri, true);
    const std::size_t uri = json.find("\"uri\":\"triangle.bin\"");
    REQUIRE(uri != std::string::npos);
    json.replace(uri, std::string("\"uri\":\"triangle.bin\"").size(),
                 "\"uri\":\"triangle_dynamic_shadow.bin\"");
    writeFile(gltfPath, json.data(), json.size());

    const std::string sidecar = R"({
        "objects": [
            {"name": "MeshNode", "staticShadowCaster": false}
        ]
    })";
    writeFile(sidecarPath, sidecar.data(), sidecar.size());

    const SceneLoadResult result = SceneLoader::load(gltfPath);
    REQUIRE_MESSAGE(result.ok(), result.error);
    REQUIRE(result.scene->objects.size() == 1u);
    CHECK_FALSE(result.scene->objects[0].staticShadowCaster);
}
