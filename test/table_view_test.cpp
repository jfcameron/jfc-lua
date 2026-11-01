// © Joseph Cameron - MIT License

#include <jfc/catch.hpp>

#include <jfc/lua.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace jfc::lua;

namespace {
    [[nodiscard]] bool mentions(const error_type &aError, const std::string &aText) {
        return aError && aError->find(aText) != std::string::npos;
    }
}

TEST_CASE("a table_view reads a script's table in place", "[view]") {
    interpreter interp;

    auto env = interp.make_environment();

    SECTION("numbers by name") {
        env->register_function("x_of", [](const table_view &aTable) { return aTable.get_number("x"); });

        REQUIRE_FALSE(env->run("out = x_of({ x = 7 })"));
        REQUIRE(env->read_number("out") == 7.0);
    }

    SECTION("booleans") {
        env->register_function("on", [](const table_view aTable) { return aTable.get_boolean("on"); });

        REQUIRE_FALSE(env->run("out = on({ on = true })"));
        REQUIRE(env->read_boolean("out") == true);
    }

    SECTION("strings, whatever they hold") {
        std::size_t length = 0;

        env->register_function("name", [&](const table_view &aTable) {
            length = aTable.get_string("name")->size();

            return aTable.get_string("name");
        });

        REQUIRE_FALSE(env->run("out = name({ name = 'a\\0b' })"));
        REQUIRE(length == 3);
        REQUIRE(env->read_string("out") == std::string("a\0b", 3));
    }

    SECTION("a list by its position") {
        env->register_function("total", [](const table_view &aTable) {
            double total = 0;

            for (std::size_t i = 1; i <= aTable.size(); ++i) total += *aTable.get_number(i);

            return total;
        });

        REQUIRE_FALSE(env->run("out = total({ 1, 2, 3, 4 })"));
        REQUIRE(env->read_number("out") == 10.0);
    }

    SECTION("a boolean key") {
        env->register_function("at_true", [](const table_view &aTable) { return aTable.get_number(true); });

        REQUIRE_FALSE(env->run("out = at_true({ [true] = 3 })"));
        REQUIRE(env->read_number("out") == 3.0);
    }

    SECTION("what is not there, or is another type, is empty") {
        env->register_function("probe", [](const table_view &aTable) {
            return !aTable.get_number("missing") && !aTable.get_number("text") && !aTable.get_string("number")
                && !aTable.get_boolean("number") && !aTable.get_table("number");
        });

        REQUIRE_FALSE(env->run("out = probe({ text = 'a', number = 1 })"));
        REQUIRE(env->read_boolean("out") == true);
    }

    SECTION("a number is not read as a string") {
        env->register_function("probe", [](const table_view &aTable) { return aTable.get_string(1).has_value(); });

        REQUIRE_FALSE(env->run("out = probe({ 5 })"));
        REQUIRE(env->read_boolean("out") == false);
    }

    SECTION("a table may hold what a data_table refuses") {
        env->register_function("a_of", [](const table_view &aTable) { return aTable.get_number("a"); });

        REQUIRE_FALSE(env->run("out = a_of({ a = 1, f = function() end, u = print })"));
        REQUIRE(env->read_number("out") == 1.0);
    }

    SECTION("a metamethod is not run") {
        env->register_function("probe", [](const table_view &aTable) { return !aTable.get_number("x"); });

        REQUIRE_FALSE(env->run(R"(
            out = probe(setmetatable({}, { __index = function() error("ran") end }))
        )"));
        REQUIRE(env->read_boolean("out") == true);
    }

    SECTION("it is optional like anything else") {
        env->register_function("maybe", [](const std::optional<table_view> &aTable) {
            return aTable ? aTable->get_number("x") : std::nullopt;
        });

        REQUIRE_FALSE(env->run("a = maybe() == nil b = maybe({ x = 2 })"));
        REQUIRE(env->read_boolean("a") == true);
        REQUIRE(env->read_number("b") == 2.0);
    }

    SECTION("beside other arguments") {
        env->register_function("mix", [](const double aScale, const table_view &aTable, const std::string_view aName) {
            return aScale * *aTable.get_number(std::string(aName));
        });

        REQUIRE_FALSE(env->run("out = mix(2, { speed = 4 }, 'speed')"));
        REQUIRE(env->read_number("out") == 8.0);
    }

    SECTION("anything but a table is refused") {
        env->register_function("takes", [](const table_view &) {});

        REQUIRE(mentions(env->run("takes(1)"), "takes: argument 1 wants a table, got a number"));
        REQUIRE(mentions(env->run("takes()"), "got no value"));
    }
}

TEST_CASE("a table_view knows what its table holds", "[view]") {
    interpreter interp;

    auto env = interp.make_environment();

    SECTION("contains") {
        env->register_function("probe", [](const table_view &aTable) {
            return aTable.contains("a") && aTable.contains(1) && aTable.contains(true) && aTable.contains("f")
                && !aTable.contains("b") && !aTable.contains(2);
        });

        REQUIRE_FALSE(env->run("out = probe({ 'first', a = 1, [true] = 2, f = print })"));
        REQUIRE(env->read_boolean("out") == true);
    }

    SECTION("size counts every key, as a data_table's does") {
        env->register_function("count", [](const table_view &aTable) { return aTable.size(); });

        REQUIRE_FALSE(env->run("out = count({ 1, 2, 3, a = 1, [10] = 2, f = print })"));
        REQUIRE(env->read_number("out") == 6.0);
    }

    SECTION("empty") {
        env->register_function("nothing", [](const table_view &aTable) { return aTable.empty(); });

        REQUIRE_FALSE(env->run("a = nothing({}) b = nothing({ 1 }) c = nothing({ x = false })"));
        REQUIRE(env->read_boolean("a") == true);
        REQUIRE(env->read_boolean("b") == false);
        REQUIRE(env->read_boolean("c") == false);
    }

    SECTION("keys lists what a key can name, and steps over the rest") {
        std::vector<key> seen;

        env->register_function("collect", [&](const table_view &aTable) { seen = aTable.keys(); });

        REQUIRE_FALSE(env->run("collect({ 'first', a = 1, [true] = 2, [{}] = 3 })"));

        REQUIRE(seen.size() == 3);
        REQUIRE(std::find(seen.begin(), seen.end(), key(1)) != seen.end());
        REQUIRE(std::find(seen.begin(), seen.end(), key("a")) != seen.end());
        REQUIRE(std::find(seen.begin(), seen.end(), key(true)) != seen.end());
    }

    SECTION("listing keys leaves the stack as it was") {
        env->register_function("again", [](const table_view &aTable) {
            for (int i = 0; i < 1000; ++i) REQUIRE(aTable.keys().size() == 2);

            return aTable.get_number("a");
        });

        REQUIRE_FALSE(env->run("out = again({ a = 1, b = 2 })"));
        REQUIRE(env->read_number("out") == 1.0);
    }

    SECTION("to_data_table keeps it past the call") {
        std::optional<data_table> kept;

        env->register_function("keep", [&](const table_view &aTable) { kept = aTable.to_data_table(); });

        REQUIRE_FALSE(env->run("keep({ a = 1, inner = { b = 2 } })"));
        REQUIRE_FALSE(interp.run("collectgarbage('collect')"));

        REQUIRE(kept->get_number("a") == 1.0);
        REQUIRE(kept->get_data_table("inner")->get_number("b") == 2.0);
    }

    SECTION("to_data_table refuses what a data_table does") {
        env->register_function("keep", [](const table_view &aTable) { (void)aTable.to_data_table(); });

        REQUIRE(mentions(env->run("keep({ f = print })"), "function"));
    }
}

TEST_CASE("a nested table_view", "[view]") {
    interpreter interp;

    auto env = interp.make_environment();

    SECTION("reads its own table") {
        env->register_function("inner", [](const table_view &aTable) {
            return aTable.get_table("place")->get_table("cell")->get_number("x");
        });

        REQUIRE_FALSE(env->run("out = inner({ place = { cell = { x = 4 } } })"));
        REQUIRE(env->read_number("out") == 4.0);
    }

    SECTION("in a loop far past lua's stack limit") {
        env->register_function("total", [](const table_view &aTable) {
            double total = 0;

            for (std::size_t i = 1; i <= aTable.size(); ++i) total += *aTable.get_table(i)->get_number("x");

            return total;
        });

        REQUIRE_FALSE(env->run(R"(
            local list = {}

            for i = 1, 20000 do list[i] = { x = 1 } end

            out = total(list)
        )"));
        REQUIRE(env->read_number("out") == 20000.0);
    }

    SECTION("two held at once, let go of in the order they were made") {
        env->register_function("both", [](const table_view &aTable) {
            auto first = aTable.get_table("a");
            auto second = aTable.get_table("b");

            const double a = *first->get_number("x");

            first.reset();

            const double b = *second->get_number("x");

            second.reset();

            return a + b + *aTable.get_number("c");
        });

        REQUIRE_FALSE(env->run("out = both({ a = { x = 1 }, b = { x = 2 }, c = 3 })"));
        REQUIRE(env->read_number("out") == 6.0);
    }

    SECTION("a result pushed after one is gone is the result") {
        env->register_function("pair", [](const table_view &aTable) {
            const auto inner = aTable.get_table("a");

            return std::tuple{*inner->get_number("x"), std::string(*inner->get_string("y"))};
        });

        REQUIRE_FALSE(env->run("a, b = pair({ a = { x = 1, y = 'two' } })"));
        REQUIRE(env->read_number("a") == 1.0);
        REQUIRE(env->read_string("b") == "two");
    }

    SECTION("a view moved from leaves its slot alone") {
        env->register_function("moved", [](const table_view &aTable) {
            auto first = aTable.get_table("a");
            const table_view second = std::move(*first);

            first.reset();

            return second.get_number("x");
        });

        REQUIRE_FALSE(env->run("out = moved({ a = { x = 1 } })"));
        REQUIRE(env->read_number("out") == 1.0);
    }
}

TEST_CASE("a table given to a table_view stays on the stack for the whole call", "[view]") {
    interpreter interp;

    auto env = interp.make_environment();

    env->register_function("name", [&](table_view aTable) {
        const auto name = *aTable.get_string("name");

        { const table_view gone = std::move(aTable); }

        REQUIRE_FALSE(interp.run("collectgarbage('collect') collectgarbage('collect')"));

        return std::string(name);
    });

    REQUIRE_FALSE(env->run(R"(out = name({ name = "a string only this table " .. "holds" }))"));
    REQUIRE(env->read_string("out") == "a string only this table holds");
}

TEST_CASE("a table_view does not fail for want of lua's memory", "[view][limits]") {
    interpreter interp(interpreter_policy{8u << 20});

    auto env = interp.make_environment();

    bool finished = false;

    env->register_function("probe", [&](const table_view &aTable) {
        finished = !aTable.get_number(std::string(256u << 10, 'k')).has_value();
    });

    const auto ignored = env->run(R"(
        local held, empty = {}, {}

        pcall(function()
            while true do held[#held + 1] = string.rep("x", 1024) .. #held end
        end)

        probe(empty)
    )");

    REQUIRE(finished);
}

TEST_CASE("a table_view stays right across a call back into lua", "[view]") {
    interpreter interp;

    auto env = interp.make_environment();

    env->register_function("twice", [&](const table_view &aTable) {
        value_list_type results;

        REQUIRE_FALSE(env->call("double_it", {*aTable.get_number("x")}, results));

        return std::get<double>(results.at(0)) + *aTable.get_number("x");
    });

    REQUIRE_FALSE(env->run("function double_it(x) return x * 2 end out = twice({ x = 4 })"));
    REQUIRE(env->read_number("out") == 12.0);
}

TEST_CASE("a table_view works from a coroutine", "[view][coroutine]") {
    interpreter interp;

    auto env = interp.make_environment();

    env->register_function("x_of", [](const table_view &aTable) { return aTable.get_number("x"); });

    REQUIRE_FALSE(env->run("function errand() return x_of({ x = 5 }) end"));

    auto co = env->make_coroutine("errand");

    value_list_type out;

    REQUIRE_FALSE(co->resume({}, out));
    REQUIRE(std::get<double>(out.at(0)) == 5.0);
}

TEST_CASE("a value list route still copies a table into a data_table", "[view]") {
    interpreter interp;

    auto env = interp.make_environment();

    env->register_function("takes", [](value_list_type aArguments) -> value_list_type {
        return {*std::get<data_table>(aArguments.at(0)).get_number("a")};
    });

    REQUIRE_FALSE(env->run("out = takes({ a = 1 })"));
    REQUIRE(env->read_number("out") == 1.0);
}
