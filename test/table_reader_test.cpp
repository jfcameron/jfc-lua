// © Joseph Cameron - MIT License

#include <jfc/catch.hpp>

#include <jfc/lua.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

using namespace jfc::lua;

namespace {
    [[nodiscard]] data_table table_of(const std::string &aText) { return data_table::from_string(aText); }

    template <typename work_type>
    [[nodiscard]] std::string refusal(const work_type &aWork) {
        try {
            aWork();
        }
        catch (const exception &e) {
            return e.what();
        }

        FAIL("nothing was refused");

        return {};
    }
}

TEST_CASE("a reader takes only the fields it is given, and names them refusing another", "[table_reader]") {
    const auto good = table_of(R"({ version = 1, label = "oak" })");

    REQUIRE_NOTHROW(table_reader(good, "tree.lua", {"version", "label", "height"}));

    REQUIRE(refusal([] { table_reader(table_of(R"({ label = "oak", hieght = 3 })"), "tree.lua", {"label", "height"}); })
        == "tree.lua has no field \"hieght\": it takes label and height");

    REQUIRE(refusal([] { table_reader(table_of(R"({ "a list" })"), "tree.lua", {"label"}); })
        == "tree.lua has no field 1: it takes label");

    REQUIRE(refusal([] { table_reader(table_of(R"({ x = 1 })"), "", {}); })
        == "the table has no field \"x\": it takes nothing");
}

TEST_CASE("a field is read as the kind asked for, or refused saying what it holds", "[table_reader]") {
    const auto table = table_of(R"({ label = "oak", height = 3.5, evergreen = false, leaves = { colour = "green" } })");
    const table_reader read(table, "tree.lua", {"label", "height", "evergreen", "leaves", "fruit"});

    REQUIRE(read.get<std::string>("label") == "oak");
    REQUIRE(read.get<double>("height") == 3.5);
    REQUIRE(read.get<float>("height") == 3.5f);
    REQUIRE(read.get<bool>("evergreen") == false);
    REQUIRE(read.get<data_table>("leaves").get_string("colour") == "green");

    SECTION("an optional is nothing where the field is absent, and the kind where it is not") {
        REQUIRE_FALSE(read.get<std::optional<std::string>>("fruit"));
        REQUIRE(read.get<std::optional<std::string>>("label") == "oak");
        REQUIRE(refusal([&] { (void)read.get<std::optional<double>>("label"); })
            == "tree.lua: label wants a number, got a string");
    }

    SECTION("anything else is the wrong kind, and absent is nil") {
        REQUIRE(refusal([&] { (void)read.get<double>("label"); }) == "tree.lua: label wants a number, got a string");
        REQUIRE(refusal([&] { (void)read.get<std::string>("height"); })
            == "tree.lua: height wants a string, got a number");
        REQUIRE(refusal([&] { (void)read.get<bool>("leaves"); }) == "tree.lua: leaves wants a boolean, got a table");
        REQUIRE(refusal([&] { (void)read.get<data_table>("evergreen"); })
            == "tree.lua: evergreen wants a table, got a boolean");
        REQUIRE(refusal([&] { (void)read.get<std::string>("fruit"); }) == "tree.lua: fruit wants a string, got nil");
    }

    SECTION("a field it was not given is a mistake in the reading, not the table") {
        REQUIRE(refusal([&] { (void)read.get<double>("width"); })
            == "tree.lua was asked for \"width\", not one of its fields: label, height, evergreen, leaves and fruit");
    }
}

TEST_CASE("a number is read within its range, and a whole number within its type's too", "[table_reader]") {
    const auto table = table_of(R"({ half = 0.5, two = 2, big = 300, minus = -1, nan = 0/0, far = 1/0, version = 2 })");
    const table_reader read(table, "lamp.lua", {"half", "two", "big", "minus", "nan", "far", "version"});

    REQUIRE(read.get<double>("half", {.least = 0, .most = 1}) == 0.5);
    REQUIRE(refusal([&] { (void)read.get<double>("two", {.least = 0, .most = 1}); })
        == "lamp.lua: two wants a number from 0 to 1, got 2");
    REQUIRE(refusal([&] { (void)read.get<double>("minus", {.least = 0}); })
        == "lamp.lua: minus wants a number of at least 0, got -1");
    REQUIRE(refusal([&] { (void)read.get<double>("two", {.most = 1.5}); })
        == "lamp.lua: two wants a number of at most 1.5, got 2");
    REQUIRE(refusal([&] { (void)read.get<double>("nan"); }) == "lamp.lua: nan wants a finite number, got 0/0");
    REQUIRE(refusal([&] { (void)read.get<double>("far", {.least = 0}); })
        == "lamp.lua: far wants a finite number of at least 0, got 1/0");
    REQUIRE(refusal([&] { (void)read.get<int>("far"); })
        == "lamp.lua: far wants a whole number from -2147483648 to 2147483647, got 1/0");

    REQUIRE(read.get<int>("two") == 2);
    REQUIRE(read.get<std::uint8_t>("two") == 2);
    REQUIRE(refusal([&] { (void)read.get<int>("half"); })
        == "lamp.lua: half wants a whole number from -2147483648 to 2147483647, got 0.5");
    REQUIRE(refusal([&] { (void)read.get<std::uint8_t>("big"); })
        == "lamp.lua: big wants a whole number from 0 to 255, got 300");
    REQUIRE(refusal([&] { (void)read.get<unsigned>("minus"); })
        == "lamp.lua: minus wants a whole number from 0 to 4294967295, got -1");
    REQUIRE(refusal([&] { (void)read.get<int>("big", {.least = 1, .most = 16}); })
        == "lamp.lua: big wants a whole number from 1 to 16, got 300");
    REQUIRE(refusal([&] { (void)read.get<int>("version", {.least = 1, .most = 1}); })
        == "lamp.lua: version wants 1, got 2");
}

TEST_CASE("a list is read whole, each of it as the kind asked for", "[table_reader]") {
    const auto table = table_of(R"({ colour = { 1, 0.5, 0 }, names = { "a", "b" }, mixed = { 1, "two" },
        keyed = { 1, 2, x = 3 }, bright = { 1, 2, 0 } })");
    const table_reader read(table, "sky.lua", {"colour", "names", "mixed", "keyed", "bright", "none"});

    REQUIRE(read.get<std::vector<double>>("colour") == std::vector<double>{1, 0.5, 0});
    REQUIRE(read.get<std::vector<std::string>>("names") == std::vector<std::string>{"a", "b"});
    REQUIRE((read.get<std::array<float, 3>>("colour", {.least = 0, .most = 1}) == std::array<float, 3>{1, 0.5f, 0}));
    REQUIRE_FALSE(read.get<std::optional<std::vector<double>>>("none"));

    REQUIRE(refusal([&] { (void)read.get<std::vector<double>>("mixed"); })
        == "sky.lua: mixed[2] wants a number, got a string");
    REQUIRE(refusal([&] { (void)read.get<std::vector<double>>("keyed"); })
        == "sky.lua: keyed wants a list, got a table with the key \"x\"");
    REQUIRE(refusal([&] { (void)read.get<std::array<double, 4>>("colour"); })
        == "sky.lua: colour wants a list of 4, got a list of 3");
    REQUIRE(refusal([&] { (void)read.get<std::vector<double>>("bright", {.least = 0, .most = 1}); })
        == "sky.lua: bright[2] wants a number from 0 to 1, got 2");
    REQUIRE(refusal([&] { (void)read.get<std::vector<double>>("names"); })
        == "sky.lua: names[1] wants a number, got a string");
}

TEST_CASE("a table in a table is read by a reader of its own, said by its path", "[table_reader]") {
    const auto table = table_of(R"({
        sun = { elevation = "high" },
        phases = { { hour = 6 }, { hour = "noon" } },
        moon = { phase = 1, tide = 2 } })");
    const table_reader read(table, "sky.lua", {"sun", "phases", "moon", "stars", "comet"});

    const auto sun = read.table("sun", {"elevation", "azimuth"});

    REQUIRE(sun);
    REQUIRE_FALSE(sun->get<std::optional<double>>("azimuth"));
    REQUIRE(refusal([&] { (void)sun->get<double>("elevation"); })
        == "sky.lua: sun.elevation wants a number, got a string");

    const auto phases = read.tables("phases", {"hour"});

    REQUIRE(phases.size() == 2);
    REQUIRE(phases[0].get<int>("hour") == 6);
    REQUIRE(refusal([&] { (void)phases[1].get<int>("hour"); })
        == "sky.lua: phases[2].hour wants a whole number from -2147483648 to 2147483647, got a string");

    REQUIRE(refusal([&] { (void)read.table("moon", {"phase"}); })
        == "sky.lua: moon has no field \"tide\": it takes phase");
    REQUIRE(refusal([&] { (void)read.tables("sun", {"elevation"}); })
        == "sky.lua: sun wants a list, got a table with the key \"elevation\"");
    REQUIRE(refusal([&] { (void)read.table("phases", {"hour"}); }) == "sky.lua: phases has no field 1: it takes hour");

    REQUIRE_FALSE(read.table("comet", {"tail"}));
    REQUIRE(read.tables("stars", {"name"}).empty());

    SECTION("and keeps its table when the reader that made it is gone") {
        std::optional<table_reader> kept;

        {
            const auto made = table_of(R"({ sun = { elevation = 30 } })");
            const table_reader first(made, "sky.lua", {"sun"});

            kept = first.table("sun", {"elevation"});
        }

        REQUIRE(kept->get<double>("elevation") == 30);
    }
}

TEST_CASE("a refusal of the format's own is said after where the reader is", "[table_reader]") {
    const auto table = table_of(R"({ phases = { { hour = 6 } } })");
    const table_reader read(table, "sky.lua", {"phases"});

    REQUIRE(std::string(read.refused("it has no dusk").what()) == "sky.lua: it has no dusk");
    REQUIRE(std::string(read.tables("phases", {"hour"})[0].refused("its hour is too early").what())
        == "sky.lua: phases[1]: its hour is too early");
}
