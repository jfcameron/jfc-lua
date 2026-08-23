// © Joseph Cameron - All Rights Reserved

#include <jfc/catch.hpp>

#include <jfc/lua.h>

#include <string>
#include <vector>

using namespace jfc::lua;

TEST_CASE("a dense array reads as a vector", "[array]") {
    interpreter interp;

    auto env = interp.make_environment();

    REQUIRE_FALSE(env->run(
        "names = { 'jim', 'robert', 'alice' }\n"
        "hp = { 100, 80, 60 }\n"
        "alive = { true, false, true }\n"
        "nothing = {}").has_value());

    SECTION("of strings") {
        const auto names = env->read_vector<std::string>("names");

        REQUIRE(names.has_value());
        REQUIRE(*names == std::vector<std::string>{"jim", "robert", "alice"});
    }

    SECTION("of numbers") {
        const auto hp = env->read_vector<double>("hp");

        REQUIRE(hp.has_value());
        REQUIRE(*hp == std::vector<double>{100, 80, 60});
    }

    SECTION("of booleans") {
        const auto alive = env->read_vector<bool>("alive");

        REQUIRE(alive.has_value());
        REQUIRE(*alive == std::vector<bool>{true, false, true});
    }

    SECTION("an empty table is an empty array rather than nothing") {
        const auto empty = env->read_vector<std::string>("nothing");

        REQUIRE(empty.has_value());
        REQUIRE(empty->empty());
    }

    SECTION("through a path, not only at the top") {
        REQUIRE_FALSE(env->run("npc = { inventory = { 'sword', 'shield' } }").has_value());

        const auto inventory = env->read_vector<std::string>("npc.inventory");

        REQUIRE(inventory.has_value());
        REQUIRE(*inventory == std::vector<std::string>{"sword", "shield"});
    }
}

TEST_CASE("anything that is not a dense array reads as nothing", "[array]") {
    interpreter interp;

    auto env = interp.make_environment();

    SECTION("a sparse table") {
        REQUIRE_FALSE(env->run("t = { [1] = 'a', [3] = 'b' }").has_value());

        REQUIRE_FALSE(env->read_vector<std::string>("t").has_value());
    }

    SECTION("one that starts at zero, since lua lists start at one") {
        REQUIRE_FALSE(env->run("t = { [0] = 'a', [1] = 'b' }").has_value());

        REQUIRE_FALSE(env->read_vector<std::string>("t").has_value());
    }

    SECTION("one carrying a key of another kind as well") {
        REQUIRE_FALSE(env->run("t = { 'a', 'b', named = 'c' }").has_value());

        REQUIRE_FALSE(env->read_vector<std::string>("t").has_value());
    }

    SECTION("one whose elements are not all the type asked for") {
        REQUIRE_FALSE(env->run("t = { 'a', 2, 'c' }").has_value());

        REQUIRE_FALSE(env->read_vector<std::string>("t").has_value());
        REQUIRE_FALSE(env->read_vector<double>("t").has_value());
    }

    SECTION("something that is not a table") {
        REQUIRE_FALSE(env->run("scalar = 7").has_value());

        REQUIRE_FALSE(env->read_vector<double>("scalar").has_value());
    }

    SECTION("a path that leads nowhere") {
        REQUIRE_FALSE(env->read_vector<std::string>("no.such.list").has_value());
    }
}

TEST_CASE("a vector writes as a lua list", "[array]") {
    interpreter interp;

    auto env = interp.make_environment();

    SECTION("of strings, and lua agrees about its length") {
        env->write("names", std::vector<std::string>{"jim", "robert"});

        REQUIRE_FALSE(env->run(
            "length = #names\n"
            "first = names[1]\n"
            "second = names[2]\n"
            "kind = type(names)").has_value());

        REQUIRE(env->read_number("length") == 2);
        REQUIRE(env->read_string("first") == "jim");
        REQUIRE(env->read_string("second") == "robert");
        REQUIRE(env->read_string("kind") == "table");
    }

    SECTION("of numbers") {
        env->write("spawns", std::vector<double>{1, 2, 3});

        REQUIRE_FALSE(env->run("total = spawns[1] + spawns[2] + spawns[3]").has_value());
        REQUIRE(env->read_number("total") == 6);
    }

    SECTION("of booleans") {
        env->write("flags", std::vector<bool>{true, false});

        REQUIRE_FALSE(env->run("a = tostring(flags[1])  b = tostring(flags[2])").has_value());
        REQUIRE(env->read_string("a") == "true");
        REQUIRE(env->read_string("b") == "false");
    }

    SECTION("an empty one is an empty table, not an absent one") {
        env->write("none", std::vector<double>{});

        REQUIRE_FALSE(env->run("kind = type(none)  length = #none").has_value());
        REQUIRE(env->read_string("kind") == "table");
        REQUIRE(env->read_number("length") == 0);
    }

    SECTION("and at a nested path") {
        env->write("npc.inventory", std::vector<std::string>{"sword"});

        REQUIRE_FALSE(env->run("item = npc.inventory[1]").has_value());
        REQUIRE(env->read_string("item") == "sword");
    }
}

TEST_CASE("a vector round trips", "[array]") {
    interpreter interp;

    auto env = interp.make_environment();

    const std::vector<std::string> names{"jim", "robert", "alice"};
    const std::vector<double> hp{100, 80.5, -3};
    const std::vector<bool> alive{true, false, true};

    env->write("names", names);
    env->write("hp", hp);
    env->write("alive", alive);

    REQUIRE(env->read_vector<std::string>("names") == names);
    REQUIRE(env->read_vector<double>("hp") == hp);
    REQUIRE(env->read_vector<bool>("alive") == alive);
}

TEST_CASE("the interpreter's own globals do arrays too", "[array]") {
    interpreter interp;

    interp.write("spawns", std::vector<double>{1, 2, 3});

    REQUIRE(interp.read_vector<double>("spawns") == std::vector<double>{1, 2, 3});
}
