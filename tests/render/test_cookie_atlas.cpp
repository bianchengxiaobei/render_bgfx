#include "render/lighting/cookie_atlas.h"

#include <doctest/doctest.h>

#include <vector>

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

} // namespace

TEST_CASE("cookie atlas packs 8x8 tiles into the light-data atlas region") {
    Light first = makeCookieLight(0.0f);
    first.cookieSamples[0] = 0.25f;
    first.cookieSamples[1] = 0.75f;
    first.cookieSamples[8] = 0.5f;

    CookieAtlas atlas;
    atlas.beginFrame();

    uint32_t tile = 99;
    REQUIRE(atlas.allocate(first, &tile));
    CHECK(tile == 0u);
    CHECK(atlas.tileCount() == 1u);
    CHECK(atlas.texels()[0].x == doctest::Approx(0.25f));
    CHECK(atlas.texels()[1].x == doctest::Approx(0.75f));
    CHECK(atlas.texels()[kCookieAtlasWidth].x == doctest::Approx(0.5f));

    Light second = makeCookieLight(0.0f);
    second.cookieSamples[0] = 0.1f;
    REQUIRE(atlas.allocate(second, &tile));
    CHECK(tile == 1u);
    CHECK(atlas.texels()[kCookieTileSize].x == doctest::Approx(0.1f));

    std::vector<Vec4> destination(kLightDataTextureRows * kLightDataTextureWidth);
    atlas.copyTo(destination, kLightDataTextureWidth, kCookieAtlasOriginX, 0);
    CHECK(destination[kCookieAtlasOriginX].x == doctest::Approx(0.25f));
    CHECK(destination[kCookieAtlasOriginX + kCookieTileSize].x == doctest::Approx(0.1f));
}

TEST_CASE("cookie atlas ignores lights without cookies and caps at 128 tiles") {
    CookieAtlas atlas;
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
