// © Joseph Cameron - All Rights Reserved

#include <jfc/catch.hpp>

#include <jfc/lua.h>

#include <sstream>
#include <string>
#include <vector>

using namespace jfc::lua;

namespace {
    [[nodiscard]] std::string serialize(const data_table &a) {
        std::ostringstream out;
        out << a;

        return out.str();
    }

    [[nodiscard]] std::vector<double> number_keys(const data_table &a) {
        std::vector<double> out;

        for (const auto &k : a.keys()) if (k.is_number()) out.push_back(std::get<double>(k.value()));

        return out;
    }
}

TEST_CASE("the dense run grows, and is indistinguishable from the map", "[dense]") {
    data_table t;

    SECTION("appending one at a time") {
        for (int i = 1; i <= 5; ++i) t.set(i, static_cast<double>(i * 10));

        REQUIRE(t.size() == 5);
        REQUIRE(number_keys(t) == std::vector<double>{1, 2, 3, 4, 5});
        REQUIRE(t.get_number(3) == 30);
    }

    SECTION("overwriting within the run") {
        t.set(1, 1.0);
        t.set(2, 2.0);
        t.set(1, 99.0);

        REQUIRE(t.size() == 2);
        REQUIRE(t.get_number(1) == 99);
    }

    SECTION("a key past the end stays sparse until the gap is filled") {
        t.set(1, 1.0);
        t.set(3, 3.0);

        REQUIRE(t.size() == 2);
        REQUIRE(number_keys(t) == std::vector<double>{1, 3});

        // filling the hole should absorb what was beyond it
        t.set(2, 2.0);

        REQUIRE(t.size() == 3);
        REQUIRE(number_keys(t) == std::vector<double>{1, 2, 3});
        REQUIRE(t.get_number(3) == 3);
    }

    SECTION("absorbing is not merely an optimisation: without it a key lands in both containers") {
        t.set(1, 1.0);
        t.set(3, 30.0);
        t.set(2, 2.0);
        t.set(3, 99.0);

        REQUIRE(t.size() == 3);
        REQUIRE(number_keys(t) == std::vector<double>{1, 2, 3});
        REQUIRE(t.get_number(3) == 99);
    }

    SECTION("and absorbing may cascade") {
        t.set(1, 1.0);
        t.set(3, 3.0);
        t.set(4, 4.0);
        t.set(5, 5.0);

        REQUIRE(number_keys(t) == std::vector<double>{1, 3, 4, 5});

        t.set(2, 2.0);

        REQUIRE(t.size() == 5);
        REQUIRE(number_keys(t) == std::vector<double>{1, 2, 3, 4, 5});

        for (int i = 1; i <= 5; ++i) REQUIRE(t.get_number(i) == i);
    }
}

TEST_CASE("keys that are not array slots stay out of the run", "[dense]") {
    data_table t;

    t.set(1, 1.0);
    t.set(2, 2.0);

    SECTION("zero and negatives") {
        t.set(0, 0.0);
        t.set(-1, -1.0);

        REQUIRE(t.get_number(0) == 0);
        REQUIRE(t.get_number(-1) == -1);
        REQUIRE(number_keys(t) == std::vector<double>{-1, 0, 1, 2});
    }

    SECTION("fractions, which fall between slots") {
        t.set(1.5, 15.0);

        REQUIRE(t.get_number(1.5) == 15);
        REQUIRE(t.get_number(1) == 1);
        REQUIRE(number_keys(t) == std::vector<double>{1, 1.5, 2});
    }

    SECTION("and they never become slots by accident") {
        t.set(0.0, 99.0);

        REQUIRE(t.get_number(0) == 99);
        REQUIRE(t.get_number(1) == 1);
        REQUIRE(t.size() == 3);
    }
}

TEST_CASE("erasing from the run keeps every other key reachable", "[dense]") {
    data_table t;

    for (int i = 1; i <= 5; ++i) t.set(i, static_cast<double>(i));

    SECTION("from the end") {
        t.erase(5);

        REQUIRE(t.size() == 4);
        REQUIRE(number_keys(t) == std::vector<double>{1, 2, 3, 4});
        REQUIRE_FALSE(t.contains(5));
    }

    SECTION("from the middle: the hole ends the run, and the tail survives") {
        t.erase(3);

        REQUIRE(t.size() == 4);
        REQUIRE_FALSE(t.contains(3));
        REQUIRE(number_keys(t) == std::vector<double>{1, 2, 4, 5});

        REQUIRE(t.get_number(4) == 4);
        REQUIRE(t.get_number(5) == 5);
    }

    SECTION("and the run can be rebuilt afterwards") {
        t.erase(3);
        t.set(3, 33.0);

        REQUIRE(t.size() == 5);
        REQUIRE(number_keys(t) == std::vector<double>{1, 2, 3, 4, 5});
        REQUIRE(t.get_number(3) == 33);
    }

    SECTION("from the front") {
        t.erase(1);

        REQUIRE(t.size() == 4);
        REQUIRE_FALSE(t.contains(1));
        REQUIRE(number_keys(t) == std::vector<double>{2, 3, 4, 5});
        REQUIRE(t.get_number(2) == 2);
    }
}

TEST_CASE("where a key is stored is invisible from outside", "[dense]") {
    interpreter interp;

    auto env = interp.make_environment();

    SECTION("a mixture round trips through lua unchanged") {
        data_table original;

        original.set(1, "one");
        original.set(2, "two");
        original.set(4, "four");        // sparse
        original.set(-1, "minus one");
        original.set(0.5, "a half");
        original.set("named", "by name");
        original.set(true, "yes");

        env->write("sent", original);

        const auto returned = env->read_data_table("sent");

        REQUIRE(returned.has_value());
        REQUIRE(returned->size() == original.size());

        for (const auto &k : original.keys()) {
            REQUIRE(returned->contains(k));
            REQUIRE(returned->get_string(k) == original.get_string(k));
        }
    }

    SECTION("serialisation is the same whichever container a key came from") {
        data_table dense;
        dense.set(1, 1.0);
        dense.set(2, 2.0);

        data_table sparse;
        sparse.set(2, 2.0);            // lands in the map
        sparse.set(1, 1.0);            // then absorbs it

        REQUIRE(serialize(dense) == serialize(sparse));
        REQUIRE(serialize(dense) == "{[1]=1,[2]=2}");
    }

    SECTION("a lua list read in and written back is still a list") {
        REQUIRE_FALSE(env->run("list = { 'a', 'b', 'c' }").has_value());

        const auto read = env->read_data_table("list");

        REQUIRE(read.has_value());

        env->write("copy", *read);

        REQUIRE_FALSE(env->run("n = #copy  same = copy[1] == 'a' and copy[3] == 'c'").has_value());

        REQUIRE(env->read_number("n") == 3);
        REQUIRE(env->read_boolean("same") == true);
    }

    SECTION("and a sparse one is still sparse") {
        REQUIRE_FALSE(env->run("holes = { [1] = 'a', [3] = 'c' }").has_value());

        const auto read = env->read_data_table("holes");

        REQUIRE(read.has_value());
        REQUIRE(read->size() == 2);

        env->write("copy", *read);

        REQUIRE_FALSE(env->run("one = copy[1]  two = tostring(copy[2])  three = copy[3]").has_value());

        REQUIRE(env->read_string("one") == "a");
        REQUIRE(env->read_string("two") == "nil");
        REQUIRE(env->read_string("three") == "c");
    }
}
