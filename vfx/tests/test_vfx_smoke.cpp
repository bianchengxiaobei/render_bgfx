#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "vfx/handles.h"
#include "vfx/types.h"
#include "vfx/version.h"

TEST_CASE("vfx subproject is available") {
    CHECK(vfx::version() != nullptr);
    CHECK(vfx::kInvalidIndex == 0xffffffffu);
    CHECK(vfx::kDefaultParticleCapacity <= vfx::kMaxParticleCapacity);
}

TEST_CASE("vfx handles distinguish valid and invalid") {
    const vfx::EffectHandle invalid{};
    CHECK_FALSE(invalid.valid());

    vfx::EffectHandle valid{};
    valid.index = 1;
    CHECK(valid.valid());
    CHECK(valid != invalid);
}
