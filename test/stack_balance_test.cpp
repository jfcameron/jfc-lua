// © Joseph Cameron - All Rights Reserved

#include <jfc/catch.hpp>

#include <jfc/lua.h>

#include <string>

using namespace jfc::lua;

namespace {
    constexpr int PAST_THE_CEILING = 80000;

    template <typename work_type>
    [[nodiscard]] int count_agreements(work_type aWork) {
        int agreed = 0;

        for (int i = 0; i < PAST_THE_CEILING; ++i) if (aWork(i)) ++agreed;

        return agreed;
    }
}

TEST_CASE("reading does not grow the lua stack", "[stack]") {
    interpreter interp;

    REQUIRE_FALSE(interp.run("number_value = 42\n"
        "string_value = 'hello'\n"
        "bool_value = true\n"
        "table_value = { 1, 2, 3 }\n"
        "nested = { inner = { leaf = 7 } }").has_value());

    SECTION("a number, repeatedly") {
        REQUIRE(count_agreements([&](int) {
            const auto value = interp.read_number("number_value");

            return value.has_value() && *value == 42;
        }) == PAST_THE_CEILING);
    }

    SECTION("a string, repeatedly") {
        REQUIRE(count_agreements([&](int) { return interp.read_string("string_value").has_value(); })
            == PAST_THE_CEILING);
    }

    SECTION("a boolean, repeatedly") {
        REQUIRE(count_agreements([&](int) { return interp.read_boolean("bool_value").has_value(); })
            == PAST_THE_CEILING);
    }

    SECTION("a table, repeatedly") {
        REQUIRE(count_agreements([&](int) { return interp.read_data_table("table_value").has_value(); })
            == PAST_THE_CEILING);
    }

    SECTION("a nested path, repeatedly") {
        REQUIRE(count_agreements([&](int) { return interp.read_number("nested.inner.leaf").has_value(); })
            == PAST_THE_CEILING);
    }

    SECTION("a path that does not exist, repeatedly") {
        REQUIRE(count_agreements([&](int) { return !interp.read_number("no.such.path").has_value(); })
            == PAST_THE_CEILING);
    }

    SECTION("a read of the wrong type, repeatedly") {
        REQUIRE(count_agreements([&](int) { return !interp.read_number("string_value").has_value(); })
            == PAST_THE_CEILING);
    }
}

TEST_CASE("failing does not grow the lua stack", "[stack]") {
    interpreter interp;

    SECTION("a script that will not compile, repeatedly") {
        REQUIRE(count_agreements([&](int) { return interp.run("message = { 1 2, 3 }").has_value(); })
            == PAST_THE_CEILING);
    }

    SECTION("a script that compiles and then fails, repeatedly") {
        REQUIRE(count_agreements([&](int) { return interp.run("error('deliberate')").has_value(); })
            == PAST_THE_CEILING);
    }

    SECTION("syntax validation of bad input, repeatedly") {
        REQUIRE(count_agreements([&](int) {
            return interp.validate_syntax("message = { 1 2, 3 }").has_value(); }) == PAST_THE_CEILING);
    }

    SECTION("syntax validation of good input, repeatedly") {
        REQUIRE(count_agreements([&](int) {
            return !interp.validate_syntax("message = { 1, 2, 3 }").has_value(); }) == PAST_THE_CEILING);
    }
}

TEST_CASE("writing does not grow the lua stack", "[stack]") {
    interpreter interp;

    SECTION("a value, repeatedly") {
        for (int i = 0; i < PAST_THE_CEILING; ++i) interp.write("written", static_cast<double>(i));

        const auto value = interp.read_number("written");

        REQUIRE(value.has_value());
        REQUIRE(*value == PAST_THE_CEILING - 1);
    }

    SECTION("into a nested path, repeatedly") {
        REQUIRE_FALSE(interp.run("holder = { }").has_value());

        for (int i = 0; i < PAST_THE_CEILING; ++i) interp.write("holder.field", static_cast<double>(i));

        REQUIRE(interp.read_number("holder.field").has_value());
    }
}

TEST_CASE("an error is always describable", "[stack]") {
    interpreter interp;

    const auto error = interp.run("message = { 1 2, 3 }");

    REQUIRE(error.has_value());
    REQUIRE_FALSE(error->empty());
}

TEST_CASE("running against an environment does not grow the lua stack", "[stack][environment]") {
    interpreter interp;

    auto env = interp.make_environment();

    REQUIRE_FALSE(env->run("number_value = 42\n"
        "string_value = 'hello'\n"
        "nested = { inner = { leaf = 7 } }").has_value());

    SECTION("reading a number, repeatedly") {
        REQUIRE(count_agreements([&](int) {
            const auto value = env->read_number("number_value");

            return value.has_value() && *value == 42;
        }) == PAST_THE_CEILING);
    }

    SECTION("reading a nested path, repeatedly") {
        REQUIRE(count_agreements([&](int) { return env->read_number("nested.inner.leaf").has_value(); })
            == PAST_THE_CEILING);
    }

    SECTION("reading a path that does not exist, repeatedly") {
        REQUIRE(count_agreements([&](int) { return !env->read_number("no.such.path").has_value(); })
            == PAST_THE_CEILING);
    }

    SECTION("writing, repeatedly") {
        REQUIRE(count_agreements([&](int i) {
            env->write("written", static_cast<double>(i));

            const auto value = env->read_number("written");

            return value.has_value() && *value == static_cast<double>(i);
        }) == PAST_THE_CEILING);
    }

    SECTION("running a script, repeatedly") {
        REQUIRE(count_agreements([&](int) { return !env->run("number_value = 42").has_value(); })
            == PAST_THE_CEILING);
    }

    SECTION("a script that fails, repeatedly") {
        REQUIRE(count_agreements([&](int) { return env->run("error('deliberate')").has_value(); })
            == PAST_THE_CEILING);
    }

    SECTION("reading by a number key, repeatedly") {
        REQUIRE_FALSE(env->run("list = { 'jim', 'robert' }").has_value());

        REQUIRE(count_agreements([&](int) { return env->read_string("list[1]") == "jim"; })
            == PAST_THE_CEILING);
    }

    SECTION("reading a nested numeric path, repeatedly") {
        REQUIRE_FALSE(env->run("grid = { [2] = { [3] = 9 } }").has_value());

        REQUIRE(count_agreements([&](int) { return env->read_number("grid[2][3]") == 9; })
            == PAST_THE_CEILING);
    }

    SECTION("writing by a number key, repeatedly") {
        REQUIRE(count_agreements([&](int i) {
            env->write(path{"written", i % 8}, static_cast<double>(i));

            const auto value = env->read_number(path{"written", i % 8});

            return value.has_value() && *value == static_cast<double>(i);
        }) == PAST_THE_CEILING);
    }

    SECTION("a path that throws, repeatedly") {
        REQUIRE(count_agreements([&](int) {
            try { (void)env->read_number("a["); return false; }
            catch (const jfc::lua_exception &) { return true; }
        }) == PAST_THE_CEILING);
    }

    SECTION("alternating between two environments, repeatedly") {
        auto other = interp.make_environment();

        REQUIRE_FALSE(other->run("number_value = 7").has_value());

        REQUIRE(count_agreements([&](int i) {
            const auto &which = (i % 2) ? other : env;

            const auto value = which->read_number("number_value");

            return value.has_value() && *value == ((i % 2) ? 7 : 42);
        }) == PAST_THE_CEILING);
    }
}
