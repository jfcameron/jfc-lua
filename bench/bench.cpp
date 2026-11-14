// © Joseph Cameron - MIT License

#include <jfc/lua.h>

#include <lua.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

using namespace jfc::lua;

namespace {
    constexpr std::size_t PASSES = 3;
    constexpr std::size_t SCRIPT_CALLS = 500000;
    constexpr std::size_t HOST_CALLS = 200000;

    std::size_t allocations = 0;
}

void *operator new(const std::size_t aSize) {
    ++allocations;

    if (auto *const pMemory = std::malloc(aSize ? aSize : 1)) return pMemory;

    throw std::bad_alloc();
}

void operator delete(void *const pMemory) noexcept { std::free(pMemory); }
void operator delete(void *const pMemory, std::size_t) noexcept { std::free(pMemory); }

namespace {
    struct thing final {
        double value = 2;
    };

    struct row final {
        const char *name;
        const char *line;
    };

    /// \brief what each binding is called with. Every result reaches sink, so none of it can be dropped.
    const row SCRIPT_ROWS[] = {
        {"no arguments", "none()"},
        {"three numbers", "s = s + numbers(i, 2, 3)"},
        {"string in", "s = s + string_in(text)"},
        {"string out", "s = s + #string_out()"},
        {"small table in", "s = s + table_in(fields)"},
        {"method", "s = s + object:get(i)"},
    };

    //! a turn of module-like work: fields read and written, arithmetic, a branch, a lua call, a c call, a string made
    constexpr const char *WORKLOAD = R"(
        local cells = {}
        for i = 1, 64 do cells[i] = { x = i, y = i * 2 } end
        local function weigh(c) return c.x * 0.5 + c.y end
        function turn()
            local total = 0
            for i = 1, #cells do
                local c = cells[i]
                if c.x % 3 == 0 then total = total + weigh(c) else total = total - 1 end
                c.x = c.x + 1
            end
            return total + #tostring(total) + numbers(1, 2, 3)
        end
    )";

    //! the best of PASSES, in nanoseconds a call
    template <typename body_type>
    double report(const char *const aGroup, const char *const aName, const std::size_t aCalls, body_type &&aBody) {
        double best = std::numeric_limits<double>::max();
        double allocated = 0;

        for (std::size_t pass = 0; pass < PASSES; ++pass) {
            const auto allocationsBefore = allocations;
            const auto began = std::chrono::steady_clock::now();

            aBody();

            const std::chrono::duration<double, std::nano> took = std::chrono::steady_clock::now() - began;

            best = std::min(best, took.count() / static_cast<double>(aCalls));
            allocated = static_cast<double>(allocations - allocationsBefore) / static_cast<double>(aCalls);
        }

        std::printf("  %-11s %-24s %9.1f ns %7.2f allocs\n", aGroup, aName, best, allocated);

        return best;
    }

    [[nodiscard]] std::string loop_around(const char *const aLine) {
        return "function bench_loop() local text, fields, s = 'a short name', { x = 1, y = 2, z = 3 }, 0 for i = 1, "
            + std::to_string(SCRIPT_CALLS) + " do " + aLine + " end sink = s end";
    }

    void require_nothing(const error_type &aError) {
        if (!aError) return;

        std::fprintf(stderr, "bench failed: %s\n", aError->c_str());
        std::exit(EXIT_FAILURE);
    }

    //! aSource run in L, as luaL_dostring runs it
    void raw_run(lua_State *const L, const std::string &aSource) {
#if defined(JFC_LUA_LUAU)
        std::size_t size = 0;

        char *const bytecode = luau_compile(aSource.c_str(), aSource.size(), nullptr, &size);

        const int loaded = luau_load(L, "=bench", bytecode, size, 0);

        std::free(bytecode);
#else
        const int loaded = luaL_loadstring(L, aSource.c_str());
#endif

        if (loaded != 0 || lua_pcall(L, 0, 0, 0) != 0) {
            std::fprintf(stderr, "bench failed: %s\n", lua_tostring(L, -1));
            std::exit(EXIT_FAILURE);
        }
    }

    //! aFunction pushed as a c function
    void raw_push(lua_State *const L, const lua_CFunction aFunction) {
#if defined(JFC_LUA_LUAU)
        lua_pushcfunction(L, aFunction, "bench");
#else
        lua_pushcfunction(L, aFunction);
#endif
    }

    //! aFunction made the global aName
    void raw_register(lua_State *const L, const char *const aName, const lua_CFunction aFunction) {
        raw_push(L, aFunction);
        lua_setglobal(L, aName);
    }

    int raw_numbers(lua_State *const pState) {
        lua_pushnumber(pState, lua_tonumber(pState, 1) + lua_tonumber(pState, 2) + lua_tonumber(pState, 3));

        return 1;
    }

    void script_calling_raw(const char *const aMode) {
        lua_State *const L = luaL_newstate();

        luaL_openlibs(L);

        if (*aMode) raw_run(L, aMode);

        raw_register(L, "none", [](lua_State *) { return 0; });

        raw_register(L, "numbers", raw_numbers);

        raw_register(L, "string_in", [](lua_State *pState) {
            std::size_t length = 0;

            lua_tolstring(pState, 1, &length);
            lua_pushnumber(pState, static_cast<lua_Number>(length));

            return 1;
        });

        raw_register(L, "string_out", [](lua_State *pState) {
            lua_pushlstring(pState, "a short name", 12);

            return 1;
        });

        raw_register(L, "table_in", [](lua_State *pState) {
            lua_getfield(pState, 1, "x");

            return 1;
        });

        lua_newtable(L);
        raw_push(L, [](lua_State *pState) {
            lua_pushnumber(pState, lua_tonumber(pState, 2) + 2);

            return 1;
        });
        lua_setfield(L, -2, "get");
        lua_setglobal(L, "object");

        for (const auto &[name, line] : SCRIPT_ROWS) {
            raw_run(L, loop_around(line));

            report("raw", name, SCRIPT_CALLS, [L] {
                lua_getglobal(L, "bench_loop");
                lua_pcall(L, 0, 0, 0);
            });
        }

        lua_close(L);
    }

    //! under aBudget where there is one, which stops its clock for every call into c++
    void script_calling(const char *const aGroup, const char *const aMode, const bool aTyped,
        const std::optional<std::chrono::nanoseconds> aBudget = {}) {
        interpreter lua;

        require_nothing(lua.run(aMode));

        auto env = lua.make_environment(environment_policy{standard_library::safe, aBudget});

        if (aTyped) {
            env->register_function("none", [] {});
            env->register_function("numbers", [](const double a, const double b, const double c) { return a + b + c; });
            env->register_function("string_in", [](const std::string_view aText) { return aText.size(); });
            env->register_function("string_out", [] { return std::string_view("a short name"); });
            env->register_function("table_in", [](const table_view &aTable) { return *aTable.get_number("x"); });
            env->register_function("function_in", [](const function_reference &) {});

            lua.register_type<thing>("thing", method("get", [](const thing &aThing, const double aValue) {
                return aThing.value + aValue;
            }));
        }
        else {
            env->register_function("none", [](value_list_type) -> value_list_type { return {}; });

            env->register_function("numbers", [](value_list_type aArguments) -> value_list_type {
                return {std::get<double>(aArguments[0]) + std::get<double>(aArguments[1]) + std::get<double>(aArguments[2])};
            });

            env->register_function("string_in", [](value_list_type aArguments) -> value_list_type {
                return {static_cast<double>(std::get<std::string>(aArguments[0]).size())};
            });

            env->register_function("string_out", [](value_list_type) -> value_list_type {
                return {std::string("a short name")};
            });

            env->register_function("table_in", [](value_list_type aArguments) -> value_list_type {
                return {*std::get<data_table>(aArguments[0]).get_number("x")};
            });

            lua.register_type<thing>("thing", {
                {"get", [](thing &aThing, value_list_type aArguments) -> value_list_type {
                    return {aThing.value + std::get<double>(aArguments[0])};
                }},
            });
        }

        env->write("object", std::make_shared<thing>());

        const auto run = [&](const char *const aName, const char *const aLine) {
            require_nothing(env->run(loop_around(aLine)));

            report(aGroup, aName, SCRIPT_CALLS, [&] { require_nothing(env->call("bench_loop")); });
        };

        for (const auto &[name, line] : SCRIPT_ROWS) run(name, line);

        if (aTyped) run("function in", "function_in(print)");
    }

    void host_calling_raw() {
        lua_State *const L = luaL_newstate();

        luaL_openlibs(L);

        raw_run(L, "function hook(a, b, c) return a end on = { update = function() end }");

        double total = 0;

        report("raw", "three numbers, a result", HOST_CALLS, [&] {
            for (std::size_t i = 0; i < HOST_CALLS; ++i) {
                lua_getglobal(L, "hook");
                lua_pushnumber(L, 1);
                lua_pushnumber(L, 2);
                lua_pushnumber(L, 3);
                lua_pcall(L, 3, LUA_MULTRET, 0);

                total += lua_tonumber(L, -1);

                lua_settop(L, 0);
            }
        });

        report("raw", "on.update, nothing", HOST_CALLS, [&] {
            for (std::size_t i = 0; i < HOST_CALLS; ++i) {
                lua_getglobal(L, "on");
                lua_getfield(L, -1, "update");
                lua_pcall(L, 0, LUA_MULTRET, 0);
                lua_settop(L, 0);
            }
        });

        std::printf("  %-11s %-24s %9.1f\n", "", "(sink)", total);

        lua_close(L);
    }

    void host_calling() {
        interpreter lua;

        auto env = lua.make_environment();

        require_nothing(env->run("function hook(a, b, c) return a end on = { update = function() end }"));

        const value_list_type arguments{1.0, 2.0, 3.0};

        value_list_type results;

        report("environment", "three numbers, a result", HOST_CALLS, [&] {
            for (std::size_t i = 0; i < HOST_CALLS; ++i) require_nothing(env->call("hook", arguments, results));
        });

        report("fresh lists", "three numbers, a result", HOST_CALLS, [&] {
            for (std::size_t i = 0; i < HOST_CALLS; ++i) {
                value_list_type fresh;

                require_nothing(env->call("hook", {1.0, 2.0, 3.0}, fresh));
            }
        });

        double total = 0;

        report("typed", "three numbers, a result", HOST_CALLS, [&] {
            for (std::size_t i = 0; i < HOST_CALLS; ++i) total += *env->call<double>("hook", 1.0, 2.0, 3.0);
        });

        const path hook("hook");

        report("built path", "three numbers, a result", HOST_CALLS, [&] {
            for (std::size_t i = 0; i < HOST_CALLS; ++i) require_nothing(env->call(hook, arguments, results));
        });

        auto held = *env->read_reference("hook");

        report("reference", "three numbers, a result", HOST_CALLS, [&] {
            for (std::size_t i = 0; i < HOST_CALLS; ++i) require_nothing(held.call(arguments, results));
        });

        report("typed ref", "three numbers, a result", HOST_CALLS, [&] {
            for (std::size_t i = 0; i < HOST_CALLS; ++i) total += *held.call<double>(1.0, 2.0, 3.0);
        });

        report("fresh ref", "three numbers, a result", HOST_CALLS, [&] {
            for (std::size_t i = 0; i < HOST_CALLS; ++i) {
                value_list_type fresh;

                require_nothing(held.call({1.0, 2.0, 3.0}, fresh));
            }
        });

        report("environment", "on.update, nothing", HOST_CALLS, [&] {
            for (std::size_t i = 0; i < HOST_CALLS; ++i) require_nothing(env->call("on.update"));
        });

        const path update("on.update");

        report("built path", "on.update, nothing", HOST_CALLS, [&] {
            for (std::size_t i = 0; i < HOST_CALLS; ++i) require_nothing(env->call(update));
        });

        auto heldUpdate = *env->read_reference("on.update");

        report("reference", "on.update, nothing", HOST_CALLS, [&] {
            for (std::size_t i = 0; i < HOST_CALLS; ++i) require_nothing(heldUpdate.call());
        });

        std::printf("  %-11s %-24s %9.1f\n", "", "(sink)", total);
    }

    void reading_raw() {
        lua_State *const L = luaL_newstate();

        luaL_openlibs(L);

        raw_run(L, "player = { hitbox = { width = 2 } }");

        double total = 0;

        report("raw", "player.hitbox.width", HOST_CALLS, [&] {
            for (std::size_t i = 0; i < HOST_CALLS; ++i) {
                lua_getglobal(L, "player");
                lua_getfield(L, -1, "hitbox");
                lua_getfield(L, -1, "width");

                total += lua_tonumber(L, -1);

                lua_settop(L, 0);
            }
        });

        std::printf("  %-11s %-24s %9.1f\n", "", "(sink)", total);

        lua_close(L);
    }

    void reading() {
        interpreter lua;

        auto env = lua.make_environment();

        require_nothing(env->run("player = { hitbox = { width = 2 } }"));

        double total = 0;

        report("string", "player.hitbox.width", HOST_CALLS, [&] {
            for (std::size_t i = 0; i < HOST_CALLS; ++i) total += *env->read_number("player.hitbox.width");
        });

        const path width("player.hitbox.width");

        report("built path", "player.hitbox.width", HOST_CALLS, [&] {
            for (std::size_t i = 0; i < HOST_CALLS; ++i) total += *env->read_number(width);
        });

        report("parse only", "player.hitbox.width", HOST_CALLS, [&] {
            for (std::size_t i = 0; i < HOST_CALLS; ++i) total += static_cast<double>(path("player.hitbox.width").size());
        });

        std::printf("  %-11s %-24s %9.1f\n", "", "(sink)", total);
    }
}

namespace {
#if !defined(JFC_LUA_LUAU)
    std::size_t counted = 0;

    void count_instruction(lua_State *, lua_Debug *) { ++counted; }
#endif

    //! how long a turn of WORKLOAD takes, and under Lua 5.1 how many vm instructions it is where aCounted: what a
    //! million took, which the instruction budget was counted in. LuaJIT's compiled code is not counted
    void a_million_instructions(const char *const aMode, [[maybe_unused]] const bool aCounted) {
        constexpr std::size_t TURNS = 20000;

        lua_State *const L = luaL_newstate();

        luaL_openlibs(L);

        if (*aMode) raw_run(L, aMode);

        raw_register(L, "numbers", raw_numbers);
        raw_run(L, WORKLOAD);

#if defined(JFC_LUA_LUAU)
        lua_setsafeenv(L, LUA_GLOBALSINDEX, true);
#endif

        const auto turns = [L](const std::size_t aTurns) {
            for (std::size_t i = 0; i < aTurns; ++i) {
                lua_getglobal(L, "turn");
                lua_pcall(L, 0, 1, 0);
                lua_settop(L, 0);
            }
        };

        const double perTurn = report("raw", "a turn of module work", TURNS, [&] { turns(TURNS); });

#if defined(JFC_LUA_LUAU)
        static_cast<void>(perTurn);
#else
        if (aCounted) {
            constexpr std::size_t COUNTED_TURNS = 100;

            counted = 0;

            lua_sethook(L, count_instruction, LUA_MASKCOUNT, 1);
            turns(COUNTED_TURNS);
            lua_sethook(L, nullptr, 0, 0);

            const double instructions = static_cast<double>(counted) / static_cast<double>(COUNTED_TURNS);

            std::printf("  %-11s %-24s %9.1f\n", "", "instructions a turn", instructions);
            std::printf("  %-11s %-24s %9.2f ms\n", "", "a million instructions", perTurn / instructions);
        }
#endif

        lua_close(L);
    }

    //! a turn of WORKLOAD as an engine runs a module's: in an environment, under a time budget, called from c++
    void a_turn_in_an_environment() {
        constexpr std::size_t TURNS = 20000;

        interpreter lua;

        auto env = lua.make_environment(environment_policy{standard_library::safe, std::chrono::seconds(10)});

        env->register_function("numbers", [](const double aA, const double aB, const double aC) {
            return aA + aB + aC;
        });

        require_nothing(env->run(WORKLOAD));

        report("environment", "a turn of module work", TURNS, [&] {
            for (std::size_t i = 0; i < TURNS; ++i) static_cast<void>(env->call<double>("turn"));
        });
    }
}

int main() {
    const std::string_view backend(JFC_LUA_BENCH_BACKEND);

    const char *const modes[][2] = {{"jit on", ""}, {"interpreter", "jit.off()"}};

    for (std::size_t i = 0; i < (backend == "luajit" ? 2u : 1u); ++i) {
        std::printf("a script calling c++, %s, %s\n", JFC_LUA_BENCH_BACKEND, backend == "luajit" ? modes[i][0] : "");

        script_calling_raw(modes[i][1]);
        script_calling("typed", modes[i][1], true);
        script_calling("value list", modes[i][1], false);
        script_calling("budgeted", modes[i][1], true, std::chrono::seconds(10));
    }

    std::printf("c++ calling a script, %s\n", JFC_LUA_BENCH_BACKEND);

    host_calling_raw();
    host_calling();

    std::printf("c++ reading a value, %s\n", JFC_LUA_BENCH_BACKEND);

    reading_raw();
    reading();

    for (std::size_t i = 0; i < (backend == "luajit" ? 2u : 1u); ++i) {
        std::printf("a million instructions, %s, %s\n", JFC_LUA_BENCH_BACKEND, backend == "luajit" ? modes[i][0] : "");

        a_million_instructions(modes[i][1], backend != "luajit" || i == 1);
    }

    std::printf("a turn of module work in a budgeted environment, %s\n", JFC_LUA_BENCH_BACKEND);

    a_turn_in_an_environment();
}
