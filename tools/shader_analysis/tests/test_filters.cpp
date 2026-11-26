#include "hbg/analysis/filters.h"

#include <doctest/doctest.h>

#include <string>
#include <vector>

using namespace hbg::analysis;

TEST_CASE("glob matching supports stars and question marks") {
    CHECK(globMatch("forward", "forward"));
    CHECK(globMatch("forward*", "forward_buffer"));
    CHECK(globMatch("*Lighting", "deferredLighting"));
    CHECK(globMatch("deferred?ight*", "deferredLighting_variant"));
    CHECK_FALSE(globMatch("forward", "forward_buffer"));
    CHECK_FALSE(globMatch("forward?", "forward"));
}

TEST_CASE("pass filters mirror include and exclude parsing") {
    const PassFilter include = PassFilter::parse("+forward,shadow*");
    CHECK(include.matches("forward", "forward.fs", "fragment"));
    CHECK(include.matches("shadowcaster", "shadowcaster.fs", "fragment"));
    CHECK_FALSE(include.matches("deferred_lighting", "deferred_lighting.fs", "fragment"));

    const PassFilter exclude = PassFilter::parse("-forward");
    CHECK_FALSE(exclude.matches("forward", "forward.fs", "fragment"));
    CHECK(exclude.matches("deferred_lighting", "deferred_lighting.fs", "fragment"));

    const PassFilter bare = PassFilter::parse("forward");
    CHECK(bare.matches("forward", "forward.fs", "fragment"));
    CHECK_FALSE(bare.matches("forward_buffer", "forward_buffer.fs", "fragment"));

    const PassFilter empty = PassFilter::parse("");
    CHECK(empty.empty());
    CHECK(empty.matches("anything", "anything.fs", "fragment"));

    CHECK_FALSE(PassFilter::parse("+").matches("anything", "anything.fs", "fragment"));
    CHECK(PassFilter::parse("-").matches("anything", "anything.fs", "fragment"));
}

TEST_CASE("pass filters can match full stem and stage") {
    CHECK(PassFilter::parse("+forward.fs").matches("forward", "forward.fs", "fragment"));
    CHECK(PassFilter::parse("+fragment").matches("forward", "forward.fs", "fragment"));
    CHECK_FALSE(PassFilter::parse("+vertex").matches("forward", "forward.fs", "fragment"));
}

TEST_CASE("keyword filters use Unity OR-of-AND semantics") {
    const KeywordFilter filter = KeywordFilter::parse("A&B|C");
    CHECK_FALSE(filter.empty());
    CHECK(filter.matches({"A=1", "B=2"}));
    CHECK(filter.matches({"C=1"}));
    CHECK(filter.matches({"A=1", "B=2", "C=3"}));
    CHECK_FALSE(filter.matches({"A=1"}));
    CHECK_FALSE(KeywordFilter::parse("A&&B").matches({"A=1", "B=2"}));
    CHECK_FALSE(KeywordFilter::parse("|").matches({"A=1"}));
    CHECK(KeywordFilter::parse("").matches({"A=1"}));
}

TEST_CASE("define filters match names or exact values") {
    const DefineFilter byName = DefineFilter::parse({"HAS_NORMAL_MAP"});
    CHECK(byName.matches({"HAS_NORMAL_MAP=1"}));
    CHECK_FALSE(byName.matches({"HAS_MASK_MAP=1"}));

    const DefineFilter byValue = DefineFilter::parse({"HAS_NORMAL_MAP=1"});
    CHECK(byValue.matches({"HAS_NORMAL_MAP=1"}));
    CHECK_FALSE(byValue.matches({"HAS_NORMAL_MAP=0"}));

    CHECK(DefineFilter::parse({}).matches({"ANYTHING=1"}));
}
