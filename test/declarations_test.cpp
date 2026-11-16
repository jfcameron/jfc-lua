// © Joseph Cameron - MIT License

#include <jfc/catch.hpp>

#include <jfc/lua/declarations.h>

#if defined(JFC_LUA_LUAU)
    #include <Luau/Parser.h>
#endif

#include <map>
#include <string>
#include <utility>
#include <vector>

using namespace jfc::lua;

namespace {
    using value = signature::value;

    [[nodiscard]] declared made(std::string aPath, std::vector<value> aArguments, std::vector<value> aResults,
        std::vector<std::string> aParameters, std::string aWords = {}, const bool aRest = false) {
        declared out;

        out.path = std::move(aPath);
        out.signature.arguments = std::move(aArguments);
        out.signature.results = std::move(aResults);
        out.signature.takes_rest = aRest;
        out.signature.gives_rest = aRest;
        out.parameters = std::move(aParameters);
        out.words = std::move(aWords);

        return out;
    }

    const std::vector<declared> FUNCTIONS{
        made("world.give_item", {{"actor"}, {"string"}, {"integer", true}}, {{"boolean"}}, {"who", "what", "count"},
            "gives an item"),
        made("world.time.set", {{"number"}}, {}, {"hour"}),
        made("say", {}, {}, {}, "says anything", true),
        made("verb", {{"string"}}, {{"verb", true}}, {"name"}),
        made("verb.get", {{"string"}}, {{"verb", true}}, {"end"}),
    };

    const std::map<std::string, std::vector<declared>> TYPES{
        {"verb", {made("use", {{"value"}}, {}, {"at"}, "uses it", true)}},
    };
}

TEST_CASE("declarations say each type with its methods and each function under its path", "[declarations]") {
    REQUIRE(luau_declarations(FUNCTIONS, TYPES) ==
        "declare extern type verb with\n"
        "    -- uses it\n"
        "    function use(self, at: any, ...: any): ...any\n"
        "end\n"
        "\n"
        "-- says anything\n"
        "declare function say(...: any): ...any\n"
        "\n"
        "declare verb: ((name: string) -> verb?) & {\n"
        "    get: (a1: string) -> verb?,\n"
        "}\n"
        "\n"
        "declare world: {\n"
        "    -- gives an item\n"
        "    give_item: (who: actor, what: string, count: number?) -> boolean,\n"
        "    time: {\n"
        "        set: (hour: number) -> (),\n"
        "    },\n"
        "}\n");
}

#if defined(JFC_LUA_LUAU)
TEST_CASE("declarations are what Luau reads as a definitions file", "[declarations]") {
    const std::string text = luau_declarations(FUNCTIONS, TYPES);

    Luau::Allocator allocator;
    Luau::AstNameTable names(allocator);
    Luau::ParseOptions options;

    options.allowDeclarationSyntax = true;

    const auto parsed = Luau::Parser::parse(text.data(), text.size(), names, allocator, options);

    std::string errors;

    for (const auto &each : parsed.errors) errors += each.getMessage() + "\n";

    INFO(text << errors);

    REQUIRE(parsed.errors.empty());
}
#endif
