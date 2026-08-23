// © Joseph Cameron - All Rights Reserved

#include <jfc/catch.hpp>

#include <jfc/lua.h>

#include <sstream>
#include <string>

using namespace jfc::lua;

namespace {
    [[nodiscard]] std::string render(const path &a) {
        std::ostringstream out;
        out << a;

        return out.str();
    }
}

TEST_CASE("a path parses to the same thing as writing its keys out", "[path]") {
    SECTION("a single name") {
        REQUIRE(path("hp") == path{"hp"});
    }

    SECTION("names separated by dots") {
        REQUIRE(path("stats.strength") == path{"stats", "strength"});
        REQUIRE(path("a.b.c.d") == path{"a", "b", "c", "d"});
    }

    SECTION("a number key in brackets") {
        REQUIRE(path("list[1]") == path{"list", 1});
        REQUIRE(path("npcs[2].name") == path{"npcs", 2, "name"});
    }

    SECTION("a boolean key in brackets") {
        REQUIRE(path("flags[true]") == path{"flags", true});
        REQUIRE(path("flags[false]") == path{"flags", false});
    }

    SECTION("a quoted string key, which is how a key containing a delimiter is written") {
        REQUIRE(path("t['my.key']") == path{"t", "my.key"});
        REQUIRE(path("t[\"my.key\"]") == path{"t", "my.key"});
        REQUIRE(path("t['a[0]']") == path{"t", "a[0]"});
        REQUIRE(path("t['']") == path{"t", ""});
    }

    SECTION("escapes inside a quoted key") {
        REQUIRE(path("t['it\\'s']") == path{"t", "it's"});
        REQUIRE(path("t['back\\\\slash']") == path{"t", "back\\slash"});
    }

    SECTION("a bracket may start a path") {
        REQUIRE(path("[1]") == path{1});
        REQUIRE(path("[1].name") == path{1, "name"});
    }

    SECTION("brackets may follow one another") {
        REQUIRE(path("grid[2][3]") == path{"grid", 2, 3});
    }

    SECTION("numbers need not be integers, since lua has one number type") {
        REQUIRE(path("t[1.5]") == path{"t", 1.5});
        REQUIRE(path("t[-1]") == path{"t", -1});
        REQUIRE(path("t[0]") == path{"t", 0});
    }
}

TEST_CASE("a dot always means a string key and a bracket always means a typed one", "[path]") {
    SECTION("a name that looks like a number is still a name") {
        REQUIRE(path("a.1") == path{"a", "1"});
        REQUIRE(path("a.1") != path{"a", 1});
    }

    SECTION("a name that looks like a boolean is still a name") {
        REQUIRE(path("a.true") == path{"a", "true"});
        REQUIRE(path("a.true") != path{"a", true});
    }

    SECTION("and the bracket forms are the typed ones") {
        REQUIRE(path("a[1]") == path{"a", 1});
        REQUIRE(path("a[1]") != path{"a", "1"});

        REQUIRE(path("a[true]") == path{"a", true});
        REQUIRE(path("a[true]") != path{"a", "true"});
    }

    SECTION("so the three keys that print alike are three different keys") {
        REQUIRE(path{"t", 1} != path{"t", "1"});
        REQUIRE(path{"t", true} != path{"t", "true"});
        REQUIRE(path{"t", 1} != path{"t", true});
    }
}

TEST_CASE("keys carry their type", "[path]") {
    SECTION("an integer index is a number, not an ambiguity") {
        REQUIRE(key(2).is_number());
        REQUIRE(key(std::size_t(2)).is_number());
        REQUIRE(key(2.0).is_number());
        REQUIRE(key(-1).is_number());
    }

    SECTION("a literal is a string, not a pointer converted to true") {
        REQUIRE(key("name").is_string());
        REQUIRE(key(std::string("name")).is_string());
    }

    SECTION("a boolean is a boolean") {
        REQUIRE(key(true).is_boolean());
        REQUIRE(key(false).is_boolean());
    }

    SECTION("and they do not compare equal across types") {
        REQUIRE(key(1) != key("1"));
        REQUIRE(key(true) != key(1));
        REQUIRE(key(true) != key("true"));
        REQUIRE(key(1) == key(1.0));
    }
}

TEST_CASE("a path reports its shape", "[path]") {
    const path p("npcs[2].name");

    REQUIRE(p.size() == 3);
    REQUIRE_FALSE(p.empty());

    REQUIRE(p[0] == key("npcs"));
    REQUIRE(p[1] == key(2));
    REQUIRE(p[2] == key("name"));

    REQUIRE(p.segments().size() == 3);
}

TEST_CASE("the written form parses back to the same path", "[path]") {
    const path cases[] = {
        path{"hp"},
        path{"stats", "strength"},
        path{"list", 1},
        path{"npcs", 2, "name"},
        path{"flags", true},
        path{"flags", false},
        path{"t", "my.key"},
        path{"t", "a[0]"},
        path{"t", "it's"},
        path{"t", "back\\slash"},
        path{"t", ""},
        path{1},
        path{1, "name"},
        path{"grid", 2, 3},
        path{"t", 1.5},
        path{"t", -1},
        path{"t", "1"},
        path{"t", "true"}
    };

    for (const auto &original : cases) {
        const auto written = render(original);

        INFO("wrote " << written);

        REQUIRE(path(written) == original);
    }
}

TEST_CASE("the written form is the one a person would have written", "[path]") {
    REQUIRE(render(path{"npcs", 2, "name"}) == "npcs[2].name");
    REQUIRE(render(path{"stats", "strength"}) == "stats.strength");
    REQUIRE(render(path{"flags", true}) == "flags[true]");
    REQUIRE(render(path{"t", "my.key"}) == "t['my.key']");
    REQUIRE(render(path{"t", 1}) == "t[1]");
    REQUIRE(render(path{"t", "1"}) == "t.1");
}

TEST_CASE("a malformed path is a mistake in the caller, and says so", "[path]") {
    const char *const malformed[] = {
        "",             // nothing at all
        ".",            // a name that is empty
        ".a",           // ditto, leading
        "a.",           // ditto, trailing
        "a..b",         // ditto, in the middle
        "a[",           // never closed
        "a[1",          // ditto
        "a[]",          // no key
        "a['x]",        // quote never closed
        "a[\"x]",       // ditto
        "a[nope]",      // not a number, a boolean, or quoted
        "a[1x]",        // not quite a number
        "a[1]]",        // a stray bracket
        "a]b",          // ditto
        "a'b",          // a quote outside a bracket is one too
        "a\"b"          // ditto
    };

    for (const auto *const bad : malformed) {
        INFO("path \"" << bad << "\"");

        REQUIRE_THROWS_AS(path(bad), jfc::lua_exception);
    }
}

TEST_CASE("the reason given is the specific one", "[path]") {
    const struct { const char *path; const char *expected; } cases[] = {
        {"a[]",     "contains no key"},
        {"a[",      "never closed"},
        {"a['x]",   "never closed"},
        {"a..b",    "is empty"},
        {"a[nope]", "is not a number"}
    };

    for (const auto &c : cases) {
        INFO("path \"" << c.path << "\"");

        try {
            const path parsed(c.path);

            FAIL("expected a throw");
        }
        catch (const jfc::lua_exception &e) {
            REQUIRE(std::string(e.what()).find(c.expected) != std::string::npos);
        }
    }
}

TEST_CASE("the message names the path and the reason", "[path]") {
    try {
        const path p("npcs[oops]");

        FAIL("expected a throw");
    }
    catch (const jfc::lua_exception &e) {
        const std::string what = e.what();

        REQUIRE(what.find("npcs[oops]") != std::string::npos);
        REQUIRE(what.find("oops") != std::string::npos);
    }
}
