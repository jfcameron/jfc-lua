// © Joseph Cameron - MIT License

#include <jfc/catch.hpp>

#include <jfc/lua.h>

#include <chrono>
#include <string>

using namespace jfc::lua;
using namespace std::chrono_literals;

TEST_CASE("a compiled script runs in every environment it is given, against each one's globals", "[script]") {
    const script counting("count = (count or 0) + 1", "counting.lua");

    interpreter first;
    interpreter second;

    auto a = first.make_environment();
    auto b = first.make_environment();
    auto c = second.make_environment();

    REQUIRE_FALSE(a->run(counting));
    REQUIRE_FALSE(a->run(counting));
    REQUIRE_FALSE(b->run(counting));
    REQUIRE_FALSE(c->run(counting));
    REQUIRE_FALSE(first.run(counting));

    REQUIRE(a->read_number("count") == 2);
    REQUIRE(b->read_number("count") == 1);
    REQUIRE(c->read_number("count") == 1);
    REQUIRE(first.read_number("count") == 1);
}

TEST_CASE("a compiled script's errors name it as run names its source", "[script]") {
    interpreter lua;

    auto env = lua.make_environment();

    SECTION("by its name") {
        const auto error = env->run(script("\nerror('the bucket leaks')", "rules.lua"));

        REQUIRE(error);
        REQUIRE_THAT(*error, Catch::Matchers::Contains("rules.lua:2: the bucket leaks"));
    }

    SECTION("by its source where it has none") {
        const std::string source = "error('the bucket leaks')";

        const auto compiled = env->run(script(source));

        REQUIRE(compiled);
        REQUIRE(compiled == env->run(source));
    }

    SECTION("one that does not compile gives its error where it is run, each time") {
        const script broken("x = = 1", "broken.lua");

        const auto error = env->run(broken);

        REQUIRE(error);
        REQUIRE_THAT(*error, Catch::Matchers::Contains("broken.lua:1"));
        REQUIRE(env->run(broken) == error);
    }
}

TEST_CASE("a compiled script keeps to its environment's time budget", "[script][limits]") {
    interpreter lua;

    auto env = lua.make_environment(environment_policy{standard_library::safe, 1ms});

    const auto error = env->run(script("while true do end", "spin.lua"));

    REQUIRE(error);
    REQUIRE_THAT(*error, Catch::Matchers::Contains("time budget"));
}
