// © Joseph Cameron - MIT License

#include <jfc/catch.hpp>

#include <jfc/lua.h>

#include <chrono>
#include <cstdint>
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
    struct lantern final { int lit = 0; };
    struct bucket final { int held = 0; };

    double plain_add(const double aLeft, const double aRight) { return aLeft + aRight; }

    [[nodiscard]] bool mentions(const error_type &aError, const std::string &aText) {
        return aError && aError->find(aText) != std::string::npos;
    }
}

TEST_CASE("a typed function reads what its signature says", "[typed]") {
    interpreter interp;

    auto env = interp.make_environment();

    SECTION("numbers") {
        env->register_function("add", [](const double aLeft, const float aRight) { return aLeft + aRight; });

        REQUIRE_FALSE(env->run("out = add(1.5, 2.5)"));
        REQUIRE(env->read_number("out") == 4.0);
    }

    SECTION("booleans") {
        env->register_function("flip", [](const bool aValue) { return !aValue; });

        REQUIRE_FALSE(env->run("out = flip(false)"));
        REQUIRE(env->read_boolean("out") == true);
    }

    SECTION("strings, by value and by reference") {
        env->register_function("by_value", [](std::string aText) { return aText + "!"; });
        env->register_function("by_reference", [](const std::string &aText) { return aText + "?"; });

        REQUIRE_FALSE(env->run("a = by_value('well') b = by_reference('well')"));
        REQUIRE(env->read_string("a") == "well!");
        REQUIRE(env->read_string("b") == "well?");
    }

    SECTION("a string_view sees the whole string, whatever it holds") {
        std::size_t length = 0;

        env->register_function("measure", [&](const std::string_view aText) { length = aText.size(); });

        REQUIRE_FALSE(env->run("measure('a\\0b')"));
        REQUIRE(length == 3);
    }

    SECTION("a data_table") {
        env->register_function("field", [](const data_table &aTable) { return aTable.get_number("a"); });

        REQUIRE_FALSE(env->run("out = field({ a = 7 })"));
        REQUIRE(env->read_number("out") == 7.0);
    }

    SECTION("a userdata") {
        env->write("object", std::make_shared<lantern>());
        env->register_function("empty", [](const userdata &aObject) { return aObject.empty(); });

        REQUIRE_FALSE(env->run("out = empty(object)"));
        REQUIRE(env->read_boolean("out") == false);
    }

    SECTION("a shared_ptr to a bound object") {
        const auto light = std::make_shared<lantern>();

        env->write("object", light);
        env->register_function("light", [](std::shared_ptr<lantern> aLantern) { aLantern->lit = 1; });

        REQUIRE_FALSE(env->run("light(object)"));
        REQUIRE(light->lit == 1);
    }

    SECTION("no arguments at all") {
        int called = 0;

        env->register_function("call", [&] { ++called; });

        REQUIRE_FALSE(env->run("call()"));
        REQUIRE(called == 1);
    }
}

TEST_CASE("a typed function can be anything callable", "[typed]") {
    interpreter interp;

    auto env = interp.make_environment();

    SECTION("a function pointer") {
        env->register_function("add", plain_add);

        REQUIRE_FALSE(env->run("out = add(1, 2)"));
        REQUIRE(env->read_number("out") == 3.0);
    }

    SECTION("a mutable lambda keeps its state between calls") {
        env->register_function("count", [calls = 0]() mutable { return ++calls; });

        REQUIRE_FALSE(env->run("count() out = count()"));
        REQUIRE(env->read_number("out") == 2.0);
    }

    SECTION("a noexcept lambda") {
        env->register_function("one", []() noexcept { return 1; });

        REQUIRE_FALSE(env->run("out = one()"));
        REQUIRE(env->read_number("out") == 1.0);
    }

    SECTION("on the interpreter itself") {
        interp.register_function("two", [] { return 2; });

        REQUIRE_FALSE(interp.run("out = two()"));
        REQUIRE(interp.read_number("out") == 2.0);
    }
}

TEST_CASE("the value list route is untouched", "[typed]") {
    interpreter interp;

    auto env = interp.make_environment();

    SECTION("a closure over a value list still gets every argument") {
        env->register_function("count", [](value_list_type aArguments) -> value_list_type {
            return {static_cast<double>(aArguments.size())};
        });

        REQUIRE_FALSE(env->run("out = count(1, 'two', true, nil)"));
        REQUIRE(env->read_number("out") == 4.0);
    }

    SECTION("and one taking it by reference") {
        env->register_function("count", [](const value_list_type &aArguments) -> value_list_type {
            return {static_cast<double>(aArguments.size())};
        });

        REQUIRE_FALSE(env->run("out = count(1, 2)"));
        REQUIRE(env->read_number("out") == 2.0);
    }
}

TEST_CASE("a whole number is checked for what it will be cast to", "[typed]") {
    interpreter interp;

    auto env = interp.make_environment();

    long long seen = 0;

    env->register_function("small", [&](const std::int8_t aValue) { seen = aValue; });
    env->register_function("count", [&](const std::uint8_t aValue) { seen = aValue; });
    env->register_function("wide", [&](const std::int64_t aValue) { seen = aValue; });

    SECTION("whole values arrive") {
        REQUIRE_FALSE(env->run("small(-128)"));
        REQUIRE(seen == -128);

        REQUIRE_FALSE(env->run("count(255)"));
        REQUIRE(seen == 255);
    }

    SECTION("the widest type takes its lowest value") {
        REQUIRE_FALSE(env->run("wide(-2^63)"));
        REQUIRE(seen == INT64_MIN);
    }

    SECTION("a fraction is refused and shown") {
        const auto error = env->run("small(1.5)");

        REQUIRE(mentions(error, "a whole number"));
        REQUIRE(mentions(error, "1.5"));
    }

    SECTION("a value past the top is refused, and the range named") {
        REQUIRE(mentions(env->run("small(128)"), "from -128 to 127"));
        REQUIRE(mentions(env->run("count(256)"), "from 0 to 255"));
    }

    SECTION("a value past the bottom is refused") {
        REQUIRE(mentions(env->run("small(-129)"), "from -128 to 127"));
        REQUIRE(mentions(env->run("count(-1)"), "from 0 to 255"));
    }

    SECTION("the first double past the widest type is refused") {
        REQUIRE(mentions(env->run("wide(2^63)"), "a whole number"));
    }

    SECTION("not a number is refused") {
        REQUIRE(mentions(env->run("small(0/0)"), "a whole number"));
    }

    SECTION("nor are the infinities") {
        REQUIRE(mentions(env->run("small(1/0)"), "a whole number"));
        REQUIRE(mentions(env->run("small(-1/0)"), "a whole number"));
    }
}

TEST_CASE("an optional argument may be left out", "[typed]") {
    interpreter interp;

    auto env = interp.make_environment();

    env->register_function("greet", [](const std::string_view aName, const std::optional<std::string_view> aTitle) {
        return std::string(aTitle.value_or("friend")) + " " + std::string(aName);
    });

    SECTION("left off the end") {
        REQUIRE_FALSE(env->run("out = greet('wren')"));
        REQUIRE(env->read_string("out") == "friend wren");
    }

    SECTION("given as nil") {
        REQUIRE_FALSE(env->run("out = greet('wren', nil)"));
        REQUIRE(env->read_string("out") == "friend wren");
    }

    SECTION("given") {
        REQUIRE_FALSE(env->run("out = greet('wren', 'captain')"));
        REQUIRE(env->read_string("out") == "captain wren");
    }

    SECTION("given as the wrong type, which is still an error") {
        REQUIRE(mentions(env->run("greet('wren', 1)"), "argument 2 wants a string"));
    }
}

TEST_CASE("a typed function's results go back as what they are", "[typed]") {
    interpreter interp;

    auto env = interp.make_environment();

    SECTION("nothing") {
        env->register_function("nothing", [] {});

        REQUIRE_FALSE(env->run("out = select('#', nothing())"));
        REQUIRE(env->read_number("out") == 0.0);
    }

    SECTION("whole numbers") {
        env->register_function("some", [] { return std::uint32_t{7}; });

        REQUIRE_FALSE(env->run("out = some()"));
        REQUIRE(env->read_number("out") == 7.0);
    }

    SECTION("booleans stay booleans") {
        env->register_function("yes", [] { return true; });

        REQUIRE_FALSE(env->run("out = yes()"));
        REQUIRE(env->read_boolean("out") == true);
    }

    SECTION("text of every kind") {
        env->register_function("owned", [] { return std::string("a"); });
        env->register_function("viewed", [] { return std::string_view("b"); });
        env->register_function("literal", [] { return "c"; });

        REQUIRE_FALSE(env->run("out = owned() .. viewed() .. literal()"));
        REQUIRE(env->read_string("out") == "abc");
    }

    SECTION("a string_view of an argument") {
        env->register_function("same", [](const std::string_view aText) { return aText; });

        REQUIRE_FALSE(env->run("out = same('kept')"));
        REQUIRE(env->read_string("out") == "kept");
    }

    SECTION("a null literal is nil") {
        env->register_function("none", []() -> const char * { return nullptr; });

        REQUIRE_FALSE(env->run("out = none() == nil"));
        REQUIRE(env->read_boolean("out") == true);
    }

    SECTION("an empty optional is nil, and a full one its value") {
        env->register_function("find", [](const bool aFound) -> std::optional<double> {
            if (aFound) return 3.0;

            return {};
        });

        REQUIRE_FALSE(env->run("missing = find(false) == nil found = find(true)"));
        REQUIRE(env->read_boolean("missing") == true);
        REQUIRE(env->read_number("found") == 3.0);
    }

    SECTION("a tuple is several results, in order") {
        env->register_function("many", [] { return std::tuple{1.0, std::string("two"), true}; });

        REQUIRE_FALSE(env->run("a, b, c = many()"));
        REQUIRE(env->read_number("a") == 1.0);
        REQUIRE(env->read_string("b") == "two");
        REQUIRE(env->read_boolean("c") == true);
    }

    SECTION("a tuple longer than a new coroutine's stack") {
        env->register_function("long", [] {
            const auto ten = std::tuple{1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
            const auto fifty = std::tuple_cat(ten, ten, ten, ten, ten);

            return std::tuple_cat(fifty, fifty, fifty, fifty);
        });

        REQUIRE_FALSE(env->run("out = coroutine.wrap(function() return select('#', long()) end)()"));
        REQUIRE(env->read_number("out") == 200.0);
    }

    SECTION("a value list, however long it turned out") {
        env->register_function("repeated", [](const int aCount) {
            return value_list_type(static_cast<std::size_t>(aCount), value_list_type::value_type{1.0});
        });

        REQUIRE_FALSE(env->run("out = select('#', repeated(50))"));
        REQUIRE(env->read_number("out") == 50.0);
    }

    SECTION("a data_table") {
        env->register_function("made", [] {
            data_table out;

            out.set("a", 1.0);

            return out;
        });

        REQUIRE_FALSE(env->run("out = made().a"));
        REQUIRE(env->read_number("out") == 1.0);
    }

    SECTION("a bound object is the lua value it already was") {
        const auto light = std::make_shared<lantern>();

        env->write("object", light);
        env->register_function("same", [&] { return light; });

        REQUIRE_FALSE(env->run("out = same() == object"));
        REQUIRE(env->read_boolean("out") == true);
    }

    SECTION("a null object is nil") {
        env->register_function("none", [] { return std::shared_ptr<lantern>(); });

        REQUIRE_FALSE(env->run("out = none() == nil"));
        REQUIRE(env->read_boolean("out") == true);
    }
}

TEST_CASE("a wrong call says who was called, where, and what was wanted", "[typed]") {
    interpreter interp;

    auto env = interp.make_environment();

    env->register_function("world.gas.set", [](const double, const std::string_view) {});

    SECTION("the wrong type") {
        const auto error = env->run("world.gas.set(1, 2)");

        REQUIRE(mentions(error, "world.gas.set"));
        REQUIRE(mentions(error, "argument 2 wants a string, got a number"));
    }

    SECTION("a number is not a string, nor a string a number") {
        REQUIRE(mentions(env->run("world.gas.set('1', 'a')"), "argument 1 wants a number, got a string"));
    }

    SECTION("nil") {
        REQUIRE(mentions(env->run("world.gas.set(nil, 'a')"), "argument 1 wants a number, got nil"));
    }

    SECTION("too few") {
        REQUIRE(mentions(env->run("world.gas.set(1)"), "argument 2 wants a string, got no value"));
    }

    SECTION("too many") {
        REQUIRE(mentions(env->run("world.gas.set(1, 'a', 3)"), "takes at most 2 arguments, got 3"));
    }

    SECTION("too many, for one that takes one or none") {
        env->register_function("one", [](const double) {});
        env->register_function("none", [] {});

        REQUIRE(mentions(env->run("one(1, 2)"), "takes at most 1 argument, got 2"));
        REQUIRE(mentions(env->run("none(1)"), "takes no arguments, got 1"));
    }

    SECTION("the first wrong argument is the one reported") {
        REQUIRE(mentions(env->run("world.gas.set('a', 1)"), "argument 1"));
    }

    SECTION("a boolean is not a number") {
        REQUIRE(mentions(env->run("world.gas.set(true, 'a')"), "got a boolean"));
    }

    SECTION("a script can catch it") {
        REQUIRE_FALSE(env->run("ok, why = pcall(world.gas.set, 1)"));
        REQUIRE(env->read_boolean("ok") == false);
        REQUIRE(env->read_string("why")->find("argument 2") != std::string::npos);
    }

    SECTION("and a function was not called on bad arguments") {
        int called = 0;

        env->register_function("once", [&](const double) { ++called; });

        REQUIRE(env->run("once('a')"));
        REQUIRE(called == 0);
    }
}

TEST_CASE("a typed function takes the object it asks for, and names any other", "[typed]") {
    interpreter interp;

    interp.register_type<lantern>("lantern", {});
    interp.register_type<bucket>("bucket", {});

    auto env = interp.make_environment();

    env->register_function("light", [](const std::shared_ptr<lantern> &aLantern) { aLantern->lit = 1; });

    env->write("light_source", std::make_shared<lantern>());
    env->write("pail", std::make_shared<bucket>());

    SECTION("the right one") {
        REQUIRE_FALSE(env->run("light(light_source)"));
    }

    SECTION("another registered type") {
        REQUIRE(mentions(env->run("light(pail)"), "argument 1 wants a lantern, got a bucket"));
    }

    SECTION("nil") {
        REQUIRE(mentions(env->run("light(nil)"), "argument 1 wants a lantern, got nil"));
    }

    SECTION("a table") {
        REQUIRE(mentions(env->run("light({})"), "argument 1 wants a lantern, got a table"));
    }

    SECTION("a type never registered is still refused") {
        env->register_function("fill", [](const std::shared_ptr<std::string> &) {});

        REQUIRE(mentions(env->run("fill(pail)"), "argument 1 wants a different object, got a bucket"));
    }

    SECTION("an optional object may be nil") {
        env->register_function("maybe", [](const std::optional<std::shared_ptr<lantern>> &aLantern) {
            return aLantern.has_value();
        });

        REQUIRE_FALSE(env->run("a = maybe() b = maybe(light_source)"));
        REQUIRE(env->read_boolean("a") == false);
        REQUIRE(env->read_boolean("b") == true);
        REQUIRE(mentions(env->run("maybe(pail)"), "wants a lantern"));
    }
}

TEST_CASE("a typed function shares the variant route's boundary", "[typed]") {
    interpreter interp;

    auto env = interp.make_environment();

    SECTION("a std::exception becomes a lua error, and the interpreter carries on") {
        env->register_function("fails", []() -> double { throw std::runtime_error("the well is dry"); });

        REQUIRE(mentions(env->run("fails()"), "the well is dry"));
        REQUIRE_FALSE(env->run("ok = pcall(fails)"));
        REQUIRE(env->read_boolean("ok") == false);
    }

    SECTION("anything else is reported under the function's name") {
        env->register_function("throws_an_int", [] { throw 1; });

        const auto error = env->run("throws_an_int()");

        REQUIRE(mentions(error, "throws_an_int"));
        REQUIRE(mentions(error, "not a std::exception"));
    }

    SECTION("a table that cannot cross") {
        env->register_function("takes", [](const data_table &) {});

        REQUIRE(mentions(env->run("takes({ f = function() end })"), "function"));
    }

    SECTION("the call timer hears of it, by name") {
        std::string heard;

        interp.set_call_timer([&](const std::string_view aName, std::chrono::nanoseconds) { heard = aName; });

        env->register_function("timed.call", [] {});

        REQUIRE_FALSE(env->run("timed.call()"));
        REQUIRE(heard == "timed.call");
    }

    SECTION("a call into lua from inside one") {
        env->register_function("twice", [&](const double aValue) {
            value_list_type results;

            REQUIRE_FALSE(env->call("double_it", {aValue}, results));

            return std::get<double>(results.at(0));
        });

        REQUIRE_FALSE(env->run("function double_it(x) return x * 2 end out = twice(4)"));
        REQUIRE(env->read_number("out") == 8.0);
    }

    SECTION("the stack is left as it was found") {
        env->register_function("fails", []() -> double { throw std::runtime_error("no"); });
        env->register_function("works", [](const double aValue) { return aValue; });

        REQUIRE_FALSE(env->run(R"(
            for i = 1, 100000 do
                works(i)
                pcall(fails)
            end
        )"));
    }

    SECTION("the object lives past a failed call") {
        const auto light = std::make_shared<lantern>();

        env->write("object", light);
        env->register_function("fails", [](std::shared_ptr<lantern>) { throw std::runtime_error("no"); });

        const auto owners = light.use_count();

        REQUIRE(env->run("fails(object)"));
        REQUIRE(light.use_count() == owners);
    }
}

TEST_CASE("a typed result past the memory budget is a lua error", "[typed][limits]") {
    interpreter interp(interpreter_policy{8u << 20});

    auto env = interp.make_environment();

    env->register_function("huge", [](const std::string_view aPrefix) {
        return std::string(aPrefix) + std::string(16u << 20, 'x');
    });

    REQUIRE(env->run("out = huge('a long prefix that does not fit in a small string')"));
    REQUIRE_FALSE(env->run("out = 1 + 1"));
    REQUIRE(env->read_number("out") == 2.0);
}

TEST_CASE("a typed function can suspend the coroutine calling it", "[typed]") {
    interpreter interp;

    auto env = interp.make_environment();

    int called = 0;

    env->register_yielding_function("wait", [&](const double aSeconds, const std::string_view aWhere) {
        ++called;

        return std::tuple{aSeconds, std::string(aWhere)};
    });

    SECTION("its results are what the coroutine yields") {
        REQUIRE_FALSE(env->run(R"(
            function errand()
                local late = wait(2, "at the well")
                return "back", late
            end
        )"));

        auto co = env->make_coroutine("errand");

        value_list_type out;

        REQUIRE_FALSE(co->resume({}, out));

        REQUIRE(called == 1);
        REQUIRE(out.size() == 2);
        REQUIRE(std::get<double>(out[0]) == 2.0);
        REQUIRE(std::get<std::string>(out[1]) == "at the well");

        REQUIRE_FALSE(co->resume({true}, out));

        REQUIRE(out.size() == 2);
        REQUIRE(std::get<std::string>(out[0]) == "back");
        REQUIRE(std::get<bool>(out[1]) == true);
    }

    SECTION("outside a coroutine it says so") {
        REQUIRE(mentions(env->run("wait(1, 'here')"), "only a coroutine can wait"));
        REQUIRE(called == 0);
    }

    SECTION("a wrong call is an error, not a suspension") {
        REQUIRE_FALSE(env->run("function errand() wait('soon', 'here') end"));

        auto co = env->make_coroutine("errand");

        value_list_type out;

        REQUIRE(mentions(co->resume({}, out), "argument 1 wants a number"));
        REQUIRE(called == 0);
    }
}
