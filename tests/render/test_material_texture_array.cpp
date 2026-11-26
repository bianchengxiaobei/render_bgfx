#include "render/material/material_texture_array.h"

#include <doctest/doctest.h>

using namespace hbg;

TEST_CASE("bindless material texture formats include compressed color and normal formats") {
    CHECK(render::isBindlessMaterialTextureFormat(gfx::TextureFormat::RGBA8));
    CHECK(render::isBindlessMaterialTextureFormat(gfx::TextureFormat::RGBA32F));
    CHECK(render::isBindlessMaterialTextureFormat(gfx::TextureFormat::BC1));
    CHECK(render::isBindlessMaterialTextureFormat(gfx::TextureFormat::BC3));
    CHECK(render::isBindlessMaterialTextureFormat(gfx::TextureFormat::BC5));
    CHECK(render::isBindlessMaterialTextureFormat(gfx::TextureFormat::BC7));

    CHECK_FALSE(render::isBindlessMaterialTextureFormat(gfx::TextureFormat::RGBA16F));
    CHECK_FALSE(render::isBindlessMaterialTextureFormat(gfx::TextureFormat::D32F));

    CHECK(render::isBindlessMaterialTextureCompressed(gfx::TextureFormat::BC7));
    CHECK_FALSE(render::isBindlessMaterialTextureCompressed(gfx::TextureFormat::RGBA8));
}

TEST_CASE("bindless material texture blocks match compressed block layout") {
    CHECK(render::bindlessMaterialTextureBlockExtent(gfx::TextureFormat::RGBA8) == 1);
    CHECK(render::bindlessMaterialTextureBlockExtent(gfx::TextureFormat::RGBA32F) == 1);
    CHECK(render::bindlessMaterialTextureBlockExtent(gfx::TextureFormat::BC1) == 4);
    CHECK(render::bindlessMaterialTextureBlockExtent(gfx::TextureFormat::BC3) == 4);
    CHECK(render::bindlessMaterialTextureBlockExtent(gfx::TextureFormat::BC5) == 4);
    CHECK(render::bindlessMaterialTextureBlockExtent(gfx::TextureFormat::BC7) == 4);

    CHECK(render::bindlessMaterialTextureBlockBytes(gfx::TextureFormat::BC1) == 8);
    CHECK(render::bindlessMaterialTextureBlockBytes(gfx::TextureFormat::BC5) == 8);
    CHECK(render::bindlessMaterialTextureBlockBytes(gfx::TextureFormat::BC3) == 16);
    CHECK(render::bindlessMaterialTextureBlockBytes(gfx::TextureFormat::BC7) == 16);
    CHECK(render::bindlessMaterialTextureBlockBytes(gfx::TextureFormat::RGBA8) == 0);
}

TEST_CASE("bindless heterogeneous texture arrays pad compressed blocks by one block per edge") {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t offsetX = 0;
    uint32_t offsetY = 0;
    REQUIRE(render::bindlessMaterialTextureHeterogeneousLayout(
        128, 128, gfx::TextureFormat::RGBA8, width, height, offsetX, offsetY));
    CHECK(width == 256);
    CHECK(height == 256);
    CHECK(offsetX == 1);
    CHECK(offsetY == 1);

    REQUIRE(render::bindlessMaterialTextureHeterogeneousLayout(
        128, 64, gfx::TextureFormat::BC7, width, height, offsetX, offsetY));
    CHECK(width == 256);
    CHECK(height == 128);
    CHECK(offsetX == 4);
    CHECK(offsetY == 4);

    REQUIRE(render::bindlessMaterialTextureHeterogeneousLayout(
        4, 4, gfx::TextureFormat::BC1, width, height, offsetX, offsetY));
    CHECK(width == 16);
    CHECK(height == 16);
    CHECK(offsetX == 4);
    CHECK(offsetY == 4);

    CHECK_FALSE(render::bindlessMaterialTextureHeterogeneousLayout(
        0, 64, gfx::TextureFormat::BC7, width, height, offsetX, offsetY));
    CHECK_FALSE(render::bindlessMaterialTextureHeterogeneousLayout(
        65535, 64, gfx::TextureFormat::BC7, width, height, offsetX, offsetY));
    CHECK_FALSE(render::bindlessMaterialTextureHeterogeneousLayout(
        64, 64, gfx::TextureFormat::RGBA16F, width, height, offsetX, offsetY));
}

TEST_CASE("bindless heterogeneous multi-mip arrays keep each mip in the padded array") {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t offsetX = 0;
    uint32_t offsetY = 0;
    REQUIRE(render::bindlessMaterialTextureLayeredLayout(
        128, 64, 7, gfx::TextureFormat::BC1, width, height, offsetX, offsetY));
    CHECK(width == 128);
    CHECK(height == 64);
    CHECK(offsetX == 0);
    CHECK(offsetY == 0);

    REQUIRE(render::bindlessMaterialTextureLayeredLayout(
        128, 64, 7, gfx::TextureFormat::RGBA8, width, height, offsetX, offsetY));
    CHECK(width == 128);
    CHECK(height == 64);
    CHECK(offsetX == 0);
    CHECK(offsetY == 0);

    REQUIRE(render::bindlessMaterialTextureLayeredLayout(
        128, 64, 1, gfx::TextureFormat::BC7, width, height, offsetX, offsetY));
    CHECK(width == 256);
    CHECK(height == 128);
    CHECK(offsetX == 4);
    CHECK(offsetY == 4);

    CHECK_FALSE(render::bindlessMaterialTextureLayeredLayout(
        0, 64, 7, gfx::TextureFormat::BC1, width, height, offsetX, offsetY));
    CHECK_FALSE(render::bindlessMaterialTextureLayeredLayout(
        65535, 64, 7, gfx::TextureFormat::BC1, width, height, offsetX, offsetY));
}

TEST_CASE("bindless material texture mip offsets follow the compressed block layout") {
    uint32_t offset = 0;
    REQUIRE(render::bindlessMaterialTextureMipOffset(128, 128, 8, 0, gfx::TextureFormat::BC7, offset));
    CHECK(offset == 0);
    REQUIRE(render::bindlessMaterialTextureMipOffset(128, 128, 8, 1, gfx::TextureFormat::BC7, offset));
    CHECK(offset == 16'384);
    REQUIRE(render::bindlessMaterialTextureMipOffset(128, 128, 8, 2, gfx::TextureFormat::BC7, offset));
    CHECK(offset == 20'480);
    REQUIRE(render::bindlessMaterialTextureMipOffset(128, 128, 8, 7, gfx::TextureFormat::BC7, offset));
    CHECK(offset == 21'856);
    REQUIRE(render::bindlessMaterialTextureMipOffset(64, 64, 7, 6, gfx::TextureFormat::BC1, offset));
    CHECK(offset == 2'736);

    CHECK_FALSE(render::bindlessMaterialTextureMipOffset(128, 128, 8, 8, gfx::TextureFormat::BC7, offset));
    CHECK_FALSE(render::bindlessMaterialTextureMipOffset(0, 128, 8, 0, gfx::TextureFormat::BC7, offset));
}

TEST_CASE("bindless material texture mip offsets support uncompressed chains") {
    uint32_t offset = 0;
    REQUIRE(render::bindlessMaterialTextureMipOffset(8, 8, 4, 0, gfx::TextureFormat::RGBA8, offset));
    CHECK(offset == 0);
    REQUIRE(render::bindlessMaterialTextureMipOffset(8, 8, 4, 1, gfx::TextureFormat::RGBA8, offset));
    CHECK(offset == 256);
    REQUIRE(render::bindlessMaterialTextureMipOffset(8, 8, 4, 2, gfx::TextureFormat::RGBA8, offset));
    CHECK(offset == 320);
    REQUIRE(render::bindlessMaterialTextureMipOffset(8, 8, 4, 3, gfx::TextureFormat::RGBA8, offset));
    CHECK(offset == 336);
    REQUIRE(render::bindlessMaterialTextureMipOffset(128, 64, 8, 7, gfx::TextureFormat::RGBA8, offset));
    CHECK(offset == 43'688);

    CHECK_FALSE(render::bindlessMaterialTextureMipOffset(8, 8, 4, 4, gfx::TextureFormat::RGBA8, offset));
}

TEST_CASE("bindless material texture layer size includes the full mip chain") {
    uint32_t bytes = 0;
    REQUIRE(render::bindlessMaterialTextureLayerBytes(4, 4, 1, gfx::TextureFormat::RGBA8, bytes));
    CHECK(bytes == 64);

    REQUIRE(render::bindlessMaterialTextureLayerBytes(8, 8, 4, gfx::TextureFormat::RGBA8, bytes));
    CHECK(bytes == 340);

    REQUIRE(render::bindlessMaterialTextureLayerBytes(128, 128, 1, gfx::TextureFormat::BC7, bytes));
    CHECK(bytes == 16 * 1024);

    REQUIRE(render::bindlessMaterialTextureLayerBytes(128, 128, 8, gfx::TextureFormat::BC7, bytes));
    CHECK(bytes == 21'872);

    REQUIRE(render::bindlessMaterialTextureLayerBytes(128, 128, 8, gfx::TextureFormat::BC1, bytes));
    CHECK(bytes == 10'936);

    REQUIRE(render::bindlessMaterialTextureLayerBytes(256, 256, 1, gfx::TextureFormat::BC7, bytes));
    CHECK(bytes == 65'536);

    REQUIRE(render::bindlessMaterialTextureLayerBytes(16, 16, 1, gfx::TextureFormat::BC1, bytes));
    CHECK(bytes == 128);

    CHECK_FALSE(render::bindlessMaterialTextureLayerBytes(0, 128, 1, gfx::TextureFormat::BC7, bytes));
    CHECK_FALSE(render::bindlessMaterialTextureLayerBytes(128, 128, 9, gfx::TextureFormat::BC7, bytes));
    CHECK_FALSE(render::bindlessMaterialTextureLayerBytes(128, 128, 1, gfx::TextureFormat::RGBA16F, bytes));
}
