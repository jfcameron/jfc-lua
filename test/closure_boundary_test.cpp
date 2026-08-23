// © Joseph Cameron - All Rights Reserved

#include <jfc/catch.hpp>

#include <jfc/lua.h>

#include <stdexcept>
#include <string>

using namespace jfc::lua;

TEST_CASE("what a script does wrong comes back as a lua error", "[closure]") {
    interpreter interp;

    auto env = interp.make_environment();

    env->register_function("takes", [](value_list_type) -> value_list_type { return {}; });

    SECTION("an argument of a type that cannot cross, named and numbered") {
        const auto error = env->run("takes(print)");

        REQUIRE(error.has_value());

        INFO("message was: " << *error);

        REQUIRE(error->find("argument 1") != std::string::npos);
        REQUIRE(error->find("function") != std::string::npos);
    }

    SECTION("the position is reported, not just the reason") {
        const auto error = env->run("takes(1, 2, print)");

        REQUIRE(error.has_value());

        INFO("message was: " << *error);

        REQUIRE(error->find("argument 3") != std::string::npos);
        REQUIRE(error->find("function") != std::string::npos);
    }

    SECTION("a table argument holding something unstorable") {
        const auto error = env->run("takes({ f = function() end })");

        REQUIRE(error.has_value());

        INFO("message was: " << *error);

        REQUIRE(error->find("function") != std::string::npos);
    }

    SECTION("and the script can catch it like any other lua error") {
        REQUIRE_FALSE(env->run("ok, message = pcall(function() takes(print) end)").has_value());

        REQUIRE(env->read_boolean("ok") == false);
        REQUIRE(env->read_string("message").has_value());
    }
}

TEST_CASE("what the caller's own closure throws comes back as a lua error", "[closure]") {
    interpreter interp;

    auto env = interp.make_environment();

    env->register_function("explodes", [](value_list_type) -> value_list_type {
        throw std::runtime_error("the caller's own message");
    });

    env->register_function("throws_an_int", [](value_list_type) -> value_list_type {
        throw 7;
    });

    SECTION("a std::exception keeps its message") {
        const auto error = env->run("explodes()");

        REQUIRE(error.has_value());
        REQUIRE(error->find("the caller's own message") != std::string::npos);
    }

    SECTION("something that is not a std::exception is still reported") {
        const auto error = env->run("throws_an_int()");

        REQUIRE(error.has_value());

        INFO("message was: " << *error);

        REQUIRE(error->find("not a std::exception") != std::string::npos);
    }
}

TEST_CASE("the interpreter survives, and keeps working", "[closure]") {
    interpreter interp;

    auto env = interp.make_environment();
    auto bystander = interp.make_environment();

    REQUIRE_FALSE(bystander->run("hp = 100").has_value());

    env->register_function("explodes", [](value_list_type) -> value_list_type {
        throw std::runtime_error("deliberate");
    });

    env->register_function("adds", [](value_list_type args) -> value_list_type {
        double total = 0;

        for (const auto &arg : args) total += std::get<double>(arg);

        return {total};
    });

    for (int i = 0; i < 200; ++i) REQUIRE(env->run("explodes()").has_value());

    REQUIRE_FALSE(env->run("sum = adds(1, 2, 3)").has_value());
    REQUIRE(env->read_number("sum") == 6);

    REQUIRE(bystander->read_number("hp") == 100);
}

TEST_CASE("the ordinary path is untouched", "[closure]") {
    interpreter interp;

    auto env = interp.make_environment();

    env->register_function("echo", [](value_list_type args) -> value_list_type { return args; });

    REQUIRE_FALSE(env->run(
        "n, b, s = echo(42, true, 'text')").has_value());

    REQUIRE(env->read_number("n") == 42);
    REQUIRE(env->read_boolean("b") == true);
    REQUIRE(env->read_string("s") == "text");

    env->register_function("build", [](value_list_type) -> value_list_type {
        data_table out;

        out.set("name", "jim");
        out.set(1, "first");

        return {out};
    });

    REQUIRE_FALSE(env->run("t = build()  name = t.name  first = t[1]").has_value());

    REQUIRE(env->read_string("name") == "jim");
    REQUIRE(env->read_string("first") == "first");
}

TEST_CASE("a value's type is what lua says it is, not what it would convert to", "[closure][types]") {
    interpreter interp;

    auto env = interp.make_environment();

    REQUIRE_FALSE(env->run("s = '5'  n = 7  t = { s = '5', n = 7 }").has_value());

    SECTION("a string that looks like a number is not a number") {
        REQUIRE_FALSE(env->read_number("s").has_value());
        REQUIRE(env->read_string("s") == "5");
    }

    SECTION("a number is not a string") {
        REQUIRE_FALSE(env->read_string("n").has_value());
        REQUIRE(env->read_number("n") == 7);
    }

    SECTION("and the two ways of reading one value agree") {
        const auto t = env->read_data_table("t");

        REQUIRE(t.has_value());

        REQUIRE(env->read_number("t.s").has_value() == t->get_number("s").has_value());
        REQUIRE(env->read_string("t.s").has_value() == t->get_string("s").has_value());
        REQUIRE(env->read_number("t.n").has_value() == t->get_number("n").has_value());
        REQUIRE(env->read_string("t.n").has_value() == t->get_string("n").has_value());
    }

    SECTION("an argument arrives as the type the script passed") {
        env->register_function("kind", [](value_list_type args) -> value_list_type {
            const auto &first = args.at(0);

            return {std::string(first.index() == 0 ? "number"
                              : first.index() == 1 ? "boolean"
                              : first.index() == 2 ? "string" : "other")};
        });

        REQUIRE_FALSE(env->run("a = kind('5')  b = kind(5)  c = kind('text')  d = kind(true)")
            .has_value());

        REQUIRE(env->read_string("a") == "string");
        REQUIRE(env->read_string("b") == "number");
        REQUIRE(env->read_string("c") == "string");
        REQUIRE(env->read_string("d") == "boolean");
    }

    SECTION("a list of numeric strings is not a list of numbers") {
        REQUIRE_FALSE(env->run("mixed = { '1', '2' }").has_value());

        REQUIRE_FALSE(env->read_vector<double>("mixed").has_value());
        REQUIRE(env->read_vector<std::string>("mixed").has_value());
    }
}

TEST_CASE("nil crosses in both directions", "[closure][nil]") {
    interpreter interp;

    auto env = interp.make_environment();

    env->register_function("kind", [](value_list_type args) -> value_list_type {
        std::string out;

        for (const auto &arg : args)
            out += std::visit([](auto &&value) -> std::string {
                using value_type = std::decay_t<decltype(value)>;

                if constexpr (std::is_same_v<value_type, double>) return "number ";
                else if constexpr (std::is_same_v<value_type, bool>) return "boolean ";
                else if constexpr (std::is_same_v<value_type, std::string>) return "string ";
                else if constexpr (std::is_same_v<value_type, data_table>) return "table ";
                else return "nil ";
            }, arg);

        return {out};
    });

    SECTION("a nil argument arrives as one") {
        REQUIRE_FALSE(env->run("seen = kind(nil)").has_value());

        REQUIRE(env->read_string("seen") == "nil ");
    }

    SECTION("including in the middle of a call, where it is an omitted argument") {
        REQUIRE_FALSE(env->run("seen = kind(1, nil, 'x')").has_value());

        REQUIRE(env->read_string("seen") == "number nil string ");
    }

    SECTION("a returned nil arrives in lua as nil") {
        env->register_function("nothing", [](value_list_type) -> value_list_type { return {nullptr}; });

        REQUIRE_FALSE(env->run("v = nothing()  kind_of_v = type(v)").has_value());

        REQUIRE(env->read_string("kind_of_v") == "nil");
    }

    SECTION("mixed with real returns, in order") {
        env->register_function("triple", [](value_list_type) -> value_list_type
        {
            return {1.0, nullptr, std::string("last")};
        });

        REQUIRE_FALSE(env->run(
            "a, b, c = triple()\n"
            "shape = type(a) .. ' ' .. type(b) .. ' ' .. type(c)").has_value());

        REQUIRE(env->read_string("shape") == "number nil string");
    }

    SECTION("and a data_table still refuses nil, which is not the same question") {
        REQUIRE_FALSE(env->run("t = { a = 1, b = nil }").has_value());

        const auto t = env->read_data_table("t");

        REQUIRE(t.has_value());
        REQUIRE(t->size() == 1);
        REQUIRE_FALSE(t->contains("b"));
    }
}
