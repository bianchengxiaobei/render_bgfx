#include "render/scene/reflection_probe.h"

#include <doctest/doctest.h>

#include <array>
#include <vector>

using namespace hbg;
using namespace hbg::render;

TEST_CASE("reflection probe box transform follows local axes") {
    ReflectionProbe probe;
    probe.position = Vec3(1.0f, 2.0f, 3.0f);
    probe.boxExtents = Vec3(2.0f, 3.0f, 4.0f);

    const Mat4 toWorld = probeToWorld(probe);
    CHECK(transformPoint(toWorld, Vec3(0.0f, 0.0f, 0.0f)) == Vec3(1.0f, 2.0f, 3.0f));
    CHECK(transformPoint(toWorld, Vec3(1.0f, 0.0f, 0.0f)) == Vec3(2.0f, 2.0f, 3.0f));
    CHECK(transformPoint(toWorld, Vec3(0.0f, 1.0f, 0.0f)) == Vec3(1.0f, 3.0f, 3.0f));
    CHECK(transformPoint(toWorld, Vec3(0.0f, 0.0f, 1.0f)) == Vec3(1.0f, 2.0f, 4.0f));

    CHECK(probeContainsWorld(probe, Vec3(1.0f, 2.0f, 3.0f)));
    CHECK(probeContainsWorld(probe, Vec3(2.9f, 4.9f, 6.9f)));
    CHECK_FALSE(probeContainsWorld(probe, Vec3(3.1f, 2.0f, 3.0f)));
    CHECK_FALSE(probeContainsWorld(probe, Vec3(1.0f, 5.1f, 3.0f)));
}

TEST_CASE("reflection probe rotation turns the local box axes") {
    ReflectionProbe probe;
    probe.rotation = Vec3(0.0f, 90.0f, 0.0f);
    probe.boxExtents = Vec3(2.0f, 1.0f, 1.0f);

    const Mat4 toWorld = probeToWorld(probe);
    CHECK(transformPoint(toWorld, Vec3(0.0f, 0.0f, 1.0f)).x == doctest::Approx(1.0f));
    CHECK(transformPoint(toWorld, Vec3(0.0f, 0.0f, 1.0f)).z == doctest::Approx(0.0f));
    CHECK(probeContainsWorld(probe, Vec3(0.9f, 0.0f, 0.0f)));
    CHECK_FALSE(probeContainsWorld(probe, Vec3(1.1f, 0.0f, 0.0f)));
    CHECK(probeContainsWorld(probe, Vec3(0.0f, 0.0f, 1.9f)));
    CHECK_FALSE(probeContainsWorld(probe, Vec3(0.0f, 0.0f, 2.1f)));
}

TEST_CASE("reflection probe influence fades at the box edges") {
    ReflectionProbe probe;
    probe.boxExtents = Vec3(1.0f, 1.0f, 1.0f);
    probe.blendDistance = 0.5f;

    CHECK(probeInfluenceWeight(probe, Vec3(0.0f, 0.0f, 0.0f)) == doctest::Approx(1.0f));
    CHECK(probeInfluenceWeight(probe, Vec3(0.75f, 0.0f, 0.0f)) == doctest::Approx(0.5f));
    CHECK(probeInfluenceWeight(probe, Vec3(1.0f, 0.0f, 0.0f)) == doctest::Approx(0.0f));
    CHECK(probeInfluenceWeight(probe, Vec3(1.1f, 0.0f, 0.0f)) == doctest::Approx(0.0f));
}

TEST_CASE("reflection probe selection picks the nearest two influencing probes") {
    std::vector<ReflectionProbe> probes(4);
    for (ReflectionProbe& probe : probes) {
        probe.boxExtents = Vec3(2.0f, 2.0f, 2.0f);
        probe.image = 0;
    }
    probes[0].position = Vec3(0.5f, 0.0f, 0.0f);
    probes[1].position = Vec3(-0.5f, 0.0f, 0.0f);
    probes[2].position = Vec3(0.0f, 0.0f, 1.5f);
    probes[3].position = Vec3(0.0f, 0.0f, 0.0f);
    probes[3].enabled = false;

    const auto selected = selectReflectionProbes(probes, Vec3(0.0f, 0.0f, 0.0f));
    CHECK(selected[0] == 0);
    CHECK(selected[1] == 1);
}

TEST_CASE("reflection probe selection ignores probes beyond the supported count") {
    std::vector<ReflectionProbe> probes(5);
    for (std::size_t index = 0; index < probes.size(); ++index) {
        probes[index].boxExtents = Vec3(10.0f, 10.0f, 10.0f);
        probes[index].image = 0;
        probes[index].position = Vec3(static_cast<float>(index + 1), 0.0f, 0.0f);
    }
    probes[4].position = Vec3(0.1f, 0.0f, 0.0f);

    const auto selected = selectReflectionProbes(probes, Vec3(0.0f, 0.0f, 0.0f));
    CHECK(selected[0] == 0);
    CHECK(selected[1] == 1);
}

TEST_CASE("reflection probe selection returns no probe outside influence") {
    std::vector<ReflectionProbe> probes(1);
    probes[0].boxExtents = Vec3(1.0f, 1.0f, 1.0f);
    probes[0].image = 0;
    probes[0].position = Vec3(10.0f, 0.0f, 0.0f);

    const auto selected = selectReflectionProbes(probes, Vec3(0.0f, 0.0f, 0.0f));
    CHECK(selected[0] == -1);
    CHECK(selected[1] == -1);
}
