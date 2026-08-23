// © Joseph Cameron - All Rights Reserved

#include <jfc/catch.hpp>

#include <jfc/lua.h>

#include <string>

using namespace jfc::lua;

namespace {
    [[nodiscard]] environment_shared_ptr_type unrestricted(interpreter &aInterpreter) {
        return aInterpreter.make_environment(environment_policy{standard_library::unrestricted});
    }
}

TEST_CASE("a value that cannot be stored is refused, and named", "[storable]") {
    interpreter interp;

    auto env = unrestricted(interp);

    const auto refuses = [&env](const std::string &aSetup, const std::string &aExpected) {
        REQUIRE_FALSE(env->run(aSetup).has_value());

        try {
            const auto read = env->read_data_table("t");
            FAIL("expected a throw");
        }
        catch (const jfc::lua::exception &e) {
            INFO("message was: " << e.what());
            REQUIRE(std::string(e.what()).find(aExpected) != std::string::npos);
        }
    };

    SECTION("a function, named by its key") {
        refuses("t = { hp = 1, describe = function() end }", "describe holds a function");
    }

    SECTION("under a number key, which the message spells as lua would") {
        refuses("t = { function() end }", "[1] holds a function");
    }

    SECTION("nested, named by the whole path to it") {
        refuses("t = { inner = { deeper = { f = function() end } } }",
            "inner.deeper.f holds a function");
    }

    SECTION("the path names the culprit and nothing else") {
        REQUIRE_FALSE(env->run("t = { a = 1, b = 2, describe = function() end }").has_value());

        try {
            const auto read = env->read_data_table("t");

            FAIL("expected a throw");
        }
        catch (const jfc::lua::exception &e) {
            REQUIRE(std::string(e.what())
                == "table: describe holds a function, which cannot be stored");
        }
    }

    SECTION("a coroutine") {
        refuses("t = { co = coroutine.create(function() end) }", "co holds a thread");
    }

    SECTION("a key of an unstorable kind") {
        refuses("t = { [function() end] = 1 }", "key of type function");
    }
}

TEST_CASE("a table that reaches itself is refused rather than followed", "[storable]") {
    interpreter interp;

    auto env = interp.make_environment();

    SECTION("directly") {
        REQUIRE_FALSE(env->run("t = {} t.self = t").has_value());

        REQUIRE_THROWS_AS(env->read_data_table("t"), jfc::lua::exception);
    }

    SECTION("or round a longer loop") {
        REQUIRE_FALSE(env->run("t = { a = {} } t.a.back = t").has_value());

        REQUIRE_THROWS_AS(env->read_data_table("t"), jfc::lua::exception);
    }

    SECTION("but the same table twice in different branches is not a loop") {
        REQUIRE_FALSE(env->run("shared = { x = 1 } t = { a = shared, b = shared }").has_value());

        const auto read = env->read_data_table("t");

        REQUIRE(read.has_value());
        REQUIRE(read->get_data_table("a")->get_number("x") == 1);
        REQUIRE(read->get_data_table("b")->get_number("x") == 1);
    }
}

TEST_CASE("nesting past the limit is refused rather than exhausting the stack", "[storable]") {
    interpreter interp;

    auto env = interp.make_environment();

    const auto nest = [&env](const int aDepth) {
        return env->run("t = {} local c = t for i = 1, " + std::to_string(aDepth)
            + " do c.n = {} c = c.n end");
    };

    SECTION("well within it") {
        REQUIRE_FALSE(nest(64).has_value());

        REQUIRE(env->read_data_table("t").has_value());
    }

    SECTION("past it") {
        REQUIRE_FALSE(nest(10000).has_value());

        try {
            const auto read = env->read_data_table("t");

            FAIL("expected a throw");
        }
        catch (const jfc::lua::exception &e) {
            INFO("message was: " << e.what());

            REQUIRE(std::string(e.what()).find("nesting deeper than 128") != std::string::npos);
        }
    }

    SECTION("just past it, where nothing else would object") {
        REQUIRE_FALSE(nest(129).has_value());
        REQUIRE_THROWS_AS(env->read_data_table("t"), jfc::lua::exception);
    }
}

TEST_CASE("skip takes what can be stored and steps over the rest", "[storable]") {
    interpreter interp;

    auto env = unrestricted(interp);

    SECTION("the data survives and the function does not") {
        REQUIRE_FALSE(env->run("t = { hp = 100, name = 'jim', describe = function() end }")
            .has_value());

        const auto read = env->read_data_table("t", unsupported::skip);

        REQUIRE(read.has_value());
        REQUIRE(read->size() == 2);
        REQUIRE(read->get_number("hp") == 100);
        REQUIRE(read->get_string("name") == "jim");
        REQUIRE_FALSE(read->contains("describe"));
    }

    SECTION("nested, and under a key of an unstorable kind") {
        REQUIRE_FALSE(env->run(
            "t = { keep = 1, inner = { keep = 2, f = function() end }, [function() end] = 3 }")
            .has_value());

        const auto read = env->read_data_table("t", unsupported::skip);

        REQUIRE(read.has_value());
        REQUIRE(read->get_number("keep") == 1);
        REQUIRE(read->get_data_table("inner")->get_number("keep") == 2);
        REQUIRE_FALSE(read->get_data_table("inner")->contains("f"));
    }

    SECTION("a table reaching itself is stepped over too") {
        REQUIRE_FALSE(env->run("t = { hp = 1 } t.self = t").has_value());

        const auto read = env->read_data_table("t", unsupported::skip);

        REQUIRE(read.has_value());
        REQUIRE(read->get_number("hp") == 1);
        REQUIRE_FALSE(read->contains("self"));
    }

    SECTION("but depth is still refused, because that is about c++ rather than about the value") {
        REQUIRE_FALSE(env->run("t = {} local c = t for i = 1, 10000 do c.n = {} c = c.n end")
            .has_value());

        REQUIRE_THROWS_AS(env->read_data_table("t", unsupported::skip), jfc::lua::exception);
    }

    SECTION("and it is lossy on purpose: writing it back writes a table without the function") {
        REQUIRE_FALSE(env->run("t = { hp = 1, describe = function() return 'hello' end }")
            .has_value());

        const auto read = env->read_data_table("t", unsupported::skip);

        REQUIRE(read.has_value());

        env->write("copy", *read);

        REQUIRE_FALSE(env->run("kind = type(copy.describe)  hp = copy.hp").has_value());

        REQUIRE(env->read_string("kind") == "nil");
        REQUIRE(env->read_number("hp") == 1);
    }
}

TEST_CASE("reject is what happens unless skip is asked for", "[storable]") {
    interpreter interp;

    auto env = unrestricted(interp);

    REQUIRE_FALSE(env->run("t = { f = function() end }").has_value());

    REQUIRE_THROWS_AS(env->read_data_table("t"), jfc::lua::exception);
    REQUIRE_THROWS_AS(env->read_data_table("t", unsupported::reject), jfc::lua::exception);
    REQUIRE_NOTHROW(env->read_data_table("t", unsupported::skip));
}
