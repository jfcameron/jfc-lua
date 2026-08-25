// © Joseph Cameron - All Rights Reserved

#include <jfc/catch.hpp>

#include <jfc/lua.h>

#include <memory>
#include <string>

using namespace jfc::lua;

namespace {
    struct body final {
        int hp = 0;
        int *destroyed = nullptr;

        body(const int aHp, int *const aDestroyed) : hp(aHp), destroyed(aDestroyed) {}

        ~body() { if (destroyed) ++*destroyed; }
    };

    struct texture final { std::string name; };
}

TEST_CASE("a c++ object can be handed to a script and read back", "[userdata][crossing]") {
    interpreter interp;

    auto env = interp.make_environment();

    int destroyed = 0;

    const auto original = std::make_shared<body>(100, &destroyed);

    env->write("player.body", original);

    SECTION("read back as what it is") {
        const auto out = env->read_userdata<body>("player.body");

        REQUIRE(out.has_value());
        REQUIRE(*out == original);
        REQUIRE((*out)->hp == 100);
    }

    SECTION("read back untyped, then asked") {
        const auto held = env->read_userdata("player.body");

        REQUIRE(held.has_value());
        REQUIRE(held->holds<body>());
        REQUIRE(held->get<body>() == original);
    }

    SECTION("asking for the wrong type is answered, not reinterpreted") {
        REQUIRE_FALSE(env->read_userdata<texture>("player.body").has_value());
    }

    SECTION("lua sees it as a userdata") {
        REQUIRE_FALSE(env->run("kind = type(player.body)").has_value());

        REQUIRE(env->read_string("kind") == "userdata");
    }

    SECTION("a path holding something else is not one") {
        REQUIRE_FALSE(env->run("plain = 7  text = 'x'").has_value());

        REQUIRE_FALSE(env->read_userdata<body>("plain").has_value());
        REQUIRE_FALSE(env->read_userdata<body>("text").has_value());
        REQUIRE_FALSE(env->read_userdata("no.such.path").has_value());
    }

    SECTION("somebody else's userdata is not ours") {
        auto unrestricted = interp.make_environment(
            environment_policy{standard_library::unrestricted});

        REQUIRE_FALSE(unrestricted->run("handle = io.tmpfile()").has_value());
        REQUIRE_FALSE(unrestricted->run("kind = type(handle)").has_value());

        REQUIRE(unrestricted->read_string("kind") == "userdata");

        REQUIRE_FALSE(unrestricted->read_userdata<body>("handle").has_value());
        REQUIRE_FALSE(unrestricted->read_userdata("handle").has_value());
    }

    SECTION("and the other readers do not claim it") {
        REQUIRE_FALSE(env->read_number("player.body").has_value());
        REQUIRE_FALSE(env->read_string("player.body").has_value());
        REQUIRE_FALSE(env->read_boolean("player.body").has_value());
        REQUIRE_FALSE(env->read_data_table("player.body").has_value());
    }
}

TEST_CASE("a script may pass a handle about without being able to open it", "[userdata][crossing]") {
    interpreter interp;

    auto env = interp.make_environment();

    int destroyed = 0;

    env->write("a", std::make_shared<body>(1, &destroyed));

    SECTION("lua can move it, store it in a table, and hand it back") {
        REQUIRE_FALSE(env->run("b = a  holder = { it = a }").has_value());

        REQUIRE(env->read_userdata<body>("b").has_value());
        REQUIRE(env->read_userdata<body>("holder.it").has_value());
        REQUIRE(env->read_userdata<body>("b") == env->read_userdata<body>("a"));
    }

    SECTION("but its metatable is hidden, so the finaliser cannot be replaced") {
        REQUIRE_FALSE(env->run("meta = tostring(getmetatable(a))").has_value());

        REQUIRE(env->read_string("meta") == "false");
    }

    SECTION("and it cannot be stored in a data_table, which is the serialisable set") {
        REQUIRE_FALSE(env->run("t = { thing = a }").has_value());

        REQUIRE_THROWS_AS(env->read_data_table("t"), jfc::lua::exception);

        const auto skipped = env->read_data_table("t", unsupported::skip);

        REQUIRE(skipped.has_value());
        REQUIRE_FALSE(skipped->contains("thing"));
    }
}

TEST_CASE("a bound object is interpreter-scoped, not environment-scoped", "[userdata][crossing]") {
    interpreter interp;

    auto one = interp.make_environment();
    auto two = interp.make_environment();

    int destroyed = 0;

    const auto original = std::make_shared<body>(5, &destroyed);

    one->write("thing", original);

    SECTION("the other environment cannot see it by name") {
        REQUIRE_FALSE(two->read_userdata<body>("thing").has_value());
    }

    SECTION("but the same object handed over is fully usable there") {
        two->write("thing", original);

        const auto out = two->read_userdata<body>("thing");

        REQUIRE(out.has_value());
        REQUIRE(*out == original);
    }
}

TEST_CASE("ownership is shared across the barrier", "[userdata][crossing]") {
    int destroyed = 0;

    SECTION("c++ letting go does not destroy what a script still holds") {
        interpreter interp;

        auto env = interp.make_environment();

        {
            auto original = std::make_shared<body>(1, &destroyed);

            env->write("thing", original);
        }

        REQUIRE(destroyed == 0);

        const auto out = env->read_userdata<body>("thing");

        REQUIRE(out.has_value());
        REQUIRE((*out)->hp == 1);
    }

    SECTION("a script letting go does not destroy what c++ still holds") {
        interpreter interp;

        auto env = interp.make_environment();

        const auto original = std::make_shared<body>(2, &destroyed);

        env->write("thing", original);

        REQUIRE_FALSE(env->run("thing = nil").has_value());

        REQUIRE_FALSE(interp.run("collectgarbage('collect')").has_value());

        REQUIRE(destroyed == 0);
        REQUIRE(original->hp == 2);
    }

    SECTION("and when both let go it is destroyed, at collection") {
        interpreter interp;

        auto env = interp.make_environment();

        {
            auto original = std::make_shared<body>(3, &destroyed);

            env->write("thing", original);
        }

        REQUIRE(destroyed == 0);

        REQUIRE_FALSE(env->run("thing = nil").has_value());
        REQUIRE_FALSE(interp.run("collectgarbage('collect')").has_value());

        REQUIRE(destroyed == 1);
    }

    SECTION("anything still held is destroyed when the interpreter goes") {
        {
            interpreter interp;

            auto env = interp.make_environment();

            env->write("thing", std::make_shared<body>(4, &destroyed));

            REQUIRE(destroyed == 0);
        }

        REQUIRE(destroyed == 1);
    }
}

TEST_CASE("a bound object crosses in a call, in both directions", "[userdata][crossing][params]") {
    interpreter interp;

    auto env = interp.make_environment();

    int destroyed = 0;

    const auto original = std::make_shared<body>(100, &destroyed);

    SECTION("as an argument to a lua function") {
        env->register_function("hp_of", [](value_list_type args) -> value_list_type
        {
            const auto bound = std::get<userdata>(args.at(0)).get<body>();

            return {static_cast<double>(bound ? bound->hp : -1)};
        });

        REQUIRE_FALSE(env->run("function report(b) return hp_of(b) end").has_value());

        value_list_type results;

        REQUIRE_FALSE(env->call("report", {userdata::make(original)}, results).has_value());

        REQUIRE(std::get<double>(results.at(0)) == 100);
    }

    SECTION("as a result from a lua function") {
        env->write("stored", original);

        REQUIRE_FALSE(env->run("function fetch() return stored end").has_value());

        value_list_type results;

        REQUIRE_FALSE(env->call("fetch", {}, results).has_value());

        REQUIRE(results.size() == 1);

        const auto returned = std::get<userdata>(results.at(0));

        REQUIRE(returned.holds<body>());
        REQUIRE(returned.get<body>() == original);
    }

    SECTION("into and out of a registered c++ function") {
        env->register_function("echo", [](value_list_type args) -> value_list_type { return args; });

        value_list_type results;

        REQUIRE_FALSE(env->run("function bounce(b) return echo(b) end").has_value());
        REQUIRE_FALSE(env->call("bounce", {userdata::make(original)}, results).has_value());

        REQUIRE(std::get<userdata>(results.at(0)).get<body>() == original);
    }

    SECTION("a registered function may make one and hand it back") {
        env->register_function("spawn", [&destroyed](value_list_type) -> value_list_type
        {
            return {userdata::make(std::make_shared<body>(7, &destroyed))};
        });

        REQUIRE_FALSE(env->run("made = spawn()  kind = type(made)").has_value());

        REQUIRE(env->read_string("kind") == "userdata");

        const auto out = env->read_userdata<body>("made");

        REQUIRE(out.has_value());
        REQUIRE((*out)->hp == 7);
    }

    SECTION("asking a crossed one for the wrong type is still answered") {
        env->write("stored", original);

        REQUIRE_FALSE(env->run("function fetch() return stored end").has_value());

        value_list_type results;

        REQUIRE_FALSE(env->call("fetch", {}, results).has_value());

        REQUIRE_FALSE(std::get<userdata>(results.at(0)).get<texture>());
    }
}

TEST_CASE("somebody else's userdata does not cross in a call", "[userdata][crossing][params]") {
    interpreter interp;

    auto env = interp.make_environment(environment_policy{standard_library::unrestricted});

    env->register_function("takes", [](value_list_type) -> value_list_type { return {}; });

    REQUIRE_FALSE(env->run("handle = io.tmpfile()").has_value());

    const auto error = env->run("takes(handle)");

    REQUIRE(error.has_value());

    INFO("message was: " << *error);

    REQUIRE(error->find("userdata") != std::string::npos);
}
