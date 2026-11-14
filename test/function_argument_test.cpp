// © Joseph Cameron - MIT License

#include <jfc/catch.hpp>

#include <jfc/lua.h>

#include <chrono>

#include <memory>
#include <optional>
#include <string>
#include <variant>

using namespace jfc::lua;
using namespace std::chrono_literals;

namespace {
    [[nodiscard]] bool mentions(const error_type &aError, const std::string &aText) {
        return aError && aError->find(aText) != std::string::npos;
    }
}

TEST_CASE("a script can give a typed function a function", "[callback]") {
    interpreter interp;

    auto env = interp.make_environment();

    SECTION("called during the call, with arguments and results") {
        env->register_function("apply", [](function_reference aFunction, const double aValue) {
            value_list_type results;

            REQUIRE_FALSE(aFunction.call({aValue}, results));

            return std::get<double>(results.at(0));
        });

        REQUIRE_FALSE(env->run("out = apply(function(x) return x * 3 end, 2)"));
        REQUIRE(env->read_number("out") == 6.0);
    }

    SECTION("held past the call") {
        std::optional<function_reference> held;

        env->register_function("later", [&](function_reference aFunction) { held = std::move(aFunction); });

        REQUIRE_FALSE(env->run(R"(
            local count = 0

            later(function()
                count = count + 1

                return count
            end)
        )"));

        interp.collect_garbage();

        value_list_type results;

        REQUIRE_FALSE(held->call({}, results));
        REQUIRE(std::get<double>(results.at(0)) == 1.0);

        REQUIRE_FALSE(held->call({}, results));
        REQUIRE(std::get<double>(results.at(0)) == 2.0);
    }

    SECTION("it can be a function c++ registered") {
        env->register_function("inner", [] { return 5.0; });

        env->register_function("apply", [](function_reference aFunction) {
            value_list_type results;

            REQUIRE_FALSE(aFunction.call({}, results));

            return std::get<double>(results.at(0));
        });

        REQUIRE_FALSE(env->run("out = apply(inner)"));
        REQUIRE(env->read_number("out") == 5.0);
    }

    SECTION("may be left out") {
        env->register_function("maybe", [](const std::optional<function_reference> &aFunction) {
            return aFunction.has_value();
        });

        REQUIRE_FALSE(env->run("a = maybe() b = maybe(print)"));
        REQUIRE(env->read_boolean("a") == false);
        REQUIRE(env->read_boolean("b") == true);
    }

    SECTION("anything else is refused") {
        env->register_function("apply", [](const function_reference &) {});

        REQUIRE(mentions(env->run("apply(1)"), "apply: argument 1 wants a function, got a number"));
        REQUIRE(mentions(env->run("apply(nil)"), "got nil"));
    }

    SECTION("its failure comes back as an error, not an exception") {
        env->register_function("apply", [](function_reference aFunction) { return aFunction.call().value_or(""); });

        REQUIRE_FALSE(env->run("out = apply(function() error('the bucket leaks') end)"));
        REQUIRE(env->read_string("out")->find("the bucket leaks") != std::string::npos);
    }

    SECTION("it can be given back") {
        env->register_function("same", [](function_reference aFunction) { return aFunction; });

        REQUIRE_FALSE(env->run("out = same(print) == print"));
        REQUIRE(env->read_boolean("out") == true);
    }

    SECTION("the value list route still refuses one") {
        env->register_function("takes", [](value_list_type) -> value_list_type { return {}; });

        REQUIRE(mentions(env->run("takes(print)"), "which cannot cross into c++"));
    }
}

TEST_CASE("a held function runs in the environment that made it", "[callback][environment]") {
    interpreter interp;

    auto one = interp.make_environment();
    auto two = interp.make_environment();

    std::optional<function_reference> held;

    one->register_function("later", [&](function_reference aFunction) { held = std::move(aFunction); });

    REQUIRE_FALSE(one->run("name = 'one' later(function() return name end)"));
    REQUIRE_FALSE(two->run("name = 'two'"));

    value_list_type results;

    REQUIRE_FALSE(held->call({}, results));
    REQUIRE(std::get<std::string>(results.at(0)) == "one");
}

TEST_CASE("a held function keeps its environment's time budget", "[callback][limits]") {
    interpreter interp;

    auto env = interp.make_environment(environment_policy{standard_library::safe, 100ms});

    std::optional<function_reference> held;

    env->register_function("later", [&](function_reference aFunction) { held = std::move(aFunction); });

    REQUIRE_FALSE(env->run(R"(
        later(function()
            while true do end
        end)
    )"));

    REQUIRE(mentions(held->call(), "budget"));
}

TEST_CASE("a held function outlives its interpreter object", "[callback]") {
    std::optional<function_reference> held;

    {
        interpreter interp;

        interp.register_function("later", [&](function_reference aFunction) { held = std::move(aFunction); });

        REQUIRE_FALSE(interp.run("later(function() return 'still here' end)"));
    }

    value_list_type results;

    REQUIRE_FALSE(held->call({}, results));
    REQUIRE(std::get<std::string>(results.at(0)) == "still here");
}

TEST_CASE("a function given from a coroutine", "[callback][coroutine]") {
    interpreter interp;

    auto env = interp.make_environment();

    std::optional<function_reference> held;

    env->register_function("later", [&](function_reference aFunction) { held = std::move(aFunction); });

    env->register_function("now", [](function_reference aFunction) {
        value_list_type results;

        REQUIRE_FALSE(aFunction.call({}, results));

        return std::get<double>(results.at(0));
    });

    SECTION("is called while the coroutine runs") {
        REQUIRE_FALSE(env->run("function errand() return now(function() return 4 end) end"));

        auto co = env->make_coroutine("errand");

        value_list_type out;

        REQUIRE_FALSE(co->resume({}, out));
        REQUIRE(std::get<double>(out.at(0)) == 4.0);
    }

    SECTION("is held past the coroutine's end") {
        REQUIRE_FALSE(env->run("function errand() later(function() return 'made there' end) end"));

        {
            auto co = env->make_coroutine("errand");

            REQUIRE_FALSE(co->resume());
        }

        interp.collect_garbage();

        value_list_type results;

        REQUIRE_FALSE(held->call({}, results));
        REQUIRE(std::get<std::string>(results.at(0)) == "made there");
    }
}

TEST_CASE("a typed function can take any lua value by reference", "[callback]") {
    interpreter interp;

    auto env = interp.make_environment();

    std::optional<reference> held;

    env->register_function("keep", [&](reference aValue) { held = std::move(aValue); });

    SECTION("a table, read later, as it is by then") {
        REQUIRE_FALSE(env->run("t = { x = 1 } keep(t) t.x = 2"));

        REQUIRE(held->is_table());
        REQUIRE(held->read_number("x") == 2.0);
    }

    SECTION("a table holding what a data_table refuses") {
        REQUIRE_FALSE(env->run("keep({ f = print })"));

        REQUIRE(held->read_reference("f")->is_function());
    }

    SECTION("a number") {
        REQUIRE_FALSE(env->run("keep(3)"));

        REQUIRE(held->type_name() == "number");
    }

    SECTION("not nil, which is no value to hold") {
        REQUIRE(mentions(env->run("keep(nil)"), "argument 1 wants a value, got nil"));
        REQUIRE(mentions(env->run("keep()"), "argument 1 wants a value, got no value"));
    }

    SECTION("but a std::optional may be nil") {
        env->register_function("maybe", [](const std::optional<reference> &aValue) { return aValue.has_value(); });

        REQUIRE_FALSE(env->run("out = maybe(nil)"));
        REQUIRE(env->read_boolean("out") == false);
    }

    SECTION("it can be given back") {
        env->register_function("same", [&] { return *held; });

        REQUIRE_FALSE(env->run("t = {} keep(t) out = same() == t"));
        REQUIRE(env->read_boolean("out") == true);
    }

    SECTION("one from another interpreter cannot be") {
        interpreter other;

        REQUIRE_FALSE(other.run("t = {}"));

        env->register_function("foreign", [&] { return *other.read_reference("t"); });

        REQUIRE(mentions(env->run("foreign()"), "another interpreter"));
    }

    SECTION("and is let go of when c++ does") {
        REQUIRE_FALSE(env->run(R"(
            weak = setmetatable({}, { __mode = "v" })

            local function fill() local t = {} weak[1] = t keep(t) end

            fill()
        )"));

        interp.collect_garbage();
        REQUIRE_FALSE(env->run("out = weak[1] == nil"));
        REQUIRE(env->read_boolean("out") == false);

        held.reset();

        interp.collect_garbage();
        REQUIRE_FALSE(env->run("out = weak[1] == nil"));
        REQUIRE(env->read_boolean("out") == true);
    }
}

TEST_CASE("a yielding typed function can take a function", "[callback][coroutine]") {
    interpreter interp;

    auto env = interp.make_environment();

    std::optional<function_reference> held;

    env->register_yielding_function("wait_for", [&](function_reference aFunction) { held = std::move(aFunction); });

    REQUIRE_FALSE(env->run("function errand() wait_for(function() return 1 end) return 2 end"));

    auto co = env->make_coroutine("errand");

    value_list_type out;

    REQUIRE_FALSE(co->resume({}, out));
    REQUIRE(held.has_value());

    REQUIRE_FALSE(held->call({}, out));
    REQUIRE(std::get<double>(out.at(0)) == 1.0);

    REQUIRE_FALSE(co->resume({}, out));
    REQUIRE(std::get<double>(out.at(0)) == 2.0);
}
