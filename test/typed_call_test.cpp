// © Joseph Cameron - MIT License

#include <jfc/catch.hpp>

#include <jfc/lua.h>

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>

using namespace jfc::lua;

namespace {
    struct lantern final { int lit = 0; };

    [[nodiscard]] bool mentions(const error_type &aError, const std::string &aText) {
        return aError && aError->find(aText) != std::string::npos;
    }
}

TEST_CASE("a typed call gives a function what it is passed", "[call][typed]") {
    interpreter lua;

    auto env = lua.make_environment();

    REQUIRE_FALSE(env->run("function kind(a) return type(a) end function pass(a) return a end"));

    const auto kind_of = [&](const auto &aValue) { return *env->call<std::string>("kind", aValue); };

    SECTION("each kind of value") {
        REQUIRE(kind_of(1.5) == "number");
        REQUIRE(kind_of(3) == "number");
        REQUIRE(kind_of(true) == "boolean");
        REQUIRE(kind_of("literal") == "string");
        REQUIRE(kind_of(std::string("owned")) == "string");
        REQUIRE(kind_of(std::string_view("viewed")) == "string");
        REQUIRE(kind_of(nullptr) == "nil");
        REQUIRE(kind_of(std::optional<double>{}) == "nil");
        REQUIRE(kind_of(std::optional<double>{1.0}) == "number");
        REQUIRE(kind_of(data_table{}) == "table");
        REQUIRE(kind_of(std::make_shared<lantern>()) == "userdata");
        REQUIRE(kind_of(*env->read_reference("pass")) == "function");
    }

    SECTION("a boolean stays a boolean, and a whole number a number") {
        REQUIRE(*env->call<bool>("pass", true) == true);
        REQUIRE(*env->call<int>("pass", 7) == 7);
    }

    SECTION("several, in order") {
        REQUIRE_FALSE(env->run("function join(a, b, c) return a .. b .. tostring(c) end"));

        REQUIRE(*env->call<std::string>("join", "a", 1, true) == "a1true");
    }

    SECTION("a tuple is several arguments") {
        REQUIRE_FALSE(env->run("function add(a, b) return a + b end"));

        REQUIRE(*env->call<double>("add", std::tuple{1.0, 2.0}) == 3.0);
    }

    SECTION("none") {
        REQUIRE_FALSE(env->run("function four() return 4 end"));

        REQUIRE(*env->call<double>("four") == 4.0);
    }

    SECTION("more than lua's guaranteed stack") {
        REQUIRE_FALSE(env->run("function count(...) return select('#', ...) end"));

        const auto ten = std::tuple{1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

        REQUIRE(*env->call<int>("count", std::tuple_cat(ten, ten, ten, ten, ten, ten)) == 60);
    }

    SECTION("more than lua's guaranteed stack, one by one") {
        REQUIRE_FALSE(env->run("function count(...) return select('#', ...) end"));

        const auto count = [&]<std::size_t... indices>(std::index_sequence<indices...>) {
            return *env->call<int>("count", static_cast<int>(indices)...);
        };

        REQUIRE(count(std::make_index_sequence<300>{}) == 300);
    }

    SECTION("a string that holds anything") {
        REQUIRE(*env->call<std::string>("pass", std::string_view("a\0b", 3)) == std::string("a\0b", 3));
    }
}

TEST_CASE("a typed call gives back what it asks for", "[call][typed]") {
    interpreter lua;

    auto env = lua.make_environment();

    REQUIRE_FALSE(env->run(R"(
        function pass(...) return ... end
        function many() return 1, "two", true end
        function nothing() end
    )"));

    SECTION("a number") {
        REQUIRE(*env->call<double>("pass", 1.5) == 1.5);
    }

    SECTION("a string") {
        REQUIRE(*env->call<std::string>("pass", "text") == "text");
    }

    SECTION("a data_table") {
        data_table table;

        table.set("a", 1.0);

        REQUIRE(env->call<data_table>("pass", table)->get_number("a") == 1.0);
    }

    SECTION("a bound object") {
        const auto light = std::make_shared<lantern>();

        REQUIRE(*env->call<std::shared_ptr<lantern>>("pass", light) == light);
        REQUIRE(env->call<userdata>("pass", light)->get<lantern>() == light);
    }

    SECTION("a reference, kept") {
        REQUIRE_FALSE(env->run("t = { x = 2 }"));

        auto held = *env->read_reference("t");

        REQUIRE(env->call<reference>("pass", held)->read_number("x") == 2.0);
    }

    SECTION("a function, kept") {
        REQUIRE_FALSE(env->run("function five() return 5 end"));

        auto held = env->call<function_reference>("pass", *env->read_reference("five"));

        REQUIRE(*held->call<double>() == 5.0);
    }

    SECTION("several as a tuple") {
        const auto results = env->call<std::tuple<double, std::string, bool>>("many");

        REQUIRE(results);
        REQUIRE(std::get<0>(*results) == 1.0);
        REQUIRE(std::get<1>(*results) == "two");
        REQUIRE(std::get<2>(*results) == true);
    }

    SECTION("every result as a value list") {
        REQUIRE(env->call<value_list_type>("many")->size() == 3);
        REQUIRE(env->call<value_list_type>("nothing")->empty());
    }

    SECTION("more than it asks for, which are dropped") {
        REQUIRE(*env->call<double>("many") == 1.0);
    }

    SECTION("an optional result that is not there") {
        REQUIRE_FALSE(env->call<std::optional<double>>("nothing")->has_value());
        REQUIRE(env->call<std::optional<double>>("pass", nullptr)->has_value() == false);
    }

    SECTION("an optional result that is") {
        REQUIRE(env->call<std::optional<double>>("pass", 2.0)->value() == 2.0);
    }

    SECTION("nothing, as an error_type") {
        const error_type error = env->call("pass", 1.0);

        REQUIRE_FALSE(error);
        REQUIRE_FALSE(env->call<void>("pass", 1.0));
    }
}

TEST_CASE("a typed call says what went wrong", "[call][typed]") {
    interpreter lua;

    auto env = lua.make_environment();

    REQUIRE_FALSE(env->run(R"(
        function pass(...) return ... end
        function fails() error("the bucket leaks") end
        not_a_function = 1
        namespace = { pass = pass }
    )"));

    SECTION("a result that holds nothing") {
        const auto result = env->call<double>("pass", "text");

        REQUIRE_FALSE(result);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error() == "pass: result 1 wants a number, got a string");
    }

    SECTION("by the name it was called by") {
        REQUIRE(env->call<double>("namespace.pass", "text").error() == "namespace.pass: result 1 wants a number, got a string");
    }

    SECTION("a result missing") {
        REQUIRE(env->call<double>("pass").error() == "pass: result 1 wants a number, got no value");
    }

    SECTION("the position in a tuple") {
        const auto result = env->call<std::tuple<double, double>>("pass", 1.0, "text");

        REQUIRE(result.error() == "pass: result 2 wants a number, got a string");
    }

    SECTION("a whole number that does not fit") {
        REQUIRE(mentions(env->call<int>("pass", 1.5).error(), "result 1 wants a whole number"));
    }

    SECTION("an optional of the wrong type") {
        REQUIRE(mentions(env->call<std::optional<double>>("pass", "text").error(), "result 1 wants a number"));
    }

    SECTION("a value list result that cannot cross") {
        REQUIRE(mentions(env->call<value_list_type>("pass", 1.0, *env->read_reference("pass")).error(), "result 2 is a function"));
    }

    SECTION("what the script raised") {
        REQUIRE(mentions(env->call<double>("fails").error(), "the bucket leaks"));
        REQUIRE(mentions(env->call("fails"), "the bucket leaks"));
    }

    SECTION("nothing at the path") {
        REQUIRE(mentions(env->call<double>("missing").error(), "missing holds a nil, not a function"));
        REQUIRE(mentions(env->call<double>("missing.deeper").error(), "missing.deeper does not resolve"));
    }

    SECTION("not a function") {
        REQUIRE(mentions(env->call("not_a_function"), "not_a_function holds a number, not a function"));
    }

    SECTION("an argument from another interpreter") {
        interpreter other;

        REQUIRE_FALSE(other.run("t = {}"));

        REQUIRE(mentions(env->call("pass", *other.read_reference("t")), "another interpreter"));
    }

    SECTION("the stack is left as it was found") {
        for (int i = 0; i < 10000; ++i) {
            REQUIRE(*env->call<double>("pass", 1.0) == 1.0);
            REQUIRE_FALSE(env->call<double>("pass", "text"));
            REQUIRE(env->call("fails"));
        }
    }
}

TEST_CASE("a typed call works on everything that can be called", "[call][typed]") {
    interpreter lua;

    auto env = lua.make_environment();

    REQUIRE_FALSE(env->run("function double_it(x) return x * 2 end"));
    REQUIRE_FALSE(lua.run("function triple(x) return x * 3 end"));

    SECTION("an interpreter") {
        REQUIRE(*lua.call<double>("triple", 2) == 6.0);
        REQUIRE_FALSE(lua.call("triple", 2));
    }

    SECTION("a reference") {
        auto held = *env->read_reference("double_it");

        REQUIRE(*held.call<double>(2) == 4.0);
        REQUIRE_FALSE(held.call(2));
    }

    SECTION("a reference to what is not a function") {
        REQUIRE_FALSE(env->run("t = {}"));

        REQUIRE(mentions(env->read_reference("t")->call(), "the reference holds a table, not a function"));
    }

    SECTION("a function_reference") {
        std::optional<function_reference> held;

        env->register_function("later", [&](function_reference aFunction) { held = std::move(aFunction); });

        REQUIRE_FALSE(env->run("later(double_it)"));

        REQUIRE(*held->call<double>(2) == 4.0);
        REQUIRE_FALSE(held->call(2));
    }

    SECTION("from inside a typed function") {
        env->register_function("twice", [&](const double aValue) { return *env->call<double>("double_it", aValue); });

        REQUIRE_FALSE(env->run("out = twice(4)"));
        REQUIRE(env->read_number("out") == 8.0);
    }

    SECTION("from a coroutine, through a typed function") {
        env->register_function("twice", [&](const double aValue) { return *env->call<double>("double_it", aValue); });

        REQUIRE_FALSE(env->run("function errand() return twice(5) end"));

        auto co = env->make_coroutine("errand");

        value_list_type out;

        REQUIRE_FALSE(co->resume({}, out));
        REQUIRE(std::get<double>(out.at(0)) == 10.0);
    }
}

TEST_CASE("a typed call keeps to its budgets", "[call][typed][limits]") {
    SECTION("instructions, on an environment") {
        interpreter lua;

        auto env = lua.make_environment(environment_policy{standard_library::safe, 1000});

        REQUIRE_FALSE(env->run("function spin() for i = 1, 10000000 do end return 1 end"));

        REQUIRE(mentions(env->call<double>("spin").error(), "budget"));
    }

    SECTION("instructions, on a function_reference") {
        interpreter lua;

        auto env = lua.make_environment(environment_policy{standard_library::safe, 1000});

        std::optional<function_reference> held;

        env->register_function("later", [&](function_reference aFunction) { held = std::move(aFunction); });

        REQUIRE_FALSE(env->run("later(function() for i = 1, 10000000 do end end)"));

        REQUIRE(mentions(held->call<void>(), "budget"));
        REQUIRE(mentions(held->call<double>().error(), "budget"));
    }

    SECTION("memory") {
        interpreter lua(interpreter_policy{8u << 20});

        auto env = lua.make_environment();

        REQUIRE_FALSE(env->run("function pass(a) return a end"));

        REQUIRE(mentions(env->call("pass", std::string(16u << 20, 'x')), "memory"));
        REQUIRE(*env->call<double>("pass", 1.0) == 1.0);
    }
}

TEST_CASE("the value list calls are untouched", "[call][typed]") {
    interpreter lua;

    auto env = lua.make_environment();

    REQUIRE_FALSE(env->run("function count(...) return select('#', ...) end"));

    value_list_type results;

    SECTION("a list in braces") {
        REQUIRE_FALSE(env->call("count", {1.0, 2.0}, results));
        REQUIRE(std::get<double>(results.at(0)) == 2.0);
        REQUIRE_FALSE(env->call("count", {1.0}));
    }

    SECTION("a list in a variable") {
        value_list_type arguments{1.0, 2.0, 3.0};

        REQUIRE_FALSE(env->call("count", arguments, results));
        REQUIRE(std::get<double>(results.at(0)) == 3.0);
        REQUIRE_FALSE(env->call("count", arguments));
    }

    SECTION("on a reference") {
        auto held = *env->read_reference("count");

        REQUIRE_FALSE(held.call({1.0}, results));
        REQUIRE(std::get<double>(results.at(0)) == 1.0);
        REQUIRE_FALSE(held.call());
    }
}
