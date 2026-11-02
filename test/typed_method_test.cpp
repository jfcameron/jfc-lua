// © Joseph Cameron - MIT License

#include <jfc/catch.hpp>

#include <jfc/lua.h>

#include <chrono>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>

using namespace jfc::lua;

namespace {
    struct label final {
        std::string text;
        double size = 1;
    };

    struct button final {};

    [[nodiscard]] bool mentions(const error_type &aError, const std::string &aText) {
        return aError && aError->find(aText) != std::string::npos;
    }

    void register_label(interpreter &aLua) {
        aLua.register_type<label>("label",
            method("set_text", [](label &aLabel, const std::string_view aText) { aLabel.text = aText; }),
            method("text", [](const label &aLabel) { return aLabel.text; }),
            method("resize", [](label &aLabel, const double aSize, const std::optional<double> aScale) {
                aLabel.size = aSize * aScale.value_or(1);
            }),
            method("measure", [](const label &aLabel) { return std::tuple{aLabel.text.size(), aLabel.size}; }),
            method("fail", [](label &) { throw std::runtime_error("the label will not"); }),
            method("throw_an_int", [](label &) { throw 1; }));
    }
}

TEST_CASE("a typed method reads what its signature says", "[typed][methods]") {
    interpreter lua;

    register_label(lua);

    auto env = lua.make_environment();

    const auto greeting = std::make_shared<label>();

    env->write("greeting", greeting);

    SECTION("it works on the c++ object itself") {
        REQUIRE_FALSE(env->run("greeting:set_text('hello')"));
        REQUIRE(greeting->text == "hello");
    }

    SECTION("a const object") {
        greeting->text = "well";

        REQUIRE_FALSE(env->run("out = greeting:text()"));
        REQUIRE(env->read_string("out") == "well");
    }

    SECTION("an optional argument") {
        REQUIRE_FALSE(env->run("greeting:resize(2)"));
        REQUIRE(greeting->size == 2.0);

        REQUIRE_FALSE(env->run("greeting:resize(2, 3)"));
        REQUIRE(greeting->size == 6.0);
    }

    SECTION("several results") {
        greeting->text = "well";

        REQUIRE_FALSE(env->run("length, size = greeting:measure()"));
        REQUIRE(env->read_number("length") == 4.0);
        REQUIRE(env->read_number("size") == 1.0);
    }

    SECTION("called with a dot, given the object") {
        REQUIRE_FALSE(env->run("greeting.set_text(greeting, 'dotted')"));
        REQUIRE(greeting->text == "dotted");
    }

    SECTION("a table_view and a position past the object") {
        lua.register_type<button>("button", method("sum", [](button &, const double aFirst, const table_view &aTable) {
            return aFirst + *aTable.get_number("x");
        }));

        env->write("pressed", std::make_shared<button>());

        REQUIRE_FALSE(env->run("out = pressed:sum(1, { x = 2 })"));
        REQUIRE(env->read_number("out") == 3.0);
    }

    SECTION("a mutable lambda keeps its state") {
        lua.register_type<button>("button", method("count", [calls = 0](button &) mutable { return ++calls; }));

        env->write("pressed", std::make_shared<button>());

        REQUIRE_FALSE(env->run("pressed:count() out = pressed:count()"));
        REQUIRE(env->read_number("out") == 2.0);
    }

    SECTION("a type with no methods") {
        lua.register_type<button>("button");

        env->write("pressed", std::make_shared<button>());

        REQUIRE_FALSE(env->run("out = tostring(pressed):sub(1, 6)"));
        REQUIRE(env->read_string("out") == "button");
    }
}

TEST_CASE("a typed method's errors count from the first argument after the object", "[typed][methods]") {
    interpreter lua;

    register_label(lua);

    auto env = lua.make_environment();

    env->write("greeting", std::make_shared<label>());

    SECTION("the wrong type") {
        REQUIRE(mentions(env->run("greeting:set_text(1)"), "label:set_text: argument 1 wants a string, got a number"));
    }

    SECTION("too few") {
        REQUIRE(mentions(env->run("greeting:set_text()"), "argument 1 wants a string, got no value"));
    }

    SECTION("too many") {
        REQUIRE(mentions(env->run("greeting:set_text('a', 'b')"), "takes at most 1 argument, got 2"));
        REQUIRE(mentions(env->run("greeting:text(1)"), "takes no arguments, got 1"));
    }

    SECTION("called on anything but its own type") {
        REQUIRE(mentions(env->run("greeting.set_text('a', 'b')"), "set_text is a method of label"));
    }

    SECTION("a std::exception") {
        REQUIRE(mentions(env->run("greeting:fail()"), "the label will not"));
    }

    SECTION("anything else names the method") {
        REQUIRE(mentions(env->run("greeting:throw_an_int()"), "label:throw_an_int threw something that is not a std::exception"));
    }

    SECTION("the call timer hears of it") {
        std::string heard;

        lua.set_call_timer([&](const std::string_view aName, std::chrono::nanoseconds) { heard = aName; });

        REQUIRE_FALSE(env->run("greeting:set_text('a')"));
        REQUIRE(heard == "label:set_text");
    }
}

TEST_CASE("an object given to a typed method is not the object it was called on", "[typed][methods]") {
    interpreter lua;

    lua.register_type<label>("label");

    lua.register_type<button>("button",
        method("kept", [](button &, const std::shared_ptr<label> &aLabel) { return aLabel->size; }),
        method("held", [](button &, const userdata &aObject) { return aObject.get<label>()->size; }));

    auto env = lua.make_environment();

    const auto greeting = std::make_shared<label>();

    greeting->size = 7;

    env->write("greeting", greeting);
    env->write("pressed", std::make_shared<button>());

    REQUIRE_FALSE(env->run("a = pressed:kept(greeting) b = pressed:held(greeting)"));
    REQUIRE(env->read_number("a") == 7.0);
    REQUIRE(env->read_number("b") == 7.0);
    REQUIRE(mentions(env->run("pressed:kept(pressed)"), "argument 1 wants a label, got a button"));
}

TEST_CASE("a type's methods can be typed one at a time", "[typed][methods]") {
    interpreter lua;

    lua.register_type<label>("label",
        method("typed", [](label &aLabel, const std::string_view aText) { aLabel.text = aText; }),
        method("listed", [](label &aLabel, value_list_type aArguments) -> value_list_type {
            aLabel.text = std::get<std::string>(aArguments.at(0));

            return {static_cast<double>(aArguments.size())};
        }));

    auto env = lua.make_environment();

    const auto greeting = std::make_shared<label>();

    env->write("greeting", greeting);

    REQUIRE_FALSE(env->run("greeting:typed('a')"));
    REQUIRE(greeting->text == "a");

    REQUIRE_FALSE(env->run("out = greeting:listed('b', 2, 3)"));
    REQUIRE(greeting->text == "b");
    REQUIRE(env->read_number("out") == 3.0);
}

TEST_CASE("a `__call` method can be typed", "[typed][methods][call]") {
    interpreter lua;

    lua.register_type<label>("label", method("__call", [](label &aLabel, const double aSize) {
        aLabel.size = aSize;

        return aSize * 2;
    }));

    auto env = lua.make_environment();

    env->write("greeting", std::make_shared<label>());

    REQUIRE_FALSE(env->run("out = greeting(4)"));
    REQUIRE(env->read_number("out") == 8.0);
}

TEST_CASE("the methods of a value list still work as they did", "[typed][methods]") {
    interpreter lua;

    lua.register_type<label>("label", {
        {"count", [](label &, value_list_type aArguments) -> value_list_type {
            return {static_cast<double>(aArguments.size())};
        }},
    });

    auto env = lua.make_environment();

    env->write("greeting", std::make_shared<label>());

    REQUIRE_FALSE(env->run("out = greeting:count(1, 'two', nil)"));
    REQUIRE(env->read_number("out") == 3.0);
    REQUIRE(mentions(env->run("greeting:count(print)"), "argument 1 is a function, which cannot cross into c++"));
}

TEST_CASE("a trailing value list takes every argument left", "[typed][rest]") {
    interpreter lua;

    auto env = lua.make_environment();

    env->register_function("after", [](const std::string_view aName, value_list_type aRest) {
        return std::tuple{std::string(aName), aRest.size()};
    });

    SECTION("with arguments left") {
        REQUIRE_FALSE(env->run("name, count = after('a', 1, true, 'x')"));
        REQUIRE(env->read_string("name") == "a");
        REQUIRE(env->read_number("count") == 3.0);
    }

    SECTION("with none left") {
        REQUIRE_FALSE(env->run("name, count = after('a')"));
        REQUIRE(env->read_number("count") == 0.0);
    }

    SECTION("what comes before it is still checked") {
        REQUIRE(mentions(env->run("after(1, 2)"), "after: argument 1 wants a string, got a number"));
    }

    SECTION("and what cannot cross is numbered from the start") {
        REQUIRE(mentions(env->run("after('a', 1, print)"), "argument 3 is a function, which cannot cross into c++"));
    }

    SECTION("the same for a method") {
        lua.register_type<label>("label", method("after", [](label &, const double, const value_list_type &aRest) {
            return aRest.size();
        }));

        env->write("greeting", std::make_shared<label>());

        REQUIRE_FALSE(env->run("out = greeting:after(1, 2, 3)"));
        REQUIRE(env->read_number("out") == 2.0);
        REQUIRE(mentions(env->run("greeting:after(1, print)"), "argument 2 is a function"));
    }
}

TEST_CASE("a function given to a method keeps the budget of the script that gave it", "[typed][methods][limits]") {
    interpreter lua;

    std::optional<function_reference> held;

    lua.register_type<label>("label", method("later", [&](label &, function_reference aFunction) {
        held = std::move(aFunction);
    }));

    auto env = lua.make_environment(environment_policy{standard_library::safe, 1000});

    env->write("greeting", std::make_shared<label>());

    REQUIRE_FALSE(env->run(R"(
        greeting:later(function()
            for i = 1, 10000000 do end
        end)
    )"));

    REQUIRE(mentions(held->call(), "budget"));
}
