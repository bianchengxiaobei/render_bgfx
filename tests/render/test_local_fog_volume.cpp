#include "render/atmosphere/local_fog_volume.h"

#include <doctest/doctest.h>

#include <cmath>
#include <limits>

using namespace hbg;
using namespace hbg::render;

TEST_CASE("local fog volume defaults match HDRP artist parameters") {
    const LocalFogVolume volume;
    CHECK(volume.enabled);
    CHECK(volume.shape == LocalFogShape::Box);
    CHECK(volume.size == Vec3(1.0f, 1.0f, 1.0f));
    CHECK(volume.albedo == Vec3(1.0f, 1.0f, 1.0f));
    CHECK(volume.meanFreePath == doctest::Approx(10.0f));
    CHECK(volume.positiveFade == Vec3(0.1f, 0.1f, 0.1f));
    CHECK(volume.negativeFade == Vec3(0.1f, 0.1f, 0.1f));
    CHECK(volume.falloff == LocalFogFalloff::Linear);
    CHECK(volume.maskPlane == LocalFogMaskPlane::XZ);
    CHECK(volume.maskImage == -1);
    CHECK(volume.distanceFadeLength() == doctest::Approx(0.00001526f));
    CHECK(volume.extinction() == doctest::Approx(0.1f));
    CHECK(volume.scattering().x == doctest::Approx(0.1f));
}

TEST_CASE("local fog volume sanitizes invalid parameters") {
    LocalFogVolume volume;
    volume.size = Vec3(-2.0f, 0.0f, std::numeric_limits<float>::quiet_NaN());
    volume.positiveFade = Vec3(-1.0f, 2.0f, 3.0f);
    volume.negativeFade = Vec3(-1.0f, 2.0f, 3.0f);
    volume.meanFreePath = 0.0f;
    volume.anisotropy = 2.0f;
    volume.distanceFadeStart = 10.0f;
    volume.distanceFadeEnd = 5.0f;
    volume.maskIntensity = 2.0f;
    volume.maskTiling = Vec3(0.0f, -2.0f, 3.0f);

    sanitizeLocalFogVolume(volume);
    CHECK(volume.size == Vec3(2.0f, 1.0e-3f, 1.0f));
    CHECK(volume.positiveFade == Vec3(0.0f, 2.0f, 3.0f));
    CHECK(volume.negativeFade == Vec3(0.0f, 2.0f, 3.0f));
    CHECK(volume.meanFreePath == doctest::Approx(0.05f));
    CHECK(volume.anisotropy == doctest::Approx(0.99f));
    CHECK(volume.distanceFadeEnd == doctest::Approx(10.0f + 0.00001526f));
    CHECK(volume.maskTiling == Vec3(0.001f, 2.0f, 3.0f));
    CHECK(volume.maskIntensity == doctest::Approx(0.0f));
}

TEST_CASE("local fog box falloff matches HDRP OBB face and distance fade") {
    LocalFogVolume volume;
    volume.center = Vec3(0.0f, 0.0f, 0.0f);
    volume.size = Vec3(2.0f, 2.0f, 2.0f);
    volume.positiveFade = Vec3(0.5f, 0.5f, 0.5f);
    volume.negativeFade = Vec3(0.5f, 0.5f, 0.5f);
    volume.distanceFadeStart = 10.0f;
    volume.distanceFadeEnd = 20.0f;

    CHECK(localFogFalloff(volume, Vec3(0.0f, 0.0f, 0.0f), 10.0f) == doctest::Approx(1.0f));
    CHECK(localFogFalloff(volume, Vec3(0.75f, 0.0f, 0.0f), 10.0f) == doctest::Approx(0.25f));
    CHECK(localFogFalloff(volume, Vec3(0.0f, 0.0f, 0.0f), 15.0f) == doctest::Approx(0.5f));
    CHECK(localFogFalloff(volume, Vec3(0.0f, 0.0f, 0.0f), 20.0f) == doctest::Approx(0.0f));
    CHECK(localFogFalloff(volume, Vec3(1.01f, 0.0f, 0.0f), 10.0f) == doctest::Approx(0.0f));

    volume.invertFade = true;
    CHECK(localFogFalloff(volume, Vec3(0.0f, 0.0f, 0.0f), 10.0f) == doctest::Approx(0.0f));
    CHECK(localFogFalloff(volume, Vec3(0.75f, 0.0f, 0.0f), 10.0f) == doctest::Approx(0.75f));
    CHECK(localFogFalloff(volume, Vec3(1.01f, 0.0f, 0.0f), 10.0f) == doctest::Approx(0.0f));
}

TEST_CASE("local fog sphere falloff uses normalized ellipsoid radius") {
    LocalFogVolume volume;
    volume.shape = LocalFogShape::Sphere;
    volume.size = Vec3(2.0f, 2.0f, 2.0f);
    volume.positiveFade = Vec3(0.5f, 0.5f, 0.5f);

    CHECK(localFogFalloff(volume, Vec3(0.0f, 0.0f, 0.0f), 0.0f) == doctest::Approx(1.0f));
    CHECK(localFogFalloff(volume, Vec3(0.5f, 0.0f, 0.0f), 0.0f) == doctest::Approx(1.0f));
    CHECK(localFogFalloff(volume, Vec3(0.75f, 0.0f, 0.0f), 0.0f) == doctest::Approx(0.5f));
    CHECK(localFogFalloff(volume, Vec3(1.01f, 0.0f, 0.0f), 0.0f) == doctest::Approx(0.0f));

    volume.size = Vec3(4.0f, 4.0f, 4.0f);
    CHECK(localFogFalloff(volume, Vec3(1.5f, 0.0f, 0.0f), 0.0f) == doctest::Approx(0.5f));
}

TEST_CASE("local fog exponential falloff matches HDRP power curve") {
    LocalFogVolume volume;
    volume.size = Vec3(2.0f, 2.0f, 2.0f);
    volume.positiveFade = Vec3(0.5f, 0.5f, 0.5f);
    volume.negativeFade = Vec3(0.5f, 0.5f, 0.5f);
    volume.falloff = LocalFogFalloff::Exponential;

    CHECK(localFogFalloff(volume, Vec3(0.75f, 0.0f, 0.0f), 0.0f) ==
          doctest::Approx(std::pow(0.25f, 2.2f)));
}
