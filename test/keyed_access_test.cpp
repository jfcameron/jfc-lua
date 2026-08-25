// © Joseph Cameron - All Rights Reserved

#include <jfc/catch.hpp>

#include <jfc/lua.h>

#include <string>

using namespace jfc::lua;

TEST_CASE("a value can be read by a key of any type", "[keys]") {
    interpreter interp;

    auto env = interp.make_environment();

    REQUIRE_FALSE(env->run(
        "list = { 'jim', 'robert' }\n"
        "flags = { [true] = 'yes', [false] = 'no' }\n"
        "nested = { [1] = { name = 'inner' } }\n"
        "fraction = { [1.5] = 'half past one' }").has_value());

    SECTION("a number key") {
        REQUIRE(env->read_string("list[1]") == "jim");
        REQUIRE(env->read_string("list[2]") == "robert");
        REQUIRE_FALSE(env->read_string("list[3]").has_value());
    }

    SECTION("a boolean key") {
        REQUIRE(env->read_string("flags[true]") == "yes");
        REQUIRE(env->read_string("flags[false]") == "no");
    }

    SECTION("a non-integral number key, since lua has one number type") {
        REQUIRE(env->read_string("fraction[1.5]") == "half past one");
    }

    SECTION("through a nested numeric segment") {
        REQUIRE(env->read_string("nested[1].name") == "inner");
    }

    SECTION("and the typed form addresses the same value as the written one") {
        REQUIRE(env->read_string(path{"list", 1}) == "jim");
        REQUIRE(env->read_string(path{"nested", 1, "name"}) == "inner");
        REQUIRE(env->read_string(path{"flags", true}) == "yes");
    }

    SECTION("which is what makes a computed index possible at all") {
        for (int i = 1; i <= 2; ++i) {
            INFO("index " << i);

            REQUIRE(env->read_string(path{"list", i}).has_value());
        }
    }
}

TEST_CASE("a number key and the string that prints like it are different values", "[keys]") {
    interpreter interp;

    auto env = interp.make_environment();

    REQUIRE_FALSE(env->run("mixed = { [1] = 'the number one', ['1'] = 'the string one' }").has_value());

    REQUIRE(env->read_string("mixed[1]") == "the number one");
    REQUIRE(env->read_string("mixed.1") == "the string one");
    REQUIRE(env->read_string(path{"mixed", 1}) == "the number one");
    REQUIRE(env->read_string(path{"mixed", "1"}) == "the string one");
}

TEST_CASE("writing by a number key edits the list rather than a lookalike", "[keys]") {
    interpreter interp;

    auto env = interp.make_environment();

    REQUIRE_FALSE(env->run("list = { 'jim', 'robert' }").has_value());

    SECTION("the element really is replaced") {
        env->write("list[1]", "OVERWRITTEN");

        REQUIRE_FALSE(env->run(
            "one = tostring(list[1])\n"
            "lookalike = tostring(list['1'])\n"
            "length = #list").has_value());

        REQUIRE(env->read_string("one") == "OVERWRITTEN");
        REQUIRE(env->read_string("lookalike") == "nil");
        REQUIRE(env->read_number("length") == 2);
    }

    SECTION("and the dotted form still writes the string key, which is now the rule rather than a trap") {
        env->write("list.1", "BESIDE");

        REQUIRE_FALSE(env->run(
            "one = tostring(list[1])\n"
            "lookalike = tostring(list['1'])").has_value());

        REQUIRE(env->read_string("one") == "jim");
        REQUIRE(env->read_string("lookalike") == "BESIDE");
    }

    SECTION("a boolean key too") {
        env->write("flags[true]", "yes");

        REQUIRE_FALSE(env->run("v = tostring(flags[true])  s = tostring(flags['true'])").has_value());

        REQUIRE(env->read_string("v") == "yes");
        REQUIRE(env->read_string("s") == "nil");
    }
}

TEST_CASE("missing intermediate tables are built with the key they were addressed by", "[keys]") {
    interpreter interp;

    auto env = interp.make_environment();

    SECTION("a numeric segment") {
        env->write("grid[2][3]", 9.0);

        REQUIRE_FALSE(env->run("v = grid[2][3]  t = type(grid[2])").has_value());

        REQUIRE(env->read_number("v") == 9);
        REQUIRE(env->read_string("t") == "table");
    }

    SECTION("mixed with named ones") {
        env->write("npcs[1].stats.hp", 100.0);

        REQUIRE_FALSE(env->run("v = npcs[1].stats.hp").has_value());

        REQUIRE(env->read_number("v") == 100);
    }

    SECTION("and the named case still behaves as it did") {
        env->write("a.b.c", 1.0);

        REQUIRE(env->read_number("a.b.c") == 1);
    }
}

TEST_CASE("a path is walked the way lua walks one", "[keys]") {
    interpreter interp;

    auto env = interp.make_environment();

    SECTION("intermediate segments follow __index, so an inherited table can be read through") {
        REQUIRE(env->read_number("math.pi").has_value());
        REQUIRE(*env->read_number("math.pi") > 3.14);
        REQUIRE(*env->read_number("math.pi") < 3.15);
    }

    SECTION("a path through a value that is not a table finds nothing rather than failing") {
        REQUIRE_FALSE(env->run("scalar = 7").has_value());

        REQUIRE_FALSE(env->read_number("scalar.field").has_value());
        REQUIRE_FALSE(env->read_number("scalar[1]").has_value());
    }

    SECTION("a path that does not exist finds nothing") {
        REQUIRE_FALSE(env->read_number("no.such.path").has_value());
        REQUIRE_FALSE(env->read_number("no[1].such").has_value());
    }
}

TEST_CASE("writing to a library path still shadows rather than writing through", "[keys]") {
    interpreter interp;

    auto env = interp.make_environment();
    auto other = interp.make_environment();

    env->write("math.tau", 6.28);

    REQUIRE(env->read_number("math.tau") == 6.28);
    REQUIRE(env->read_number("math.pi").has_value());

    REQUIRE_FALSE(other->read_number("math.tau").has_value());
    REQUIRE(other->read_number("math.pi").has_value());
}

TEST_CASE("an unparseable path is reported rather than read as absent", "[keys]") {
    interpreter interp;

    auto env = interp.make_environment();

    REQUIRE_THROWS_AS(env->read_number("a["), jfc::lua::exception);
    REQUIRE_THROWS_AS(env->read_string("a[nope]"), jfc::lua::exception);
    REQUIRE_THROWS_AS(env->write("a..b", 1.0), jfc::lua::exception);

    REQUIRE_FALSE(env->run("ok = 1").has_value());
    REQUIRE(env->read_number("ok") == 1);
}
