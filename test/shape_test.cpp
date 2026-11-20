// © Joseph Cameron - MIT License

#include <jfc/catch.hpp>

#include <jfc/lua.h>

#include <functional>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using namespace jfc::lua;

namespace {
    struct place final {
        [[nodiscard]] static const shape &describe() {
            static const shape out{"place", {field_of<double>("x"), field_of<double>("z")}, "where it is"};

            return out;
        }
    };

    struct thing final {
        [[nodiscard]] static const shape &describe() {
            static const shape out{"thing", {
                field_of<double>("id", "its number"),
                field_of<std::string>("what"),
                field_of<std::optional<std::string>>("slot"),
                field_of<std::optional<std::vector<std::string>>>("tags"),
                field_of<std::map<std::string, double>>("state"),
                field_of<shaped<place>>("where"),
            }};

            return out;
        }
    };

    [[nodiscard]] shaped<thing> made(const double aId, const std::string &aWhat) {
        shaped<place> where;

        where.set("x", 2.0).set("z", -3.0);

        shaped<thing> out;

        data_table state;

        state.set("base:durability", 7.0);

        out.set("id", aId).set("what", aWhat).set("state", state).set("where", where);

        return out;
    }

    [[nodiscard]] std::string refusal(const std::function<void()> &aDo) {
        try {
            aDo();
        }
        catch (const std::logic_error &e) {
            return e.what();
        }

        return {};
    }
}

TEST_CASE("a shaped table takes its shape's fields alone, each of its kind, and is handed over only whole", "[shape]") {
    shaped<thing> out = made(1, "base:iron_sword");

    REQUIRE(out.table().get_string("what") == "base:iron_sword");
    REQUIRE(out.table().get_data_table("where")->get_number("x") == 2.0);

    SECTION("a field the shape has not") {
        REQUIRE(refusal([&] { out.set("colour", "red"); }) == "thing has no field colour");
    }

    SECTION("a field of another kind") {
        REQUIRE(refusal([&] { out.set("id", "one"); }) == "thing's id is not that kind of value");
        REQUIRE(refusal([&] { out.set("state", 4.0); }) == "thing's state is not that kind of value");
    }

    SECTION("one the shape does not let be left out, left out") {
        shaped<thing> missing;

        missing.set("id", 2.0);

        REQUIRE(refusal([&] { static_cast<void>(missing.table()); }) == "thing is missing its what");
    }

    SECTION("one it does, set or not") {
        REQUIRE_FALSE(out.table().contains("slot"));

        out.set("slot", "melee");

        REQUIRE(out.table().get_string("slot") == "melee");
    }
}

TEST_CASE("a function giving a shaped table, or a list of them, gives lua tables of their fields", "[shape]") {
    interpreter interp;

    auto env = interp.make_environment({standard_library::safe, {}, "rules"});

    env->register_function("find", [] { return made(1, "base:iron_sword"); });
    env->register_function("all", [] { return std::vector<shaped<thing>>{made(1, "base:ruby"), made(2, "base:gold")}; });
    env->register_function("broken", [] { return shaped<thing>(); });

    REQUIRE_FALSE(env->run("x = find().where.x what = all()[2].what n = #all()"));
    REQUIRE(env->read_number("x") == 2.0);
    REQUIRE(env->read_string("what") == "base:gold");
    REQUIRE(env->read_number("n") == 2.0);

    SECTION("one missing a field its shape does not let be left out is an error where it is given") {
        const auto error = env->run("broken()");

        REQUIRE(error);
        REQUIRE(error->find("thing is missing its id") != std::string::npos);
    }

    SECTION("their signatures name the shape, and say a list") {
        const auto registered = interp.registered();
        const auto &rules = registered.environments.at("rules");

        const auto &one = rules.at("find").results.at(0);
        const auto &many = rules.at("all").results.at(0);

        REQUIRE(one.type == "thing");
        REQUIRE_FALSE(one.list);
        REQUIRE(one.shape);
        REQUIRE(*one.shape == thing::describe());
        REQUIRE(many.type == "thing");
        REQUIRE(many.list);
    }
}

TEST_CASE("a shape's fields say their types as a typed function's would", "[shape]") {
    const auto &fields = thing::describe().fields;

    REQUIRE(fields.at(0).type == signature::value{"number"});
    REQUIRE(fields.at(0).words == "its number");
    REQUIRE(fields.at(2).type == signature::value{"string", true});
    REQUIRE(fields.at(3).type.list);
    REQUIRE(fields.at(3).type.optional);
    REQUIRE(fields.at(4).type.type == "number");
    REQUIRE(fields.at(4).type.key == "string");
    REQUIRE(fields.at(5).type.type == "place");
    REQUIRE(fields.at(5).type.shape);
    REQUIRE(fields.at(5).type.shape->fields.size() == 2);
}
