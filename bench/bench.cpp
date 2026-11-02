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

    template <typename body_type>
    void report(const char *const aGroup, const char *const aName, const std::size_t aCalls, body_type &&aBody) {
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

    void script_calling_raw(const char *const aMode) {
        lua_State *const L = luaL_newstate();

        luaL_openlibs(L);

        if (*aMode) luaL_dostring(L, aMode);

        lua_register(L, "none", [](lua_State *) { return 0; });

        lua_register(L, "numbers", [](lua_State *pState) {
            lua_pushnumber(pState, lua_tonumber(pState, 1) + lua_tonumber(pState, 2) + lua_tonumber(pState, 3));

            return 1;
        });

        lua_register(L, "string_in", [](lua_State *pState) {
            std::size_t length = 0;

            lua_tolstring(pState, 1, &length);
            lua_pushnumber(pState, static_cast<lua_Number>(length));

            return 1;
        });

        lua_register(L, "string_out", [](lua_State *pState) {
            lua_pushlstring(pState, "a short name", 12);

            return 1;
        });

        lua_register(L, "table_in", [](lua_State *pState) {
            lua_getfield(pState, 1, "x");

            return 1;
        });

        lua_newtable(L);
        lua_pushcfunction(L, [](lua_State *pState) {
            lua_pushnumber(pState, lua_tonumber(pState, 2) + 2);

            return 1;
        });
        lua_setfield(L, -2, "get");
        lua_setglobal(L, "object");

        for (const auto &[name, line] : SCRIPT_ROWS) {
            luaL_dostring(L, loop_around(line).c_str());

            report("raw", name, SCRIPT_CALLS, [L] {
                lua_getglobal(L, "bench_loop");
                lua_pcall(L, 0, 0, 0);
            });
        }

        lua_close(L);
    }

    void script_calling(const char *const aGroup, const char *const aMode, const bool aTyped) {
        interpreter lua;

        require_nothing(lua.run(aMode));

        auto env = lua.make_environment();

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

        luaL_dostring(L, "function hook(a, b, c) return a end on = { update = function() end }");

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

        luaL_dostring(L, "player = { hitbox = { width = 2 } }");

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

int main() {
    const std::string_view backend(JFC_LUA_BENCH_BACKEND);

    const char *const modes[][2] = {{"jit on", ""}, {"interpreter", "jit.off()"}};

    for (std::size_t i = 0; i < (backend == "luajit" ? 2u : 1u); ++i) {
        std::printf("a script calling c++, %s, %s\n", JFC_LUA_BENCH_BACKEND, backend == "luajit" ? modes[i][0] : "");

        script_calling_raw(modes[i][1]);
        script_calling("typed", modes[i][1], true);
        script_calling("value list", modes[i][1], false);
    }

    std::printf("c++ calling a script, %s\n", JFC_LUA_BENCH_BACKEND);

    host_calling_raw();
    host_calling();

    std::printf("c++ reading a value, %s\n", JFC_LUA_BENCH_BACKEND);

    reading_raw();
    reading();
}
