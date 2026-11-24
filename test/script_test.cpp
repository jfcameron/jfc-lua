// © Joseph Cameron - MIT License

#include <jfc/catch.hpp>

#include <jfc/lua.h>
#include <jfc/lua/compiles_kept.h>

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

TEST_CASE("a source compiled again is given what it compiled to, and runs the same", "[script][kept]") {
    const std::string source = "kept_once = (kept_once or 0) + 1";

    interpreter lua;

    auto env = lua.make_environment();

    const auto before = backend::compiles_kept();

    const script first(source, "first.lua");
    const script second(source, "second.lua");

    REQUIRE_FALSE(env->run(first));
    REQUIRE_FALSE(env->run(second));
    REQUIRE_FALSE(env->run(source));
    REQUIRE(env->read_number("kept_once") == 3);

    const auto after = backend::compiles_kept();

#if defined(JFC_LUA_LUAU)
    REQUIRE(after.compiled == before.compiled + 1);
    REQUIRE(after.sources >= 1);
#else
    REQUIRE(after.compiled == before.compiled);
    REQUIRE(after.sources == 0);
#endif
}

TEST_CASE("what is kept stays under the most bytes, all let go when one more would pass it", "[script][kept]") {
    const auto sized = [](const std::size_t aBytes, const char aFill) {
        return "return \"" + std::string(aBytes, aFill) + "\"";
    };

    for (const char each : {'a', 'b', 'c', 'd', 'e'}) {
        static_cast<void>(script(sized(backend::MOST_KEPT_BYTES / 6, each)));

        REQUIRE(backend::compiles_kept().bytes <= backend::MOST_KEPT_BYTES);
    }

#if defined(JFC_LUA_LUAU)
    REQUIRE(backend::compiles_kept().sources < 5);

    const auto before = backend::compiles_kept();

    const auto huge = sized(backend::MOST_KEPT_BYTES, 'z');

    static_cast<void>(script(huge));
    static_cast<void>(script(huge));

    REQUIRE(backend::compiles_kept().compiled == before.compiled + 2);
    REQUIRE(backend::compiles_kept().sources == before.sources);
#endif
}
