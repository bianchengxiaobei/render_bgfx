#include "gfx/buffer.h"
#include "gfx/framebuffer_cache.h"
#include "gfx/texture.h"
#include "gfx/uniform.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <unordered_set>

using namespace hbg::gfx;

TEST_CASE("texture format mapping is stable") {
    CHECK(toBgfx(TextureFormat::RGBA8) == bgfx::TextureFormat::RGBA8);
    CHECK(toBgfx(TextureFormat::RG11B10F) == bgfx::TextureFormat::RG11B10F);
    CHECK(toBgfx(TextureFormat::RG16F) == bgfx::TextureFormat::RG16F);
    CHECK(toBgfx(TextureFormat::RG32F) == bgfx::TextureFormat::RG32F);
    CHECK(toBgfx(TextureFormat::R32U) == bgfx::TextureFormat::R32U);
    CHECK(toBgfx(TextureFormat::D24S8) == bgfx::TextureFormat::D24S8);
    CHECK(toBgfx(TextureFormat::Count) == bgfx::TextureFormat::Count);

    CHECK(isDepthFormat(TextureFormat::D16));
    CHECK(isDepthFormat(TextureFormat::D24S8));
    CHECK(isDepthFormat(TextureFormat::D32F));
    CHECK_FALSE(isDepthFormat(TextureFormat::RGBA8));
    CHECK_FALSE(isDepthFormat(TextureFormat::RG11B10F));
}

TEST_CASE("uniform type mapping is stable") {
    CHECK(toBgfx(UniformType::Sampler) == bgfx::UniformType::Sampler);
    CHECK(toBgfx(UniformType::Vec4) == bgfx::UniformType::Vec4);
    CHECK(toBgfx(UniformType::Mat3) == bgfx::UniformType::Mat3);
    CHECK(toBgfx(UniformType::Mat4) == bgfx::UniformType::Mat4);
}

TEST_CASE("vertex layouts produce expected strides") {
    const VertexLayout positionColor = VertexLayout::positionColor();
    CHECK(positionColor.ready());
    CHECK(positionColor.stride() == 16);

    const VertexLayout positionNormalUv = VertexLayout::positionNormalUv();
    CHECK(positionNormalUv.ready());
    CHECK(positionNormalUv.stride() == 32);
}

TEST_CASE("framebuffer keys compare and hash consistently") {
    const TextureHandle first{3, 1};
    const TextureHandle second{7, 2};

    const FrameBufferKey a{{first, second}};
    const FrameBufferKey b{{first, second}};
    const FrameBufferKey c{{second, first}};
    const FrameBufferKey withMip{{first, second}, {0, 2}};

    CHECK(a == b);
    CHECK_FALSE(a == c);
    CHECK_FALSE(a == withMip);

    const FrameBufferKeyHash hash;
    CHECK(hash(a) == hash(b));
    CHECK(hash(a) != hash(withMip));

    std::unordered_set<FrameBufferKey, FrameBufferKeyHash> keys;
    keys.insert(a);
    keys.insert(b);
    keys.insert(c);
    keys.insert(withMip);
    CHECK(keys.size() == 3);

    FrameBufferCache cache;
    const FrameBufferHandle firstFrameBuffer{4, 1};
    const FrameBufferHandle secondFrameBuffer{5, 1};
    cache.insert(a, firstFrameBuffer);
    cache.insert(c, secondFrameBuffer);
    const std::vector<FrameBufferHandle> containingFirst = cache.findContaining(first);
    REQUIRE(containingFirst.size() == 2);
    CHECK(std::find(containingFirst.begin(), containingFirst.end(), firstFrameBuffer) != containingFirst.end());
    const std::vector<FrameBufferHandle> containingSecond = cache.findContaining(second);
    REQUIRE(containingSecond.size() == 2);
    CHECK(std::find(containingSecond.begin(), containingSecond.end(), secondFrameBuffer) != containingSecond.end());
}
