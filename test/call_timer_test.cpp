// © Joseph Cameron - MIT License

#include <jfc/catch.hpp>

#include <jfc/lua.h>

#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

using namespace jfc::lua;

TEST_CASE("a call timer is told each call a script makes into c++, by the name it was registered at", "[call_timer]") {
    interpreter interp;

    auto env = interp.make_environment();

    env->register_function("world.slow", [](value_list_type) -> value_list_type {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));

        return {1.0};
    });

    env->register_function("quick", [](value_list_type) -> value_list_type { return {}; });

    std::map<std::string, int> calls;
    std::map<std::string, std::chrono::nanoseconds> took;

    SECTION("none set: nothing is told, and calls work as ever") {
        REQUIRE_FALSE(env->run("assert(world.slow() == 1) quick()"));

        REQUIRE(calls.empty());
    }

    SECTION("set: each call named and timed, whatever environment it was registered in, before or after") {
        interp.set_call_timer([&](const std::string_view aName, const std::chrono::nanoseconds aTook) {
            ++calls[std::string(aName)];
            took[std::string(aName)] += aTook;
        });

        auto other = interp.make_environment();

        other->register_function("later", [](value_list_type) -> value_list_type { return {}; });

        REQUIRE_FALSE(env->run("for i = 1, 3 do world.slow() end quick()"));
        REQUIRE_FALSE(other->run("later() later()"));

        REQUIRE(calls == std::map<std::string, int>{{"world.slow", 3}, {"quick", 1}, {"later", 2}});

        REQUIRE(took["world.slow"] >= std::chrono::milliseconds(6));
        REQUIRE(took["quick"] < std::chrono::milliseconds(2));

        SECTION("and emptied again, nothing more is told") {
            interp.set_call_timer({});

            REQUIRE_FALSE(env->run("world.slow()"));

            REQUIRE(calls["world.slow"] == 3);
        }
    }

    SECTION("a function that waits is timed for its call, from a coroutine") {
        interp.set_call_timer([&](const std::string_view aName, std::chrono::nanoseconds) { ++calls[std::string(aName)]; });

        env->register_yielding_function("wait", [](value_list_type) -> value_list_type { return {}; });

        REQUIRE_FALSE(env->run("function behaviour() wait() wait() end"));

        auto co = env->make_coroutine("behaviour");

        REQUIRE(co);
        REQUIRE_FALSE(co->resume());
        REQUIRE_FALSE(co->resume());

        REQUIRE(calls["wait"] == 2);
    }

    SECTION("a method on an object is named for its type and itself") {
        struct counter final { int count = 0; };

        interp.register_type<counter>("counter", {
            {"bump", [](counter &aSelf, value_list_type) -> value_list_type { return {static_cast<double>(++aSelf.count)}; }},
        });

        interp.set_call_timer([&](const std::string_view aName, std::chrono::nanoseconds) { ++calls[std::string(aName)]; });

        env->write("c", std::make_shared<counter>());

        REQUIRE_FALSE(env->run("c:bump() assert(c:bump() == 2)"));

        REQUIRE(calls == std::map<std::string, int>{{"counter:bump", 2}});
    }
}
