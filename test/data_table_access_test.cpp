// © Joseph Cameron - All Rights Reserved

#include <jfc/catch.hpp>

#include <jfc/lua.h>

#include <algorithm>
#include <string>
#include <vector>

using namespace jfc::lua;

TEST_CASE("a table read from lua can be inspected", "[table][access]") {
    interpreter interp;

    auto env = interp.make_environment();

    REQUIRE_FALSE(env->run(
        "npc = { name = 'jim', hp = 100, alive = true,\n"
        "        [1] = 'first', [true] = 'flagged',\n"
        "        stats = { strength = 18 } }").has_value());

    const auto npc = env->read_data_table("npc");

    REQUIRE(npc.has_value());

    SECTION("by string key") {
        REQUIRE(npc->get_string("name") == "jim");
        REQUIRE(npc->get_number("hp") == 100);
        REQUIRE(npc->get_boolean("alive") == true);
    }

    SECTION("by number key") {
        REQUIRE(npc->get_string(1) == "first");
    }

    SECTION("by boolean key") {
        REQUIRE(npc->get_string(true) == "flagged");
    }

    SECTION("a subtable comes back as a table") {
        const auto stats = npc->get_data_table("stats");

        REQUIRE(stats);
        REQUIRE(stats->get_number("strength") == 18);
    }

    SECTION("asking for the wrong type gives nothing rather than a conversion") {
        REQUIRE_FALSE(npc->get_number("name").has_value());
        REQUIRE_FALSE(npc->get_string("hp").has_value());
        REQUIRE_FALSE(npc->get_boolean("name").has_value());
        REQUIRE_FALSE(npc->get_data_table("name"));
    }

    SECTION("asking for a key that is not there gives nothing") {
        REQUIRE_FALSE(npc->get_string("nope").has_value());
        REQUIRE_FALSE(npc->get_string(99).has_value());
        REQUIRE_FALSE(npc->get_string(false).has_value());
    }

    SECTION("and the keys that print alike stay apart") {
        REQUIRE(npc->get_string(1) == "first");
        REQUIRE_FALSE(npc->get_string("1").has_value());
    }
}

TEST_CASE("a table reports what it holds", "[table][access]") {
    interpreter interp;

    auto env = interp.make_environment();

    REQUIRE_FALSE(env->run("t = { 'a', 'b', named = 'c', [true] = 'd' }").has_value());

    const auto t = env->read_data_table("t");

    REQUIRE(t.has_value());

    REQUIRE(t->size() == 4);
    REQUIRE_FALSE(t->empty());

    REQUIRE(t->contains(1));
    REQUIRE(t->contains(2));
    REQUIRE(t->contains("named"));
    REQUIRE(t->contains(true));
    REQUIRE_FALSE(t->contains(false));
    REQUIRE_FALSE(t->contains(3));
    REQUIRE_FALSE(t->contains("1"));

    SECTION("and enumerates them, so a list of unknown length can be walked") {
        const auto keys = t->keys();

        REQUIRE(keys.size() == 4);

        std::vector<std::string> values;

        for (const auto &k : keys) if (const auto v = t->get_string(k)) values.push_back(*v);

        std::sort(values.begin(), values.end());

        REQUIRE(values == std::vector<std::string>{"a", "b", "c", "d"});
    }

    SECTION("number keys come back ascending, which is what a list needs") {
        REQUIRE_FALSE(env->run("list = { 'one', 'two', 'three' }").has_value());

        const auto list = env->read_data_table("list");

        REQUIRE(list.has_value());

        std::vector<std::string> ordered;

        for (const auto &k : list->keys())
            if (k.is_number()) ordered.push_back(*list->get_string(k));

        REQUIRE(ordered == std::vector<std::string>{"one", "two", "three"});
    }

    SECTION("an empty table says so") {
        REQUIRE(data_table().empty());
        REQUIRE(data_table().size() == 0);
        REQUIRE(data_table().keys().empty());
    }
}

TEST_CASE("a table can be built in c++ and written to lua", "[table][access]") {
    interpreter interp;

    auto env = interp.make_environment();

    SECTION("every key kind and every value kind survives the crossing") {
        data_table npc;

        npc.set("name", "robert");
        npc.set("hp", 80.0);
        npc.set("alive", true);
        npc.set(1, "first");
        npc.set(true, "flagged");

        data_table stats;
        stats.set("strength", 12.0);
        npc.set("stats", stats);

        env->write("built", npc);

        REQUIRE_FALSE(env->run(
            "name = built.name\n"
            "hp = built.hp\n"
            "alive = built.alive\n"
            "one = built[1]\n"
            "flagged = built[true]\n"
            "strength = built.stats.strength").has_value());

        REQUIRE(env->read_string("name") == "robert");
        REQUIRE(env->read_number("hp") == 80);
        REQUIRE(env->read_boolean("alive") == true);
        REQUIRE(env->read_string("one") == "first");
        REQUIRE(env->read_string("flagged") == "flagged");
        REQUIRE(env->read_number("strength") == 12);
    }

    SECTION("a string literal is stored as a string, not as true") {
        // const char* converts to bool by a standard conversion and to std::string by a user defined
        // one, so without an overload taking it a literal would be stored as the boolean true.
        data_table t;

        t.set("greeting", "hello");

        REQUIRE(t.get_string("greeting") == "hello");
        REQUIRE_FALSE(t.get_boolean("greeting").has_value());
    }

    SECTION("and it round trips through lua unchanged") {
        data_table original;

        original.set(1, "one");
        original.set(2.5, "two and a half");
        original.set(true, "yes");
        original.set(false, "no");
        original.set("name", "jim");
        original.set("count", 3.0);

        env->write("sent", original);

        const auto returned = env->read_data_table("sent");

        REQUIRE(returned.has_value());
        REQUIRE(returned->size() == original.size());

        for (const auto &k : original.keys()) {
            INFO("key index " << (k.is_number() ? "number" : k.is_boolean() ? "boolean" : "string"));

            REQUIRE(returned->contains(k));
            REQUIRE(returned->get_string(k) == original.get_string(k));
            REQUIRE(returned->get_number(k) == original.get_number(k));
        }
    }
}

TEST_CASE("a table can be edited in c++", "[table][access]") {
    data_table t;

    SECTION("setting the same key twice replaces it") {
        t.set("hp", 100.0);
        t.set("hp", 50.0);

        REQUIRE(t.get_number("hp") == 50);
        REQUIRE(t.size() == 1);
    }

    SECTION("a key can change type") {
        t.set("v", 1.0);

        REQUIRE(t.get_number("v") == 1);

        t.set("v", "now a string");

        REQUIRE_FALSE(t.get_number("v").has_value());
        REQUIRE(t.get_string("v") == "now a string");
    }

    SECTION("erasing removes it") {
        t.set("a", 1.0);
        t.set(1, 2.0);
        t.set(true, 3.0);

        REQUIRE(t.size() == 3);

        t.erase("a");
        t.erase(1);
        t.erase(true);

        REQUIRE(t.empty());
        REQUIRE_FALSE(t.contains("a"));
        REQUIRE_FALSE(t.contains(1));
        REQUIRE_FALSE(t.contains(true));
    }

    SECTION("erasing something that is not there is not an error") {
        t.erase("nothing");
        t.erase(42);

        REQUIRE(t.empty());
    }

    SECTION("the two boolean keys are separate") {
        t.set(true, "yes");
        t.set(false, "no");

        REQUIRE(t.size() == 2);
        REQUIRE(t.get_string(true) == "yes");
        REQUIRE(t.get_string(false) == "no");

        t.erase(true);

        REQUIRE(t.get_string(false) == "no");
        REQUIRE_FALSE(t.contains(true));
    }
}

TEST_CASE("copying a table copies what it holds", "[table][access]") {
    data_table original;

    original.set("hp", 1.0);
    original.set(1, "first");

    data_table inner;
    inner.set("strength", 10.0);
    original.set("stats", inner);

    data_table copy = original;

    copy.set("hp", 2.0);
    copy.set(1, "changed");
    copy.erase("stats");

    REQUIRE(original.get_number("hp") == 1);
    REQUIRE(original.get_string(1) == "first");
    REQUIRE(original.get_data_table("stats"));
    REQUIRE(original.get_data_table("stats")->get_number("strength") == 10);

    REQUIRE(copy.get_number("hp") == 2);
    REQUIRE(copy.get_string(1) == "changed");
    REQUIRE_FALSE(copy.get_data_table("stats"));
}

TEST_CASE("a subtable is copied into its parent, not shared with the caller", "[table][access]") {
    data_table inner;
    inner.set("hp", 100.0);

    data_table outer;
    outer.set("stats", inner);

    inner.set("hp", 1.0);

    REQUIRE(outer.get_data_table("stats")->get_number("hp") == 100);
}
