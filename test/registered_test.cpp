// © Joseph Cameron - MIT License

#include <jfc/catch.hpp>

#include <jfc/lua.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

using namespace jfc::lua;

namespace {
    struct lantern final { int lit = 0; };
    struct unbound final {};

    [[nodiscard]] signature::value of(const std::string &aType, const bool aOptional = false) {
        return {aType, aOptional};
    }
}

TEST_CASE("an interpreter lists what its named environments have registered, with their signatures", "[registered]") {
    interpreter interp;

    auto rules = interp.make_environment({standard_library::safe, {}, "rules"});

    SECTION("a typed function's arguments and results, as its c++ signature says them") {
        rules->register_function("world.give", [](const std::string_view, const std::optional<int>, const bool)
            -> std::tuple<double, std::optional<std::string>> { return {1, {}}; });

        const auto listed = interp.registered().environments.at("rules").at("world.give");

        REQUIRE(listed.arguments == std::vector{of("string"), of("integer", true), of("boolean")});
        REQUIRE(listed.results == std::vector{of("number"), of("string", true)});
        REQUIRE_FALSE(listed.takes_rest);
        REQUIRE_FALSE(listed.gives_rest);
        REQUIRE_FALSE(listed.yields);
    }

    SECTION("tables, functions, any value and objects") {
        rules->register_function("each", [](const data_table &, const table_view &, const function_reference &,
            const reference &, const userdata &) {});

        REQUIRE(interp.registered().environments.at("rules").at("each").arguments
            == std::vector{of("table"), of("table"), of("function"), of("value"), of("userdata")});
    }

    SECTION("a value list takes or gives every value left") {
        rules->register_function("pass", [](const double, const value_list_type &aRest) { return aRest; });

        const auto listed = interp.registered().environments.at("rules").at("pass");

        REQUIRE(listed.arguments == std::vector{of("number")});
        REQUIRE(listed.takes_rest);
        REQUIRE(listed.results.empty());
        REQUIRE(listed.gives_rest);
    }

    SECTION("a closure of value lists takes and gives every value") {
        rules->register_function("old",
            environment::closure_type([](value_list_type aArguments) { return aArguments; }));

        const auto listed = interp.registered().environments.at("rules").at("old");

        REQUIRE(listed.arguments.empty());
        REQUIRE(listed.takes_rest);
        REQUIRE(listed.gives_rest);
    }

    SECTION("a function that waits says so") {
        rules->register_yielding_function("wait", [](const double) {});

        REQUIRE(interp.registered().environments.at("rules").at("wait").yields);
    }

    SECTION("an object of a bound type is named as it was bound, and one never bound is userdata") {
        interp.register_type<lantern>("lantern", method("light", [](lantern &aSelf) { ++aSelf.lit; }));

        rules->register_function("hang", [](const std::shared_ptr<lantern> &, const std::shared_ptr<unbound> &) {});

        REQUIRE(interp.registered().environments.at("rules").at("hang").arguments
            == std::vector{of("lantern"), of("userdata")});
    }

    SECTION("an environment with no name lists nothing, and environments sharing a name are listed together") {
        interp.make_environment()->register_function("hidden", [] {});
        interp.make_environment({standard_library::safe, {}, "rules"})->register_function("also", [] {});
        rules->register_function("first", [] {});

        const auto listed = interp.registered().environments;

        REQUIRE(listed.size() == 1);
        REQUIRE(listed.at("rules").size() == 2);
        REQUIRE(listed.at("rules").contains("also"));
        REQUIRE(listed.at("rules").contains("first"));
    }

    SECTION("a function registered again is listed as it was last registered") {
        rules->register_function("twice", [](const double) {});
        rules->register_function("twice", [](const std::string &) {});

        REQUIRE(interp.registered().environments.at("rules").at("twice").arguments == std::vector{of("string")});
    }
}

TEST_CASE("an interpreter lists each bound type's methods, with their signatures", "[registered]") {
    interpreter interp;

    interp.register_type<lantern>("lantern",
        method("light", [](lantern &aSelf, const int aBy) { return aSelf.lit += aBy; }),
        method("snuff", [](lantern &aSelf) { aSelf.lit = 0; }));

    const auto methods = interp.registered().types.at("lantern");

    REQUIRE(methods.at("light").arguments == std::vector{of("integer")});
    REQUIRE(methods.at("light").results == std::vector{of("integer")});
    REQUIRE(methods.at("snuff").arguments.empty());
    REQUIRE(methods.at("snuff").results.empty());

    SECTION("a type bound again lists only its new methods, and a method of value lists takes and gives every value") {
        interp.register_type<lantern>("lantern",
            {{"any", [](lantern &, value_list_type aArguments) { return aArguments; }}});

        const auto again = interp.registered().types.at("lantern");

        REQUIRE(again.size() == 1);
        REQUIRE(again.at("any").takes_rest);
        REQUIRE(again.at("any").gives_rest);
    }
}

TEST_CASE("an interpreter lists the lua functions each named environment reaches, and where", "[registered]") {
    interpreter interp;

    auto ui = interp.make_environment({standard_library::safe, {}, "interface"});

    ui->register_function("ui.text", [] {});

    REQUIRE_FALSE(ui->run(
        "ui = ui or {}\n"
        "function ui.window(options) end\n"
        "local helper = {}\n"
        "function helper.hidden() end\n"
        "ui.nested = {\n"
        "    deeper = function(a, b) end,\n"
        "}\n"
        "ui.nested.again = ui.nested\n", "lib/ui.lua"));

    const auto defined = interp.registered().defined.at("interface");

    REQUIRE(defined.at("ui.window") == definition{"lib/ui.lua", 2});
    REQUIRE(defined.at("ui.nested.deeper") == definition{"lib/ui.lua", 6});
    REQUIRE_FALSE(defined.contains("ui.text"));
    REQUIRE_FALSE(defined.contains("helper.hidden"));

    SECTION("a script run without a name gives no source") {
        REQUIRE_FALSE(ui->run("function plain() end"));

        REQUIRE(interp.registered().defined.at("interface").at("plain").source.empty());
    }

    SECTION("an environment no longer living lists nothing it defined, and keeps what it registered") {
        ui.reset();

        const auto listed = interp.registered();

        REQUIRE_FALSE(listed.defined.contains("interface"));
        REQUIRE(listed.environments.at("interface").contains("ui.text"));
    }

    SECTION("the latest environment made with a name is the one read") {
        auto another = interp.make_environment({standard_library::safe, {}, "interface"});

        REQUIRE_FALSE(another->run("function other() end", "lib/other.lua"));

        const auto listed = interp.registered().defined.at("interface");

        REQUIRE(listed.contains("other"));
        REQUIRE_FALSE(listed.contains("ui.window"));
    }
}
