// © Joseph Cameron - MIT License

#include <jfc/catch.hpp>

#if defined(JFC_LUA_LUAU)
    #include <jfc/lua/checker.h>
    #include <jfc/lua/declarations.h>

    #include <algorithm>
    #include <map>
    #include <memory>
    #include <optional>
    #include <string>
    #include <vector>

using namespace jfc::lua;

namespace {
    struct project final {
        std::map<std::string, std::string> files;
        std::map<std::string, std::string> environments;
        std::map<std::string, std::string> declarations;

        [[nodiscard]] std::vector<checker::problem> check(const std::string &aFile) const {
            return made()->check(aFile);
        }

        [[nodiscard]] std::vector<checker::suggestion> complete(const std::string &aFile, const int aLine,
            const int aColumn) const {
            return made()->complete(aFile, aLine, aColumn);
        }

        [[nodiscard]] std::unique_ptr<checker> made() const {
            return std::make_unique<checker>(declarations,
                [this](const std::string &aName) -> std::optional<std::string> {
                    const auto found = files.find(aName);

                    if (found == files.end()) return std::nullopt;

                    return found->second;
                },
                [](const std::string &, const std::string &aName) -> std::optional<std::string> {
                    return aName + ".luau";
                },
                [this](const std::string &aName) {
                    const auto found = environments.find(aName);

                    return found == environments.end() ? std::string() : found->second;
                });
        }
    };

    [[nodiscard]] const checker::suggestion *named(const std::vector<checker::suggestion> &aOffered,
        const std::string &aName) {
        const auto found = std::find_if(aOffered.begin(), aOffered.end(),
            [&aName](const checker::suggestion &aEach) { return aEach.name == aName; });

        return found == aOffered.end() ? nullptr : &*found;
    }
}

TEST_CASE("a strict script's type error is found, where it is", "[checker]") {
    project p;

    p.files["a.luau"] = "--!strict\nlocal x: number = 'no'\n";

    const auto found = p.check("a.luau");

    REQUIRE(found.size() == 1);
    REQUIRE(found.front().file == "a.luau");
    REQUIRE(found.front().line == 2);
}

TEST_CASE("an environment's declarations make its globals known, and only there", "[checker]") {
    project p;

    p.declarations["server"] = "declare function spawn(kind: string): number\n";
    p.files["b.luau"] = "--!strict\nlocal n: number = spawn('goblin')\n";
    p.environments["b.luau"] = "server";

    REQUIRE(p.check("b.luau").empty());

    SECTION("an argument of the wrong type is found") {
        p.files["b.luau"] = "--!strict\nlocal n: number = spawn(5)\n";

        REQUIRE(p.check("b.luau").size() == 1);
    }

    SECTION("elsewhere the global is unknown") {
        p.environments.erase("b.luau");

        REQUIRE_FALSE(p.check("b.luau").empty());
    }
}

TEST_CASE("what a required library exports reaches the script requiring it", "[checker]") {
    project p;

    p.files["lib.luau"] = "--!strict\nexport type point = { x: number }\n"
        "return { origin = function(): point return { x = 0 } end }\n";
    p.files["user.luau"] = "--!strict\nlocal lib = require('lib')\nlocal at: lib.point = lib.origin()\n"
        "local said: string = at.x\n";

    const auto found = p.check("user.luau");

    REQUIRE(found.size() == 1);
    REQUIRE(found.front().line == 4);
}

TEST_CASE("refinements hold through an optional table's fields and in a closure", "[checker]") {
    project p;

    p.declarations["server"] = "declare extern type use with\n    function cancel(self): boolean\nend\n"
        "declare function begin(): any\n";

    SECTION("a field of a table that may be nil") {
        p.files["c.luau"] = "--!strict\nlocal function f(t: { [any]: any }?) return t and t.a and t.a.b end\n"
            "return f\n";

        REQUIRE(p.check("c.luau").empty());
    }

    SECTION("a local a closure reads, given any") {
        p.files["c.luau"] = "--!strict\nlocal function fight()\n    local striking: use?\n"
            "    local function let_go()\n        if striking then striking:cancel() end\n        striking = nil\n"
            "    end\n    while begin() do\n        striking = begin() or striking\n        let_go()\n    end\nend\n"
            "return fight\n";
        p.environments["c.luau"] = "server";

        REQUIRE(p.check("c.luau").empty());
    }
}

TEST_CASE("a file said to have changed is read again, and what requires it checked again", "[checker]") {
    project p;

    p.files["lib.luau"] = "--!strict\nreturn { size = function(): number return 1 end }\n";
    p.files["user.luau"] = "--!strict\nlocal lib = require('lib')\nlocal n: number = lib.size()\n";

    checker made(p.declarations,
        [&p](const std::string &aName) -> std::optional<std::string> {
            const auto found = p.files.find(aName);

            if (found == p.files.end()) return std::nullopt;

            return found->second;
        },
        [](const std::string &, const std::string &aName) -> std::optional<std::string> { return aName + ".luau"; },
        [](const std::string &) { return std::string(); });

    REQUIRE(made.check("user.luau").empty());

    p.files["lib.luau"] = "--!strict\nreturn { size = function(): string return 'one' end }\n";

    REQUIRE(made.check("user.luau").empty());

    made.changed("lib.luau");

    const auto found = made.check("user.luau");

    REQUIRE(found.size() == 1);
    REQUIRE(found.front().line == 3);
}

TEST_CASE("Luau offers a table's fields where one is being typed, each with its type", "[checker]") {
    project p;

    p.files["d.luau"] = "local point = { x = 1, label = 'here' }\nlocal y = point.\n";

    const auto offered = p.complete("d.luau", 2, 17);

    REQUIRE(offered.size() == 2);
    REQUIRE(offered.front().name == "label");
    REQUIRE(offered.front().type == checker::suggestion::kind::property);
    REQUIRE(offered.front().annotation == "string");
    REQUIRE(offered.back().name == "x");
    REQUIRE(offered.back().annotation == "number");
    REQUIRE_FALSE(offered.back().method);
}

TEST_CASE("Luau offers what a required library exports", "[checker]") {
    project p;

    p.files["lib.luau"] = "--!strict\nexport type point = { x: number }\n"
        "return { origin = function(): point return { x = 0 } end }\n";
    p.files["user.luau"] = "local lib = require('lib')\nlib.\n";

    const auto offered = p.complete("user.luau", 2, 5);
    const auto *const pOrigin = named(offered, "origin");

    REQUIRE(pOrigin);
    REQUIRE(pOrigin->annotation == "() -> point");
}

TEST_CASE("Luau offers a declared type's methods after a colon", "[checker]") {
    project p;

    p.declarations["server"] = "declare extern type use with\n    function cancel(self): boolean\nend\n"
        "declare function begin(): use\n";
    p.files["e.luau"] = "local u = begin()\nu:\n";
    p.environments["e.luau"] = "server";

    const auto offered = p.complete("e.luau", 2, 3);
    const auto *const pCancel = named(offered, "cancel");

    REQUIRE(pCancel);
    REQUIRE(pCancel->method);
    REQUIRE(pCancel->annotation.find("boolean") != std::string::npos);
}

TEST_CASE("Luau offers the names in scope and the keywords where a word is begun", "[checker]") {
    project p;

    p.declarations["server"] = "declare function spawn(kind: string): number\n";
    p.files["f.luau"] = "local alpha = 1\nal\n";
    p.environments["f.luau"] = "server";

    const auto offered = p.complete("f.luau", 2, 3);

    REQUIRE(named(offered, "alpha"));
    REQUIRE(named(offered, "alpha")->type == checker::suggestion::kind::binding);
    REQUIRE(named(offered, "alpha")->annotation == "number");
    REQUIRE(named(offered, "spawn"));
    REQUIRE(named(offered, "spawn")->annotation == "(kind: string) -> number");
    REQUIRE(named(offered, "local"));
    REQUIRE(named(offered, "local")->type == checker::suggestion::kind::keyword);
    REQUIRE(named(offered, "local")->annotation.empty());

    SECTION("and nothing in a comment") {
        p.files["f.luau"] = "local alpha = 1\n-- al\n";

        REQUIRE(p.complete("f.luau", 2, 6).empty());
    }
}

TEST_CASE("a shaped table a function gives has its fields checked, and only those", "[checker]") {
    project p;

    declared find;

    find.path = "find";

    signature::value thing{"thing"};

    thing.shape = std::make_shared<const shape>(shape{"thing", {{"label", {"string"}}, {"count", {"number"}}}});

    find.signature.results.push_back(thing);

    p.declarations["server"] = luau_declarations({find}, {});
    p.environments["g.luau"] = "server";

    p.files["g.luau"] = "--!strict\nlocal x: thing = find()\nlocal said: string = x.label\nlocal n: number = x.count\n";

    REQUIRE(p.check("g.luau").empty());

    SECTION("a field the shape has not") {
        p.files["g.luau"] = "--!strict\nlocal x = find()\nlocal said: string = x.nosuch\n";

        const auto found = p.check("g.luau");

        REQUIRE(found.size() == 1);
        REQUIRE(found.front().line == 3);
    }

    SECTION("a field of another type") {
        p.files["g.luau"] = "--!strict\nlocal x = find()\nlocal n: number = x.label\n";

        REQUIRE(p.check("g.luau").size() == 1);
    }
}

TEST_CASE("a script asks to be held to its types among its first comments", "[checker]") {
    REQUIRE(checker::strict("--!strict\nlocal x = 1\n"));
    REQUIRE(checker::strict("-- a note\n--!strict\n"));
    REQUIRE_FALSE(checker::strict("local x = 1\n--!strict\n"));
    REQUIRE_FALSE(checker::strict("local x = 1\n"));
}
#endif
