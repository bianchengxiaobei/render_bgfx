#include "core/handle.h"
#include "core/pool.h"

#include <doctest/doctest.h>

#include <string>
#include <utility>

using namespace hbg;

namespace {

struct TestTag {};

struct Value {
    Value(int number_, std::string text_) : number(number_), text(std::move(text_)) {}

    int number = 0;
    std::string text;
};

} // namespace

TEST_CASE("Handle reports validity") {
    const Handle<TestTag> invalid;
    CHECK_FALSE(invalid.valid());
    CHECK_FALSE(static_cast<bool>(invalid));

    const Handle<TestTag> valid{0, 1};
    CHECK(valid.valid());
    CHECK(static_cast<bool>(valid));
}

TEST_CASE("Pool adds, gets and removes values") {
    Pool<TestTag, Value> pool;
    const Handle<TestTag> first = pool.emplace(7, "seven");
    const Handle<TestTag> second = pool.emplace(9, "nine");

    REQUIRE(pool.size() == 2);

    Value* firstValue = pool.get(first);
    REQUIRE(firstValue != nullptr);
    CHECK(firstValue->number == 7);
    CHECK(firstValue->text == "seven");

    CHECK(pool.remove(first));
    CHECK(pool.size() == 1);
    CHECK(pool.get(first) == nullptr);
    CHECK_FALSE(pool.remove(first));
    CHECK(pool.get(second) != nullptr);
}

TEST_CASE("Pool reuses indices with a new generation") {
    Pool<TestTag, Value> pool;
    const Handle<TestTag> first = pool.emplace(1, "one");

    REQUIRE(pool.remove(first));

    const Handle<TestTag> second = pool.emplace(2, "two");
    CHECK(second.index == first.index);
    CHECK(second.generation != first.generation);
    CHECK(pool.get(first) == nullptr);
    CHECK(pool.get(second) != nullptr);
}

TEST_CASE("Pool rejects stale and out-of-range handles") {
    Pool<TestTag, Value> pool;
    const Handle<TestTag> handle = pool.emplace(1, "one");

    CHECK(pool.get(Handle<TestTag>{0, handle.generation + 1}) == nullptr);
    CHECK(pool.get(Handle<TestTag>{999, 1}) == nullptr);
    CHECK(pool.get(Handle<TestTag>{}) == nullptr);
}

TEST_CASE("Pool iterates live values") {
    Pool<TestTag, Value> pool;
    const Handle<TestTag> removed = pool.emplace(1, "one");
    (void)pool.emplace(2, "two");
    (void)pool.emplace(3, "three");
    REQUIRE(pool.remove(removed));

    int count = 0;
    int sum = 0;
    pool.forEach([&](Handle<TestTag>, const Value& value) {
        ++count;
        sum += value.number;
    });

    CHECK(count == 2);
    CHECK(sum == 5);
}

TEST_CASE("Pool grows beyond its initial capacity") {
    Pool<TestTag, Value> pool;
    for (int index = 0; index < 1024; ++index) {
        const Handle<TestTag> handle = pool.emplace(index, std::to_string(index));
        REQUIRE(handle.valid());
        REQUIRE(pool.get(handle) != nullptr);
        CHECK(pool.get(handle)->number == index);
    }
    CHECK(pool.size() == 1024);
}
