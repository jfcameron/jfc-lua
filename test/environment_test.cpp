// © Joseph Cameron - All Rights Reserved

#include <jfc/catch.hpp>

#include <jfc/lua.h>

#include <string>

using namespace jfc::lua;

TEST_CASE("environments do not share variables", "[environment]") {
    interpreter interp;

    auto bob = interp.make_environment();
    auto alice = interp.make_environment();

    SECTION("the same name in two environments is two variables") {
        REQUIRE_FALSE(bob->run("hp = 100").has_value());
        REQUIRE_FALSE(alice->run("hp = 30").has_value());

        REQUIRE(bob->read_number("hp") == 100);
        REQUIRE(alice->read_number("hp") == 30);
    }

    SECTION("a name written in one is absent from the other") {
        REQUIRE_FALSE(bob->run("secret = 'bob only'").has_value());

        REQUIRE_FALSE(alice->read_string("secret").has_value());
    }

    SECTION("writing from c++ is isolated the same way") {
        bob->write("hp", 100.0);
        alice->write("hp", 30.0);

        REQUIRE(bob->read_number("hp") == 100);
        REQUIRE(alice->read_number("hp") == 30);
    }

    SECTION("nested paths are isolated too") {
        bob->write("stats.strength", 18.0);
        alice->write("stats.strength", 4.0);

        REQUIRE(bob->read_number("stats.strength") == 18);
        REQUIRE(alice->read_number("stats.strength") == 4);
    }

    SECTION("tables are isolated") {
        REQUIRE_FALSE(bob->run("inventory = { 'sword', 'shield' }").has_value());

        REQUIRE(bob->read_data_table("inventory").has_value());
        REQUIRE_FALSE(alice->read_data_table("inventory").has_value());
    }
}

TEST_CASE("environments are isolated from the interpreter's own globals", "[environment]") {
    interpreter interp;

    auto env = interp.make_environment();

    SECTION("what the interpreter writes does not reach an environment") {
        REQUIRE_FALSE(interp.run("shared = 'from the interpreter'").has_value());

        REQUIRE(interp.read_string("shared").has_value());
        REQUIRE_FALSE(env->read_string("shared").has_value());
    }

    SECTION("what an environment writes does not reach the interpreter") {
        REQUIRE_FALSE(env->run("local_to_env = 'from the environment'").has_value());

        REQUIRE_FALSE(interp.read_string("local_to_env").has_value());
    }

    SECTION("an environment made after the interpreter wrote still does not see it") {
        REQUIRE_FALSE(interp.run("written_first = 1").has_value());

        auto later = interp.make_environment();

        REQUIRE_FALSE(later->read_number("written_first").has_value());
    }
}

TEST_CASE("an environment can reach the standard library", "[environment]") {
    interpreter interp;

    auto env = interp.make_environment();

    SECTION("the usual functions are there") {
        REQUIRE_FALSE(env->run("result = tostring(type(math.floor(2.7)))").has_value());

        REQUIRE(env->read_string("result") == "number");
    }

    SECTION("the string metatable still applies, so methods on literals work") {
        REQUIRE_FALSE(env->run("shouted = ('hi'):upper()").has_value());

        REQUIRE(env->read_string("shouted") == "HI");
    }

    SECTION("an environment cannot damage the library for another") {
        REQUIRE_FALSE(env->run("math = nil").has_value());

        auto other = interp.make_environment();

        REQUIRE_FALSE(other->run("still_here = math.floor(2.7)").has_value());
        REQUIRE(other->read_number("still_here") == 2);
    }

    SECTION("_G refers to the environment rather than to the state's globals") {
        REQUIRE_FALSE(env->run("x = 'mine'  via_g = _G.x").has_value());

        REQUIRE(env->read_string("via_g") == "mine");

        REQUIRE_FALSE(interp.run("x = 'the interpreter\\'s'").has_value());
        REQUIRE_FALSE(env->run("via_g_again = _G.x").has_value());

        REQUIRE(env->read_string("via_g_again") == "mine");
    }
}

TEST_CASE("a registered function belongs to one environment", "[environment]") {
    interpreter interp;

    auto bob = interp.make_environment();
    auto alice = interp.make_environment();

    const auto constant = [](const double aValue) {
        return [aValue](value_list_type) -> value_list_type { return {aValue}; };
    };

    SECTION("it is callable where it was registered") {
        bob->register_function("roll", constant(6.0));

        REQUIRE_FALSE(bob->run("result = roll()").has_value());
        REQUIRE(bob->read_number("result") == 6);
    }

    SECTION("and is not callable anywhere else") {
        bob->register_function("roll", constant(6.0));

        REQUIRE(alice->run("result = roll()").has_value());       
        REQUIRE(interp.run("result = roll()").has_value());
    }

    SECTION("two environments can hold different functions under one name") {
        bob->register_function("roll", constant(6.0));
        alice->register_function("roll", constant(1.0));

        REQUIRE_FALSE(bob->run("result = roll()").has_value());
        REQUIRE_FALSE(alice->run("result = roll()").has_value());

        REQUIRE(bob->read_number("result") == 6);
        REQUIRE(alice->read_number("result") == 1);
    }

    SECTION("a function registered on the interpreter stays on the interpreter") {
        interp.register_function("roll", constant(6.0));

        REQUIRE_FALSE(interp.run("result = roll()").has_value());
        REQUIRE(interp.read_number("result") == 6);

        REQUIRE(bob->run("result = roll()").has_value());
    }

    SECTION("a closure keeps its captured state per environment") {
        auto bobCount = std::make_shared<int>(0);
        auto aliceCount = std::make_shared<int>(0);

        const auto counter = [](std::shared_ptr<int> aCount) {
            return [aCount](value_list_type) -> value_list_type { return {static_cast<double>(++*aCount)}; };
        };

        bob->register_function("tick", counter(bobCount));
        alice->register_function("tick", counter(aliceCount));

        REQUIRE_FALSE(bob->run("tick() tick() tick()").has_value());
        REQUIRE_FALSE(alice->run("tick()").has_value());

        REQUIRE(*bobCount == 3);
        REQUIRE(*aliceCount == 1);
    }
}

TEST_CASE("a script's functions stay bound to the environment they were defined in", "[environment]") {
    interpreter interp;

    auto bob = interp.make_environment();
    auto alice = interp.make_environment();

    REQUIRE_FALSE(bob->run("hp = 100  function damage(n) hp = hp - n  return hp end").has_value());
    REQUIRE_FALSE(alice->run("hp = 30  function damage(n) hp = hp - n  return hp end").has_value());

    REQUIRE_FALSE(bob->run("after = damage(10)").has_value());
    REQUIRE_FALSE(alice->run("after = damage(10)").has_value());

    REQUIRE(bob->read_number("after") == 90);
    REQUIRE(alice->read_number("after") == 20);

    REQUIRE(bob->read_number("hp") == 90);
    REQUIRE(alice->read_number("hp") == 20);
}

TEST_CASE("an environment keeps its state between runs", "[environment]") {
    interpreter interp;

    auto env = interp.make_environment();

    REQUIRE_FALSE(env->run("counter = 0").has_value());

    for (int i = 0; i < 5; ++i) REQUIRE_FALSE(env->run("counter = counter + 1").has_value());

    REQUIRE(env->read_number("counter") == 5);
}

TEST_CASE("an environment outlives the interpreter that made it", "[environment]") {
    environment_shared_ptr_type escaped;
    {
        interpreter interp;

        escaped = interp.make_environment();

        REQUIRE_FALSE(escaped->run("survived = 'yes'").has_value());
    }

    REQUIRE(escaped->read_string("survived") == "yes");

    REQUIRE_FALSE(escaped->run("still_runs = math.floor(2.7)").has_value());
    REQUIRE(escaped->read_number("still_runs") == 2);
}

TEST_CASE("destroying an environment does not disturb the others", "[environment]") {
    interpreter interp;

    auto survivor = interp.make_environment();

    REQUIRE_FALSE(survivor->run("hp = 100").has_value());
    {
        auto doomed = interp.make_environment();

        doomed->register_function("noop", [](value_list_type) -> value_list_type { return {}; });

        REQUIRE_FALSE(doomed->run("hp = 1  noop()").has_value());
    }

    REQUIRE_FALSE(interp.run("collectgarbage('collect')").has_value());

    REQUIRE(survivor->read_number("hp") == 100);

    REQUIRE_FALSE(interp.run("collectgarbage('collect')").has_value());
}

TEST_CASE("re-registering a function releases the previous one", "[environment]") {
    interpreter interp;

    auto env = interp.make_environment();

    auto alive = std::make_shared<int>(0);

    for (int i = 0; i < 200; ++i) {
        env->register_function("f", [alive](value_list_type) -> value_list_type { return {}; });

        REQUIRE_FALSE(env->run("f()").has_value());
    }

    REQUIRE_FALSE(interp.run("collectgarbage('collect')").has_value());

    REQUIRE(alive.use_count() >= 1);
}

TEST_CASE("errors in one environment do not affect another", "[environment]") {
    interpreter interp;

    auto broken = interp.make_environment();
    auto healthy = interp.make_environment();

    SECTION("a runtime error is reported and leaves the other usable") {
        REQUIRE(broken->run("error('deliberate')").has_value());

        REQUIRE_FALSE(healthy->run("fine = 1").has_value());
        REQUIRE(healthy->read_number("fine") == 1);
    }

    SECTION("a syntax error is reported and leaves the other usable") {
        REQUIRE(broken->run("this is not lua").has_value());
        REQUIRE(broken->validate_syntax("this is not lua").has_value());

        REQUIRE_FALSE(healthy->validate_syntax("fine = 1").has_value());
        REQUIRE_FALSE(healthy->run("fine = 1").has_value());
        REQUIRE(healthy->read_number("fine") == 1);
    }
}

TEST_CASE("many environments coexist", "[environment]") {
    interpreter interp;

    std::vector<environment_shared_ptr_type> npcs;

    for (int i = 0; i < 50; ++i) {
        auto npc = interp.make_environment();

        REQUIRE_FALSE(npc->run("hp = 100  function hurt(n) hp = hp - n end").has_value());

        npc->write("id", static_cast<double>(i));

        npcs.push_back(npc);
    }

    for (std::size_t i = 0; i < npcs.size(); ++i)
        REQUIRE_FALSE(npcs[i]->run("hurt(" + std::to_string(i) + ")").has_value());

    for (std::size_t i = 0; i < npcs.size(); ++i) {
        INFO("npc " << i);

        REQUIRE(npcs[i]->read_number("id") == static_cast<double>(i));
        REQUIRE(npcs[i]->read_number("hp") == 100.0 - static_cast<double>(i));
    }
}

TEST_CASE("an environment can be entered from inside another", "[environment]") {
    interpreter interp;

    auto npc = interp.make_environment(environment_policy{standard_library::unrestricted});
    auto menu = interp.make_environment(environment_policy{standard_library::unrestricted});

    REQUIRE_FALSE(menu->run("owner = 'the menu'").has_value());
    REQUIRE_FALSE(npc->run("owner = 'the npc'").has_value());

    SECTION("the outer environment is still in force after the inner call returns") {
        npc->register_function("open_menu", [menu](value_list_type) -> value_list_type {
            const auto error = menu->run("opened = true");

            REQUIRE_FALSE(error.has_value());

            return {};
        });

        REQUIRE_FALSE(npc->run(
            "before = owner\n"
            "open_menu()\n"
            "after = owner").has_value());

        REQUIRE(npc->read_string("before") == "the npc");
        REQUIRE(npc->read_string("after") == "the npc");

        REQUIRE(menu->read_boolean("opened") == true);
        REQUIRE_FALSE(npc->read_boolean("opened").has_value());
    }

    SECTION("a value read from the inner environment crosses without dragging its globals along") {
        npc->register_function("ask_menu", [menu](value_list_type) -> value_list_type {
            return {menu->read_string("owner").value_or("<nothing>")};
        });

        REQUIRE_FALSE(npc->run(
            "answer = ask_menu()\n"
            "mine = owner").has_value());

        REQUIRE(npc->read_string("answer") == "the menu");
        REQUIRE(npc->read_string("mine") == "the npc");
    }

    SECTION("code loaded after an inner call still belongs to the outer environment") {
        npc->register_function("open_menu", [menu](value_list_type) -> value_list_type {
            REQUIRE_FALSE(menu->run("opened = true").has_value());

            return {};
        });

        REQUIRE_FALSE(npc->run(
            "open_menu()\n"
            "loadstring('compiled_later = true')()").has_value());

        REQUIRE(npc->read_boolean("compiled_later") == true);
        REQUIRE_FALSE(menu->read_boolean("compiled_later").has_value());
    }

    SECTION("nesting several deep unwinds in the right order") {
        auto inner = interp.make_environment(environment_policy{standard_library::unrestricted});

        REQUIRE_FALSE(inner->run("owner = 'the innermost'").has_value());

        menu->register_function("go_deeper", [inner](value_list_type) -> value_list_type {
            REQUIRE_FALSE(inner->run("reached = owner").has_value());

            return {};
        });

        npc->register_function("open_menu", [menu](value_list_type) -> value_list_type {
            REQUIRE_FALSE(menu->run("go_deeper()  seen = owner").has_value());

            return {};
        });

        REQUIRE_FALSE(npc->run("open_menu()  seen = owner").has_value());

        REQUIRE(inner->read_string("reached") == "the innermost");
        REQUIRE(menu->read_string("seen") == "the menu");
        REQUIRE(npc->read_string("seen") == "the npc");
    }
}

TEST_CASE("the standard library cannot be rewritten from inside an environment", "[environment]") {
    interpreter interp;

    auto vandal = interp.make_environment();
    auto bystander = interp.make_environment();

    SECTION("reaching into a library is refused") {
        const auto error = vandal->run("math.floor = function() return 'hijacked' end");

        REQUIRE(error.has_value());
        REQUIRE(error->find("standard library") != std::string::npos);
    }

    SECTION("and nothing is changed for anyone by the attempt") {
        REQUIRE(vandal->run("math.floor = function() return 'hijacked' end").has_value());

        REQUIRE_FALSE(bystander->run("still = math.floor(2.7)").has_value());
        REQUIRE(bystander->read_number("still") == 2);

        REQUIRE_FALSE(interp.run("still = math.floor(2.7)").has_value());
        REQUIRE(interp.read_number("still") == 2);

        REQUIRE_FALSE(vandal->run("still = math.floor(2.7)").has_value());
        REQUIRE(vandal->read_number("still") == 2);
    }

    SECTION("shadowing the whole name is still allowed, and still contained") {
        REQUIRE_FALSE(vandal->run("math = 'mine now'").has_value());

        REQUIRE(vandal->read_string("math") == "mine now");

        REQUIRE_FALSE(bystander->run("still = math.floor(2.7)").has_value());
        REQUIRE(bystander->read_number("still") == 2);
    }

    SECTION("the library cannot be lifted back out through its metatable") {
        REQUIRE_FALSE(vandal->run("hidden = tostring(getmetatable(math))").has_value());

        REQUIRE(vandal->read_string("hidden") == "false");
    }

    SECTION("the string metatable is not a way round it either") {
        REQUIRE(vandal->run("getmetatable('').__index.upper = function() return 'X' end").has_value());

        REQUIRE_FALSE(bystander->run("shouted = ('hi'):upper()").has_value());
        REQUIRE(bystander->read_string("shouted") == "HI");
    }

    SECTION("reading the library still works normally") {
        REQUIRE_FALSE(vandal->run(
            "a = math.floor(2.7)\n"
            "b = string.format('%d', 7)\n"
            "c = table.concat({'x', 'y'}, '-')\n"
            "d = ('hi'):upper()").has_value());

        REQUIRE(vandal->read_number("a") == 2);
        REQUIRE(vandal->read_string("b") == "7");
        REQUIRE(vandal->read_string("c") == "x-y");
        REQUIRE(vandal->read_string("d") == "HI");
    }
}

TEST_CASE("c++ can add to a library within one environment", "[environment]") {
    interpreter interp;

    auto env = interp.make_environment();
    auto other = interp.make_environment();

    SECTION("registering into a library table succeeds rather than erroring") {
        env->register_function("math.add", [](value_list_type args) -> value_list_type {
            double out = 0;

            for (const auto &arg : args) out += std::get<double>(arg);

            return {out};
        });

        REQUIRE_FALSE(env->run("sum = math.add(2, 3)").has_value());
        REQUIRE(env->read_number("sum") == 5);
    }

    SECTION("and the rest of that library is still reachable") {
        env->register_function("math.add", [](value_list_type) -> value_list_type { return {0.0}; });

        REQUIRE_FALSE(env->run("floored = math.floor(2.7)").has_value());
        REQUIRE(env->read_number("floored") == 2);
    }

    SECTION("and no other environment gains it") {
        env->register_function("math.add", [](value_list_type) -> value_list_type { return {0.0}; });

        REQUIRE(other->run("sum = math.add(2, 3)").has_value());

        REQUIRE_FALSE(other->run("floored = math.floor(2.7)").has_value());
        REQUIRE(other->read_number("floored") == 2);
    }

    SECTION("writing a value into a library path behaves the same way") {
        env->write("math.tau", 6.28);

        REQUIRE(env->read_number("math.tau") == 6.28);
        REQUIRE_FALSE(other->read_number("math.tau").has_value());

        REQUIRE_FALSE(env->run("floored = math.floor(2.7)").has_value());
        REQUIRE(env->read_number("floored") == 2);
    }

    SECTION("a path that is not a library is unaffected") {
        env->write("stats.strength", 18.0);
        env->write("stats.agility", 12.0);

        REQUIRE(env->read_number("stats.strength") == 18);
        REQUIRE(env->read_number("stats.agility") == 12);
    }
}

TEST_CASE("the host is deliberately not restricted", "[environment]") {
    interpreter interp;

    auto env = interp.make_environment();

    REQUIRE_FALSE(interp.run("math.double = function(n) return n * 2 end").has_value());

    REQUIRE_FALSE(env->run("doubled = math.double(4)").has_value());
    REQUIRE(env->read_number("doubled") == 8);
}

TEST_CASE("the nested table route is closed by absence rather than by protection", "[environment]") {
    interpreter interp;

    SECTION("a safe environment has no package to reach through") {
        auto a = interp.make_environment();
        auto b = interp.make_environment();

        REQUIRE(a->run("package.loaded.marker = 'planted by a'").has_value());

        REQUIRE_FALSE(b->run("seen = tostring(package)").has_value());
        REQUIRE(b->read_string("seen") == "nil");
    }

    SECTION("an unrestricted one still shares what is nested, by design") {
        auto a = interp.make_environment(environment_policy{standard_library::unrestricted});
        auto b = interp.make_environment(environment_policy{standard_library::unrestricted});

        REQUIRE_FALSE(a->run("package.loaded.marker = 'planted by a'").has_value());

        REQUIRE_FALSE(b->run("seen = package.loaded.marker").has_value());
        REQUIRE(b->read_string("seen") == "planted by a");

        REQUIRE(a->run("package.path = '/tmp/?.lua'").has_value());
    }
}

TEST_CASE("a safe environment cannot reach the host", "[environment][sandbox]") {
    interpreter interp;

    auto env = interp.make_environment();

    const auto sees = [&env](const std::string &aExpression) {
        REQUIRE_FALSE(env->run("probe = tostring(" + aExpression + ")").has_value());

        return env->read_string("probe").value_or("<none>");
    };

    SECTION("the dangerous libraries are simply absent") {
        for (const auto *const name : {"io", "os.execute", "os.exit", "os.remove", "os.getenv",
            "package", "require", "debug", "loadfile", "dofile", "load", "loadstring", "jit",
            "getfenv", "setfenv", "collectgarbage", "module", "newproxy", "string.dump"}) {
            INFO(name);

            REQUIRE(sees(name) == "nil");
        }
    }

    SECTION("require is gone, which is what actually hides luajit's ffi") {
        REQUIRE(sees("ffi") == "nil");

        REQUIRE(env->run("f = require('ffi')").has_value());
    }

    SECTION("debug is gone, which is what keeps one environment out of the others") {
        REQUIRE(sees("debug") == "nil");
        REQUIRE(env->run("r = debug.getregistry()").has_value());
    }

    SECTION("what a script is left with still does real work") {
        REQUIRE_FALSE(env->run(
            "a = math.floor(2.7)\n"
            "b = string.format('%s-%d', 'x', 7)\n"
            "c = table.concat({'p', 'q'}, ',')\n"
            "d = ('hi'):upper()\n"
            "e = select('#', 1, 2, 3)\n"
            "f = tostring(type({}))\n"
            "g = pcall(function() error('caught') end)\n"
            "h = type(os.time()) == 'number'\n"
            "local co = coroutine.create(function() coroutine.yield(9) end)\n"
            "local ok, y = coroutine.resume(co)\n"
            "i = y").has_value());

        REQUIRE(env->read_number("a") == 2);
        REQUIRE(env->read_string("b") == "x-7");
        REQUIRE(env->read_string("c") == "p,q");
        REQUIRE(env->read_string("d") == "HI");
        REQUIRE(env->read_number("e") == 3);
        REQUIRE(env->read_string("f") == "table");
        REQUIRE(env->read_boolean("g") == false);
        REQUIRE(env->read_boolean("h") == true);
        REQUIRE(env->read_number("i") == 9);
    }

    SECTION("os keeps the clock and loses everything else") {
        REQUIRE_FALSE(env->run("t = type(os.time())  c = type(os.clock())  d = type(os.date())")
            .has_value());

        REQUIRE(env->read_string("t") == "number");
        REQUIRE(env->read_string("c") == "number");
        REQUIRE(env->read_string("d") == "string");

        REQUIRE(sees("os.execute") == "nil");
        REQUIRE(sees("os.exit") == "nil");
    }

    SECTION("string keeps its methods and loses the one that emits bytecode") {
        REQUIRE(sees("string.dump") == "nil");
        REQUIRE_FALSE(env->run("kept = ('a,b'):find(',')").has_value());
        REQUIRE(env->read_number("kept") == 2);
    }

    SECTION("the safe library is read only like any other") {
        REQUIRE(env->run("math.floor = function() return 0 end").has_value());
        REQUIRE(env->run("os.time = function() return 0 end").has_value());
    }
}

TEST_CASE("safe is what an environment gets unless it asks otherwise", "[environment][sandbox]") {
    interpreter interp;

    SECTION("the default is the safe set") {
        auto env = interp.make_environment();

        REQUIRE_FALSE(env->run("probe = tostring(io)").has_value());
        REQUIRE(env->read_string("probe") == "nil");
    }

    SECTION("unrestricted is available for code as trusted as the host") {
        auto env = interp.make_environment(environment_policy{standard_library::unrestricted});

        REQUIRE_FALSE(env->run("probe = tostring(io)").has_value());
        REQUIRE(env->read_string("probe") != "nil");

#ifdef JFC_LUA_LUAJIT
        REQUIRE_FALSE(env->run("probe = tostring(require('ffi').C)").has_value());
        REQUIRE(env->read_string("probe") != "nil");
#endif
    }

    SECTION("the two sets coexist in one interpreter without leaking into each other") {
        auto trusted = interp.make_environment(environment_policy{standard_library::unrestricted});
        auto untrusted = interp.make_environment();

        REQUIRE_FALSE(trusted->run("loaded_io = tostring(io)").has_value());
        REQUIRE(trusted->read_string("loaded_io") != "nil");

        REQUIRE_FALSE(untrusted->run("probe = tostring(io)").has_value());
        REQUIRE(untrusted->read_string("probe") == "nil");
    }

    SECTION("the host itself is unrestricted regardless") {
        REQUIRE_FALSE(interp.run("probe = tostring(io)").has_value());
        REQUIRE(interp.read_string("probe") != "nil");
    }
}
