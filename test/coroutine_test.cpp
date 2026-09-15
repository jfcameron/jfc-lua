// © Joseph Cameron - All Rights Reserved

#include <jfc/catch.hpp>

#include <jfc/lua.h>

#include <lua.hpp>

#include <string>
#include <variant>

using namespace jfc::lua;

namespace {
    [[nodiscard]] double number(const value_list_type &aValues, const std::size_t aIndex) {
        return std::get<double>(aValues.at(aIndex));
    }

    [[nodiscard]] std::string text(const value_list_type &aValues, const std::size_t aIndex) {
        return std::get<std::string>(aValues.at(aIndex));
    }
}

TEST_CASE("**a coroutine yields out to c++, and what it is resumed with comes back from the yield**",
    "[coroutine]") {
    interpreter lua;

    auto env = lua.make_environment();

    REQUIRE_FALSE(env->run(R"(
        function counter(start)
            local total = start
            while true do
                local added = coroutine.yield(total)
                total = total + added
            end
        end
    )"));

    auto co = env->make_coroutine("counter");

    REQUIRE(co);
    REQUIRE(co->state() == coroutine::status::suspended);

    value_list_type out;

    SECTION("**the first resume starts it, with the arguments as the function's**") {
        REQUIRE_FALSE(co->resume({10.0}, out));
        REQUIRE(number(out, 0) == 10);
        REQUIRE(co->suspended());

        SECTION("**and every later one returns from the yield with them**") {
            REQUIRE_FALSE(co->resume({5.0}, out));
            REQUIRE(number(out, 0) == 15);

            REQUIRE_FALSE(co->resume({2.0}, out));
            REQUIRE(number(out, 0) == 17);
        }
    }
}

TEST_CASE("**a coroutine that returns has finished: its results come back, and it cannot be resumed**",
    "[coroutine]") {
    interpreter lua;

    auto env = lua.make_environment();

    REQUIRE_FALSE(env->run(R"(
        function twice()
            coroutine.yield("first")
            return "done", 2
        end
    )"));

    auto co = env->make_coroutine("twice");

    value_list_type out;

    REQUIRE_FALSE(co->resume({}, out));
    REQUIRE(text(out, 0) == "first");

    REQUIRE_FALSE(co->resume({}, out));
    REQUIRE(co->state() == coroutine::status::finished);
    REQUIRE(out.size() == 2);
    REQUIRE(text(out, 0) == "done");
    REQUIRE(number(out, 1) == 2);

    const auto again = co->resume({}, out);

    REQUIRE(again);
    REQUIRE(again->find("finished") != std::string::npos);
    REQUIRE(out.empty());
}

TEST_CASE("**a coroutine that raises an error has failed, saying why, and cannot be resumed**",
    "[coroutine]") {
    interpreter lua;

    auto env = lua.make_environment();

    REQUIRE_FALSE(env->run(R"(
        function falls_over()
            coroutine.yield()
            error("tripped on the step")
        end
    )"));

    auto co = env->make_coroutine("falls_over");

    REQUIRE_FALSE(co->resume());

    const auto error = co->resume();

    REQUIRE(error);
    REQUIRE(error->find("tripped on the step") != std::string::npos);
    REQUIRE(co->state() == coroutine::status::failed);
    REQUIRE(co->resume());
}

TEST_CASE("**a function registered to yield suspends the coroutine that calls it**",
    "[coroutine]") {
    interpreter lua;

    auto env = lua.make_environment();

    int called = 0;

    env->register_yielding_function("wait", [&called](value_list_type aArguments) {
        ++called;

        return aArguments;
    });

    REQUIRE_FALSE(env->run(R"(
        function errand()
            local late = wait(2, "at the well")
            return "back", late
        end
    )"));

    auto co = env->make_coroutine("errand");

    value_list_type out;

    SECTION("**what it returns is yielded to the host**") {
        REQUIRE_FALSE(co->resume({}, out));
        REQUIRE(called == 1);
        REQUIRE(co->suspended());
        REQUIRE(number(out, 0) == 2);
        REQUIRE(text(out, 1) == "at the well");

        SECTION("**and what the host resumes with is what the call returns in lua**") {
            REQUIRE_FALSE(co->resume({false}, out));
            REQUIRE(co->state() == coroutine::status::finished);
            REQUIRE(text(out, 0) == "back");
            REQUIRE(std::get<bool>(out.at(1)) == false);
            REQUIRE(called == 1);
        }
    }

    SECTION("**outside a coroutine it refuses, before its closure runs**") {
        REQUIRE_FALSE(env->run("function plain() return wait(1) end"));

        const auto error = env->call("plain");

        REQUIRE(error);
        REQUIRE(error->find("coroutine") != std::string::npos);
        REQUIRE(called == 0);
    }
}

TEST_CASE("**a plain registered function can be called from a coroutine as from anywhere**",
    "[coroutine]") {
    interpreter lua;

    auto env = lua.make_environment();

    env->register_function("add", [](value_list_type aArguments) -> value_list_type {
        return {std::get<double>(aArguments.at(0)) + std::get<double>(aArguments.at(1))};
    });

    REQUIRE_FALSE(env->run("function sums() coroutine.yield(add(1, 2)) return add(3, 4) end"));

    auto co = env->make_coroutine("sums");

    value_list_type out;

    REQUIRE_FALSE(co->resume({}, out));
    REQUIRE(number(out, 0) == 3);

    REQUIRE_FALSE(co->resume({}, out));
    REQUIRE(number(out, 0) == 7);
}

TEST_CASE("**each resume spends its own instruction budget**", "[coroutine][limits]") {
    interpreter lua;

    auto env = lua.make_environment({standard_library::safe, 100000});

    REQUIRE_FALSE(env->run(R"(
        function busy_but_polite()
            while true do
                for i = 1, 20000 do end   -- well inside a budget, every time
                coroutine.yield()
            end
        end

        function never_yields()
            while true do end
        end
    )"));

    SECTION("**one that yields in time is resumed as often as the host likes**") {
        auto co = env->make_coroutine("busy_but_polite");

        for (int i = 0; i < 50; ++i) REQUIRE_FALSE(co->resume());

        REQUIRE(co->suspended());
    }

    SECTION("**one that never yields fails, rather than hanging its host**") {
        auto co = env->make_coroutine("never_yields");

        const auto error = co->resume();

        REQUIRE(error);
        REQUIRE(error->find("budget") != std::string::npos);
        REQUIRE(co->state() == coroutine::status::failed);

        SECTION("**and the environment carries on**") {
            REQUIRE_FALSE(env->run("fine = true"));
            REQUIRE(env->read_boolean("fine").value_or(false));
        }
    }
}

TEST_CASE("**a coroutine runs against its environment's globals, and two of one function keep their "
    "own locals**", "[coroutine][environment]") {
    interpreter lua;

    auto first = lua.make_environment();
    auto second = lua.make_environment();

    const std::string script = R"(
        function speak()
            local said = 0
            while true do
                said = said + 1
                coroutine.yield(name, said)
            end
        end
    )";

    REQUIRE_FALSE(first->run("name = \"jim\"\n" + script));
    REQUIRE_FALSE(second->run("name = \"robert\"\n" + script));

    auto jim = first->make_coroutine("speak");
    auto robert = second->make_coroutine("speak");
    auto jimAgain = first->make_coroutine("speak");

    value_list_type out;

    REQUIRE_FALSE(jim->resume({}, out));
    REQUIRE(text(out, 0) == "jim");

    REQUIRE_FALSE(robert->resume({}, out));
    REQUIRE(text(out, 0) == "robert");

    REQUIRE_FALSE(jim->resume({}, out));
    REQUIRE(number(out, 1) == 2);

    REQUIRE_FALSE(jimAgain->resume({}, out));
    REQUIRE(text(out, 0) == "jim");
    REQUIRE(number(out, 1) == 1);
}

TEST_CASE("**copies share one coroutine**", "[coroutine]") {
    interpreter lua;

    auto env = lua.make_environment();

    REQUIRE_FALSE(env->run("function steps() coroutine.yield(1) coroutine.yield(2) end"));

    auto co = *env->make_coroutine("steps");
    auto copy = co;

    value_list_type out;

    REQUIRE_FALSE(co.resume({}, out));
    REQUIRE_FALSE(copy.resume({}, out));
    REQUIRE(number(out, 0) == 2);

    REQUIRE_FALSE(co.resume({}, out));
    REQUIRE(copy.state() == coroutine::status::finished);
}

TEST_CASE("**nothing is made of what is not a function**", "[coroutine]") {
    interpreter lua;

    auto env = lua.make_environment();

    REQUIRE_FALSE(env->run("not_a_function = 3"));

    REQUIRE_FALSE(env->make_coroutine("not_a_function"));
    REQUIRE_FALSE(env->make_coroutine("nothing_at_all"));
}

TEST_CASE("**a coroutine cannot resume itself**", "[coroutine]") {
    interpreter lua;

    auto env = lua.make_environment();

    std::optional<coroutine> co;

    std::optional<std::string> inner;

    env->register_function("resume_me", [&](value_list_type) -> value_list_type {
        inner = co->resume();

        return {};
    });

    REQUIRE_FALSE(env->run("function recursive() resume_me() end"));

    co = env->make_coroutine("recursive");

    REQUIRE_FALSE(co->resume());
    REQUIRE(inner);
    REQUIRE(inner->find("itself") != std::string::npos);
    REQUIRE(co->state() == coroutine::status::finished);
}

TEST_CASE("**yielding and failing leave the stack as they found it**", "[coroutine][stack]") {
    interpreter lua;

    auto env = lua.make_environment();

    env->register_yielding_function("wait", [](value_list_type a) { return a; });

    REQUIRE_FALSE(env->run(R"(
        function mixed()
            wait(1)
            coroutine.yield({ a = 1 }, "two", 3)
            error("and out")
        end
    )"));

    auto co = env->make_coroutine("mixed");

    value_list_type out;

    for (int i = 0; i < 2; ++i) REQUIRE_FALSE(co->resume({}, out));

    REQUIRE(out.size() == 3);

    REQUIRE(co->resume());

    for (int i = 0; i < 100; ++i) REQUIRE_FALSE(env->run("x = (x or 0) + 1"));

    REQUIRE(env->read_number("x").value_or(0) == 100);
}

#ifdef JFC_LUA_LUAJIT
TEST_CASE("**a yield across a pcall inside the coroutine comes out, and back in**", "[coroutine]") {
    interpreter lua;

    auto env = lua.make_environment();

    REQUIRE_FALSE(env->run(R"(
        function guarded()
            local ok, value = pcall(function()
                return coroutine.yield("inside") + 1
            end)
            return ok, value
        end
    )"));

    auto co = env->make_coroutine("guarded");

    value_list_type out;

    REQUIRE_FALSE(co->resume({}, out));
    REQUIRE(text(out, 0) == "inside");

    REQUIRE_FALSE(co->resume({41.0}, out));
    REQUIRE(std::get<bool>(out.at(0)));
    REQUIRE(number(out, 1) == 42);
}
#endif

TEST_CASE("**a coroutine resumed for as long as a game runs does not grow**", "[coroutine][stack]") {
    interpreter lua;

    auto env = lua.make_environment();

    REQUIRE_FALSE(env->run(R"(
        function forever()
            local n = 0
            while true do
                n = n + 1
                coroutine.yield(n, "tick", true)
            end
        end
    )"));

    auto co = env->make_coroutine("forever");

    value_list_type out;

    for (int i = 1; i <= 10000; ++i) {
        const auto error = co->resume({}, out);

        if (error) FAIL(*error);

        REQUIRE(out.size() == 3);
    }

    REQUIRE(std::get<double>(out.at(0)) == 10000);
}
