#include "render/scene/decal.h"

#include <doctest/doctest.h>

using namespace hbg;
using namespace hbg::render;

TEST_CASE("decal box transform follows HDRP local axes") {
    Decal decal;
    decal.position = Vec3(1.0f, 2.0f, 3.0f);
    decal.size = Vec3(2.0f, 4.0f, 6.0f);

    const Mat4 toWorld = decalToWorld(decal);
    CHECK(transformPoint(toWorld, Vec3(0.0f, 0.0f, 0.0f)) == Vec3(1.0f, 2.0f, 3.0f));
    CHECK(transformPoint(toWorld, Vec3(0.5f, 0.0f, 0.0f)) == Vec3(2.0f, 2.0f, 3.0f));
    CHECK(transformPoint(toWorld, Vec3(0.0f, 0.5f, 0.0f)) == Vec3(1.0f, 5.0f, 3.0f));
    CHECK(transformPoint(toWorld, Vec3(0.0f, 0.0f, 0.5f)) == Vec3(1.0f, 2.0f, 5.0f));

    CHECK(decalContainsWorld(decal, Vec3(1.0f, 2.0f, 3.0f)));
    CHECK(decalContainsWorld(decal, Vec3(1.9f, 3.9f, 4.9f)));
    CHECK_FALSE(decalContainsWorld(decal, Vec3(2.1f, 2.0f, 3.0f)));
    CHECK_FALSE(decalContainsWorld(decal, Vec3(1.0f, 5.1f, 3.0f)));
}

TEST_CASE("decal rotation turns the projector depth axis") {
    Decal decal;
    decal.rotation = Vec3(90.0f, 0.0f, 0.0f);
    const Vec3 forward = decalForwardWorld(decal);
    CHECK(forward.x == doctest::Approx(0.0f));
    CHECK(forward.y == doctest::Approx(0.0f));
    CHECK(forward.z == doctest::Approx(1.0f));

    const Mat4 toWorld = decalToWorld(decal);
    CHECK(transformPoint(toWorld, Vec3(0.0f, 0.0f, 0.5f)).y == doctest::Approx(-0.5f));
}

TEST_CASE("decal angle fade is smooth and disabled at zero") {
    CHECK(decalAngleFade(0.0f, 0.0f) == doctest::Approx(1.0f));
    CHECK(decalAngleFade(0.0f, 60.0f) == doctest::Approx(0.0f));
    CHECK(decalAngleFade(1.0f, 60.0f) == doctest::Approx(1.0f));
    CHECK(decalAngleFade(0.5f, 90.0f) == doctest::Approx(0.5f));
    CHECK(decalAngleFade(-1.0f, 90.0f) == doctest::Approx(1.0f));
    CHECK(decalAngleFade(1.0f, 180.0f) == doctest::Approx(1.0f));
}
