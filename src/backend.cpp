// © Joseph Cameron - MIT License

#include <jfc/lua/backend.h>

#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <typeindex>
#include <typeinfo>

namespace jfc::lua::backend {
    bool is_lua_error(const std::exception &aError) {
#if defined(JFC_LUA_LUAU)
        static const std::type_index ERROR_TYPE = [] {
            std::optional<std::type_index> learnt;

            lua_State *const L = luaL_newstate();

            static_cast<void>(lua_cpcall(L, [](lua_State *const pState) -> int {
                auto *const pLearnt = static_cast<std::optional<std::type_index> *>(lua_touserdata(pState, 1));

                try { luaL_error(pState, "an error raised to learn its type"); }
                catch (const std::exception &e) {
                    pLearnt->emplace(typeid(e));

                    throw;
                }
            }, &learnt));

            lua_close(L);

            return learnt.value_or(std::type_index(typeid(void)));
        }();

        return std::type_index(typeid(aError)) == ERROR_TYPE;
#else
        static_cast<void>(aError);

        return false;
#endif
    }

    std::string chunk_name(const std::string &aSource, const std::string_view aName) {
        return aName.empty() ? aSource : "@" + std::string(aName);
    }

    std::string compile(const std::string &aSource) {
#if defined(JFC_LUA_LUAU)
        lua_CompileOptions options{};

        options.optimizationLevel = 1;
        options.debugLevel = 1;

        std::size_t size = 0;

        const std::unique_ptr<char, decltype(&std::free)> bytecode(
            luau_compile(aSource.c_str(), std::strlen(aSource.c_str()), &options, &size), &std::free);

        return bytecode ? std::string(bytecode.get(), size) : std::string();
#else
        return aSource;
#endif
    }

    int load_compiled(lua_State *const L, const std::string &aCompiled, const std::string &aChunkName) {
#if defined(JFC_LUA_LUAU)
        lua_pushvalue(L, LUA_GLOBALSINDEX);
        lua_newtable(L);
        lua_replace(L, LUA_GLOBALSINDEX);

        const int status = luau_load(L, aChunkName.c_str(), aCompiled.data(), aCompiled.size(), -1);

        lua_pushvalue(L, -2);
        lua_replace(L, LUA_GLOBALSINDEX);
        lua_remove(L, -2);

        return status;
#else
        return luaL_loadbuffer(L, aCompiled.c_str(), std::strlen(aCompiled.c_str()), aChunkName.c_str());
#endif
    }

    int load(lua_State *const L, const std::string &aSource, const std::string_view aName) {
#if defined(JFC_LUA_LUAU)
        return load_compiled(L, compile(aSource), chunk_name(aSource, aName));
#else
        return load_compiled(L, aSource, chunk_name(aSource, aName));
#endif
    }

    bool defined_at(lua_State *const L, std::string &aSource, int &aLine) {
        lua_Debug about{};

#if defined(JFC_LUA_LUAU)
        if (!lua_getinfo(L, -1, "s", &about)) return false;
#else
        lua_pushvalue(L, -1);

        if (!lua_getinfo(L, ">S", &about)) return false;
#endif

        if (!about.what || std::strcmp(about.what, "C") == 0) return false;

        aSource = about.source ? about.source : "";
        aLine = about.linedefined;

        return true;
    }
}
