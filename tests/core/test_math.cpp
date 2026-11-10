#include "core/math.h"

#include <doctest/doctest.h>

#include <cmath>

using namespace hbg;

namespace {

void checkMatrix(const Mat4& actual, const Mat4& expected) {
    for (std::size_t index = 0; index < 16; ++index) {
        CHECK(actual[index] == doctest::Approx(expected[index]).epsilon(1.0e-5));
    }
}

} // namespace

TEST_CASE("lookAtRH produces the expected right-handed view") {
    const Mat4 view = lookAtRH(Vec3(0.0f, 0.0f, 3.0f), Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f));
    const Vec3 origin = transformPoint(view, Vec3(0.0f, 0.0f, 0.0f));
    CHECK(origin.x == doctest::Approx(0.0f));
    CHECK(origin.y == doctest::Approx(0.0f));
    CHECK(origin.z == doctest::Approx(-3.0f));

    const Vec3 eye = transformPoint(view, Vec3(0.0f, 0.0f, 3.0f));
    CHECK(eye.x == doctest::Approx(0.0f));
    CHECK(eye.y == doctest::Approx(0.0f));
    CHECK(eye.z == doctest::Approx(0.0f));
}

TEST_CASE("perspectiveRH maps the near center into the depth range") {
    const Mat4 projection = perspectiveRH(1.57079632679f, 1.0f, 0.1f, 100.0f, false);
    const Vec3 viewSpace(0.0f, 0.0f, -3.0f);
    const float clipX = projection[0] * viewSpace.x + projection[4] * viewSpace.y +
                        projection[8] * viewSpace.z + projection[12];
    const float clipY = projection[1] * viewSpace.x + projection[5] * viewSpace.y +
                        projection[9] * viewSpace.z + projection[13];
    const float clipZ = projection[2] * viewSpace.x + projection[6] * viewSpace.y +
                        projection[10] * viewSpace.z + projection[14];
    const float clipW = projection[3] * viewSpace.x + projection[7] * viewSpace.y +
                        projection[11] * viewSpace.z + projection[15];

    CHECK(clipX == doctest::Approx(0.0f));
    CHECK(clipY == doctest::Approx(0.0f));
    CHECK(clipW == doctest::Approx(3.0f));
    CHECK(clipZ / clipW == doctest::Approx(0.9676f).epsilon(1.0e-3));
}

TEST_CASE("Mat4 inverse round-trips a view matrix") {
    const Mat4 view = lookAtRH(Vec3(2.0f, 3.0f, 4.0f), Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f));
    checkMatrix(inverse(view) * view, Mat4::identity());
}
