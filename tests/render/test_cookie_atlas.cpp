#include "render/lighting/cookie_atlas.h"

#include <doctest/doctest.h>

#include <cstdint>

using namespace hbg;
using namespace hbg::render;

namespace {

Light makeCookieLight(float sample = 1.0f) {
    Light light;
    light.cookieMode = LightCookieMode::Clamp;
    light.cookieType = LightCookieType::Spot2D;
    light.cookieSamples.fill(sample);
    return light;
}

uint8_t texel(const CookieAtlas& atlas, uint32_t mip, uint32_t x, uint32_t y) {
    const uint32_t width = atlas.size() >> mip;
    return atlas.mipData(mip)[(static_cast<std::size_t>(y) * width + x) * 4u];
}

} // namespace

TEST_CASE("cookie atlas packs legacy 8x8 tiles with atlas scale offsets") {
    Light first = makeCookieLight(0.0f);
    first.cookieSamples[0] = 0.25f;
    first.cookieSamples[1] = 0.75f;
    first.cookieSamples[8] = 0.5f;

    CookieAtlas atlas;
    atlas.configure(1024, false);
    atlas.beginFrame();

    uint32_t tile = 99;
    Vec4 firstScaleOffset;
    REQUIRE(atlas.allocate(first, &tile, &firstScaleOffset));
    CHECK(tile == 0u);
    CHECK(atlas.tileCount() == 1u);
    CHECK(atlas.nativeTileCount() == 0u);
    CHECK(atlas.mipCount() == 1u);
    CHECK(texel(atlas, 0, 0, 0) == 64u);
    CHECK(texel(atlas, 0, 1, 0) == 191u);
    CHECK(texel(atlas, 0, 0, 1) == 128u);
    CHECK(firstScaleOffset.x == doctest::Approx(8.0f / 1024.0f));
    CHECK(firstScaleOffset.z == doctest::Approx(0.0f));

    Light second = makeCookieLight(0.0f);
    second.cookieSamples[0] = 0.1f;
    Vec4 secondScaleOffset;
    REQUIRE(atlas.allocate(second, &tile, &secondScaleOffset));
    CHECK(tile == 1u);
    CHECK(texel(atlas, 0, 8, 0) == 26u);
    CHECK(secondScaleOffset.z == doctest::Approx(8.0f / 1024.0f));
}

TEST_CASE("cookie atlas native tiles use padding and generate mips") {
    Light light = makeCookieLight(1.0f);
    light.cookieNativeWidth = 64;
    light.cookieNativeHeight = 64;
    light.cookieNative.assign(64u * 64u, 1.0f);
    light.cookieNative[0] = 0.25f;

    CookieAtlas atlas;
    atlas.configure(1024, true);
    atlas.beginFrame();

    uint32_t tile = 99;
    Vec4 scaleOffset;
    REQUIRE(atlas.allocate(light, &tile, &scaleOffset));
    CHECK(tile == 0u);
    CHECK(atlas.nativeTileCount() == 1u);
    CHECK(atlas.mipCount() == 11u);
    CHECK(atlas.padding() == 2u);
    CHECK(scaleOffset.x == doctest::Approx(68.0f / 1024.0f));
    CHECK(texel(atlas, 0, 2, 2) == 64u);
    CHECK(texel(atlas, 1, 0, 0) == 64u);
    CHECK(atlas.mipData(10).size() == 4u);
}

TEST_CASE("cookie atlas required tile size includes HDRP padding and rounding") {
    CookieAtlas atlas;
    atlas.configure(1024, true);

    Light light = makeCookieLight();
    CHECK(atlas.requiredTileSize(light) == 16u);

    light.cookieNativeWidth = 64;
    light.cookieNativeHeight = 64;
    light.cookieNative.assign(64u * 64u, 1.0f);
    CHECK(atlas.requiredTileSize(light) == 128u);

    light.cookieNativeWidth = 256;
    light.cookieNativeHeight = 256;
    light.cookieNative.assign(256u * 256u, 1.0f);
    CHECK(atlas.requiredTileSize(light) == 512u);

    atlas.configure(512, true);
    CHECK(atlas.requiredTileSize(light) == 512u);
    atlas.configure(256, true);
    CHECK(atlas.requiredTileSize(light) == 0u);

    atlas.configure(1024, false);
    CHECK(atlas.requiredTileSize(light) == kCookieLegacySize);
}

TEST_CASE("cookie atlas content hash is stable across identical frames") {
    Light light = makeCookieLight(1.0f);
    light.cookieNativeWidth = 64;
    light.cookieNativeHeight = 64;
    light.cookieNative.assign(64u * 64u, 1.0f);

    CookieAtlas atlas;
    atlas.configure(1024, true);
    atlas.beginFrame(7);
    REQUIRE(atlas.allocate(light));
    const uint64_t firstHash = atlas.contentHash();

    atlas.beginFrame(7);
    REQUIRE(atlas.allocate(light));
    CHECK(atlas.contentHash() == firstHash);

    atlas.beginFrame(8);
    REQUIRE(atlas.allocate(light));
    CHECK(atlas.contentHash() != firstHash);

    Light changed = light;
    changed.cookieNativeWidth = 128;
    changed.cookieNativeHeight = 128;
    changed.cookieNative.assign(128u * 128u, 1.0f);
    atlas.beginFrame(7);
    REQUIRE(atlas.allocate(changed));
    CHECK(atlas.contentHash() != firstHash);
}

TEST_CASE("cookie atlas ignores lights without cookies and caps legacy tiles") {
    CookieAtlas atlas;
    atlas.configure(1024, false);
    atlas.beginFrame();

    Light disabled = makeCookieLight();
    disabled.cookieMode = LightCookieMode::None;
    CHECK_FALSE(atlas.allocate(disabled));

    for (uint32_t index = 0; index < kCookieAtlasTileCapacity; ++index) {
        REQUIRE(atlas.allocate(makeCookieLight()));
    }
    CHECK(atlas.tileCount() == kCookieAtlasTileCapacity);
    CHECK_FALSE(atlas.allocate(makeCookieLight()));
}
