// © Joseph Cameron - All Rights Reserved

#include <jfc/catch.hpp>

#include <jfc/lua.h>

#include <stdexcept>
#include <string>

using namespace jfc::lua;

TEST_CASE("a lua function can be called, with values rather than source", "[call]") {
    interpreter interp;

    auto env = interp.make_environment();

    REQUIRE_FALSE(env->run(
        "function add(a, b) return a + b end\n"
        "function greet(who) return 'hello ' .. who end\n"
        "function nothing() end\n"
        "function several() return 1, 'two', true, nil end\n"
        "npc = { hp = 10, on_hit = function(self, amount) return 'took ' .. amount end }\n"
        "function describe(t) return t.name .. '/' .. t.hp end\n"
        "function make() return { name = 'jim', hp = 5 } end").has_value());

    value_list_type results;

    SECTION("arguments and one result") {
        REQUIRE_FALSE(env->call("add", {2.0, 3.0}, results).has_value());

        REQUIRE(results.size() == 1);
        REQUIRE(std::get<double>(results.at(0)) == 5);
    }

    SECTION("a function reached through a path") {
        REQUIRE_FALSE(env->call("npc.on_hit", {nullptr, 7.0}, results).has_value());

        REQUIRE(std::get<std::string>(results.at(0)) == "took 7");
    }

    SECTION("no results at all") {
        REQUIRE_FALSE(env->call("nothing", {}, results).has_value());

        REQUIRE(results.empty());
    }

    SECTION("several results, in order, including nil") {
        REQUIRE_FALSE(env->call("several", {}, results).has_value());

        REQUIRE(results.size() == 4);
        REQUIRE(std::get<double>(results.at(0)) == 1);
        REQUIRE(std::get<std::string>(results.at(1)) == "two");
        REQUIRE(std::get<bool>(results.at(2)) == true);
        REQUIRE(std::holds_alternative<decltype(nullptr)>(results.at(3)));
    }

    SECTION("a table in and a table out") {
        data_table argument;

        argument.set("name", "robert");
        argument.set("hp", 3.0);

        REQUIRE_FALSE(env->call("describe", {argument}, results).has_value());
        REQUIRE(std::get<std::string>(results.at(0)) == "robert/3");

        REQUIRE_FALSE(env->call("make", {}, results).has_value());
        REQUIRE(std::get<data_table>(results.at(0)).get_string("name") == "jim");
    }

    SECTION("the overload that discards results") {
        REQUIRE_FALSE(env->call("add", {1.0, 1.0}).has_value());
        REQUIRE_FALSE(env->call("nothing").has_value());
    }

    SECTION("results are cleared, so a later call cannot read an earlier one's") {
        REQUIRE_FALSE(env->call("several", {}, results).has_value());
        REQUIRE(results.size() == 4);

        REQUIRE_FALSE(env->call("nothing", {}, results).has_value());
        REQUIRE(results.empty());
    }
}

TEST_CASE("a string argument is a string, not source", "[call]") {
    interpreter interp;

    auto env = interp.make_environment();

    REQUIRE_FALSE(env->run("seen = nil  function remember(who) seen = who end").has_value());

    const std::string crafted = "'); stolen = 'yes'; remember('";

    SECTION("through call, it stays a value") {
        REQUIRE_FALSE(env->call("remember", {crafted}).has_value());

        REQUIRE(env->read_string("seen") == crafted);
        REQUIRE_FALSE(env->read_string("stolen").has_value());
    }

    SECTION("where building source executes it") {
        REQUIRE_FALSE(env->run("remember('" + crafted + "')").has_value());

        REQUIRE(env->read_string("stolen") == "yes");
    }
}

TEST_CASE("a call that goes wrong is reported, not thrown or crashed", "[call]") {
    interpreter interp;

    auto env = interp.make_environment();

    REQUIRE_FALSE(env->run(
        "not_a_function = 7\n"
        "function explodes() error('deliberate') end\n"
        "function gives_back_a_function() return print end\n"
        "function good_then_bad() return 1, 'two', print end").has_value());

    value_list_type results;

    SECTION("a path that resolves to something else") {
        const auto error = env->call("not_a_function", {}, results);

        REQUIRE(error.has_value());

        INFO("message was: " << *error);

        REQUIRE(error->find("not_a_function") != std::string::npos);
        REQUIRE(error->find("number") != std::string::npos);
    }

    SECTION("a path that resolves to nothing") {
        const auto error = env->call("no.such.thing", {}, results);

        REQUIRE(error.has_value());
        REQUIRE(error->find("no.such.thing") != std::string::npos);
    }

    SECTION("an error raised inside the function") {
        const auto error = env->call("explodes", {}, results);

        REQUIRE(error.has_value());
        REQUIRE(error->find("deliberate") != std::string::npos);
    }

    SECTION("a result of a kind that cannot cross, named and positioned") {
        const auto error = env->call("gives_back_a_function", {}, results);

        REQUIRE(error.has_value());

        INFO("message was: " << *error);

        REQUIRE(error->find("function") != std::string::npos);
        REQUIRE(results.empty());
    }

    SECTION("and a partly convertible return leaves nothing behind rather than half of it") {
        const auto error = env->call("good_then_bad", {}, results);

        REQUIRE(error.has_value());

        INFO("message was: " << *error);

        REQUIRE(error->find("position 3") != std::string::npos);
        REQUIRE(results.empty());
    }

    SECTION("and the environment keeps working afterwards") {
        for (int i = 0; i < 200; ++i) REQUIRE(env->call("explodes", {}, results).has_value());

        REQUIRE_FALSE(env->run("recovered = 1").has_value());
        REQUIRE(env->read_number("recovered") == 1);
    }
}

TEST_CASE("a call goes through the same guards as a run", "[call]") {
    SECTION("a c++ function that throws, reached through a lua function, comes back as an error") {
        interpreter interp;

        auto env = interp.make_environment();

        env->register_function("explodes", [](value_list_type) -> value_list_type {
            throw std::runtime_error("from the registered function");
        });

        REQUIRE_FALSE(env->run("function outer() explodes() end").has_value());

        const auto error = env->call("outer");

        REQUIRE(error.has_value());
        REQUIRE(error->find("from the registered function") != std::string::npos);
    }

    SECTION("a runaway function is stopped by the instruction budget") {
        interpreter interp(interpreter_policy{.MEMORY_BUDGET_IN_BYTES = 0,
            .MEMORY_GROWTH_BUDGET_IN_BYTES_PER_RUN = 0, .INSTRUCTION_BUDGET = 100000});

        auto env = interp.make_environment();

        REQUIRE_FALSE(env->run("function forever() while true do end end").has_value());

        const auto error = env->call("forever");

        REQUIRE(error.has_value());
        REQUIRE(error->find("instruction budget") != std::string::npos);
    }

    SECTION("a call resolves in its own environment") {
        interpreter interp;

        auto one = interp.make_environment();
        auto two = interp.make_environment();

        REQUIRE_FALSE(one->run("owner = 'one' function who() return owner end").has_value());
        REQUIRE_FALSE(two->run("owner = 'two' function who() return owner end").has_value());

        value_list_type results;

        REQUIRE_FALSE(one->call("who", {}, results).has_value());
        REQUIRE(std::get<std::string>(results.at(0)) == "one");

        REQUIRE_FALSE(two->call("who", {}, results).has_value());
        REQUIRE(std::get<std::string>(results.at(0)) == "two");
    }
}
