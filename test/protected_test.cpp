// © Joseph Cameron - MIT License

#include <jfc/catch.hpp>

#include <jfc/lua.h>

#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <variant>

using namespace jfc::lua;

TEST_CASE("a metamethod that raises does not end the program", "[protected]") {
    interpreter interp;

    auto env = interp.make_environment();

    REQUIRE_FALSE(env->run(
        "t = setmetatable({}, {\n"
        "    __index = function() error('no reading') end,\n"
        "    __newindex = function() error('no writing') end })").has_value());

    SECTION("a read through it is nothing") {
        REQUIRE_FALSE(env->read_number("t.x").has_value());
        REQUIRE_FALSE(env->read_string("t.x").has_value());
        REQUIRE_FALSE(env->read_data_table("t.x").has_value());
        REQUIRE_FALSE(env->read_reference("t.x").has_value());
        REQUIRE_FALSE(env->read_userdata("t.x").has_value());
    }

    SECTION("a write through it throws, saying where and why") {
        REQUIRE_THROWS_AS(env->write("t.x", 1.0), exception);
        REQUIRE_THROWS_WITH(env->write("t.x", 1.0), Catch::Matchers::Contains("t.x")
            && Catch::Matchers::Contains("no writing"));
    }

    SECTION("and the environment keeps working") {
        REQUIRE_FALSE(env->read_number("t.x").has_value());
        REQUIRE_THROWS_AS(env->write("t.x", 1.0), exception);

        env->write("fine", 2.0);

        REQUIRE(env->read_number("fine") == 2);
        REQUIRE_FALSE(env->run("after = fine + 1").has_value());
        REQUIRE(env->read_number("after") == 3);
    }
}

TEST_CASE("a read through a metamethod that never returns is stopped by the budget", "[protected]") {
    interpreter interp(interpreter_policy{0, 0, 100000});

    auto env = interp.make_environment();

    REQUIRE_FALSE(env->run("t = setmetatable({}, { __index = function() while true do end end })").has_value());

    REQUIRE_FALSE(env->read_number("t.x").has_value());
}

namespace {
    struct giver final {};
}

TEST_CASE("the memory budget spent, what c++ asks of lua fails without ending the program", "[protected]") {
    std::weak_ptr<int> given;

    const std::string big(64 * 1024, 'y');

    const auto give = [&given, &big](auto &&...) {
        auto object = std::make_shared<int>(7);

        given = object;

        return value_list_type{big, userdata::make(object)};
    };

    interpreter interp(interpreter_policy{1u << 20});

    interp.register_type<giver>("giver", {{"give", [&give](giver &, value_list_type) { return give(); }}});

    auto env = interp.make_environment(environment_policy{standard_library::unrestricted});

    REQUIRE_FALSE(env->run(
        "function take(...) return select('#', ...) end\n"
        "function release() hog = nil collectgarbage() end\n"
        "function waiting() while true do coroutine.yield(select('#', coroutine.yield())) end end\n"
        "function give_by_method() return the_giver:give() end").has_value());

    env->register_function("give", [&give](value_list_type) { return give(); });

    env->write("the_giver", userdata::make(std::make_shared<giver>()));

    auto co = env->make_coroutine("waiting");

    REQUIRE(co);
    REQUIRE_FALSE(co->resume().has_value());

    REQUIRE_FALSE(env->run(
        "hog = {}\n"
        "assert(not pcall(function() for n = 1, 1e9 do hog[n] = string.rep('x', 1000) .. n end end))\n"
        "for n = 1, 8 do hog[#hog] = nil end\n"
        "collectgarbage()").has_value());

    SECTION("a write throws") {
        REQUIRE_THROWS_AS([&] { for (int n = 0; n < 64; ++n) env->write("more.k" + std::to_string(n), big); }(),
            exception);
    }

    SECTION("a call that cannot push its arguments is an error") {
        value_list_type arguments;

        for (int n = 0; n < 64; ++n) arguments.push_back(big + std::to_string(n));

        REQUIRE(env->call("take", arguments).has_value());
    }

    SECTION("a registered function whose results cannot be pushed is an error, saying why, and lets go of them") {
        const auto error = env->call("give");

        REQUIRE(error.has_value());
        REQUIRE_THAT(*error, Catch::Matchers::Contains("not enough memory"));
        REQUIRE(given.expired());
    }

    SECTION("so is a method") {
        const auto error = env->call("give_by_method");

        REQUIRE(error.has_value());
        REQUIRE_THAT(*error, Catch::Matchers::Contains("not enough memory"));
        REQUIRE(given.expired());
    }

    SECTION("a resume that cannot push its arguments is an error, and the coroutine waits on") {
        const auto error = co->resume({big});

        REQUIRE(error.has_value());
        REQUIRE_THAT(*error, Catch::Matchers::Contains("not enough memory"));
        REQUIRE(co->state() == coroutine::status::suspended);

        SECTION("to be resumed once there is room") {
            REQUIRE_FALSE(env->call("release").has_value());

            value_list_type results;

            REQUIRE_FALSE(co->resume({1.0, 2.0}, results).has_value());
            REQUIRE(results.size() == 1);
            REQUIRE(std::get<double>(results.at(0)) == 2.0);
        }
    }

    SECTION("a resume with more arguments than the coroutine has room for does not end the program") {
        const value_list_type many(2000, 1.0);

        value_list_type results;

        const auto error = co->resume(many, results);

        REQUIRE(error.has_value());
        REQUIRE_THAT(*error, Catch::Matchers::Contains("not enough memory"));
        REQUIRE(co->state() == coroutine::status::suspended);
    }

    SECTION("letting go of a reference does not end the program") {
        REQUIRE_NOTHROW(co.reset());
    }

    SECTION("and once the script lets go, there is room again") {
        REQUIRE_FALSE(env->call("release").has_value());

        env->write("more.k", big);

        REQUIRE(env->read_string("more.k")->size() == big.size());
    }
}

TEST_CASE("an interpreter too small to set up is refused, not fatal", "[protected]") {
    REQUIRE_THROWS_AS(interpreter(interpreter_policy{4096}), exception);
}

TEST_CASE("a key lua refuses is refused on write, not fatal", "[protected]") {
    interpreter interp;

    auto env = interp.make_environment();

    REQUIRE_FALSE(env->run("t = {}").has_value());

    REQUIRE_THROWS_AS(env->write(path{"t", std::numeric_limits<double>::quiet_NaN()}, 1.0), exception);
}

TEST_CASE("an empty path names nothing to write to", "[protected]") {
    interpreter interp;

    auto env = interp.make_environment();

    REQUIRE_THROWS_WITH(env->write(path{}, 1.0), Catch::Matchers::Contains("empty path"));
    REQUIRE_THROWS_AS(env->write(path(std::vector<key>{}), std::string("x")), exception);
    REQUIRE_THROWS_AS(env->register_function(path{}, [](value_list_type) -> value_list_type { return {}; }), exception);
}
