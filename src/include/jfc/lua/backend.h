// © Joseph Cameron - MIT License

#ifndef JFC_LUA_BACKEND_H
#define JFC_LUA_BACKEND_H

#include <lua.hpp>

#ifndef LUA_OK
    #define LUA_OK 0
#endif

#include <cstddef>
#include <exception>
#include <new>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

/// \brief raises a lua error as luaL_error does, as an expression a c function returns under either dialect: Luau's
/// gives nothing, Lua 5.1's an int. Neither returns. A macro, so the format is checked against what it is given; Luau's
/// luaL_error is a macro too, which MSVC's preprocessor cannot be handed __VA_ARGS__ through, so its function is called
#if defined(JFC_LUA_LUAU)
    #define JFC_LUA_RAISE(...) (luaL_errorL(__VA_ARGS__), 0)
#else
    #define JFC_LUA_RAISE(...) luaL_error(__VA_ARGS__)
#endif

/// \brief what differs between the backends, each in one place: Lua 5.1's interface, on LuaJIT and the reference
/// implementation, and Luau's
namespace jfc::lua::backend {
#if defined(JFC_LUA_LUAU)
    inline constexpr bool LUAU = true;
#else
    inline constexpr bool LUAU = false;
#endif

    /// \brief a c function pushed, called aName where a traceback names it. Luau keeps the pointer, so aName lives as
    /// long as the program does
    inline void push_function(lua_State *const L, const lua_CFunction aFunction, const char *const aName) {
#if defined(JFC_LUA_LUAU)
        lua_pushcfunction(L, aFunction, aName);
#else
        static_cast<void>(aName);

        lua_pushcfunction(L, aFunction);
#endif
    }

    /// \brief a c function pushed with the aUpvalues values on top of the stack as its upvalues
    inline void push_closure(lua_State *const L, const lua_CFunction aFunction, const char *const aName,
        const int aUpvalues) {
#if defined(JFC_LUA_LUAU)
        lua_pushcclosure(L, aFunction, aName, aUpvalues);
#else
        static_cast<void>(aName);

        lua_pushcclosure(L, aFunction, aUpvalues);
#endif
    }

    /// \brief the value on top of the stack popped into the registry: its reference there
    inline int ref(lua_State *const L) {
#if defined(JFC_LUA_LUAU)
        const int out = lua_ref(L, -1);

        lua_pop(L, 1);

        return out;
#else
        return luaL_ref(L, LUA_REGISTRYINDEX);
#endif
    }

    /// \brief a reference given back
    inline void unref(lua_State *const L, const int aReference) {
#if defined(JFC_LUA_LUAU)
        lua_unref(L, aReference);
#else
        luaL_unref(L, LUA_REGISTRYINDEX, aReference);
#endif
    }

    /// \brief room for aSlots more values on the stack, as Lua 5.1's lua_checkstack makes it: false where the stack may
    /// not grow so far, and a lua error where there is no memory for it. Luau's says false for both
    [[nodiscard]] inline bool check_stack(lua_State *const L, const int aSlots) {
#if defined(JFC_LUA_LUAU)
        if (aSlots > LUAI_MAXCSTACK || lua_gettop(L) + aSlots > LUAI_MAXCSTACK) return false;

        lua_rawcheckstack(L, aSlots);

        return true;
#else
        return lua_checkstack(L, aSlots) != 0;
#endif
    }

    /// \brief resumes aThread with the aArguments values on its stack, from aFrom
    inline int resume(lua_State *const aThread, lua_State *const aFrom, const int aArguments) {
#if defined(JFC_LUA_LUAU)
        return lua_resume(aThread, aFrom, aArguments);
#else
        static_cast<void>(aFrom);

        return lua_resume(aThread, aArguments);
#endif
    }

    /// \brief aObject moved into a new full userdata on top of the stack, destroyed when it is collected: by Luau itself,
    /// or under Lua 5.1 by a `__gc` the caller puts in its metatable. Made before the userdata is, since Luau's
    /// destructor is armed from the start: a constructor that threw would leave it nothing made to destroy
    template <typename object_type>
    void new_userdata(lua_State *const L, object_type aObject) {
        static_assert(std::is_nothrow_move_constructible_v<object_type>, "a move that throws leaves nothing made");

#if defined(JFC_LUA_LUAU)
        void *const pStorage = lua_newuserdatadtor(L, sizeof(object_type), [](lua_State *, void *const pObject) {
            static_cast<object_type *>(pObject)->~object_type();
        });
#else
        void *const pStorage = lua_newuserdata(L, sizeof(object_type));
#endif

        new (pStorage) object_type(std::move(aObject));
    }

    /// \brief whether aError is lua's own, raised by the vm on its way to the call protecting it, to be let through:
    /// Luau's errors are std::exceptions, Lua 5.1's never are
    [[nodiscard]] bool is_lua_error(const std::exception &aError);

    /// \brief the name aSource is loaded under, which errors quote: "@" and aName, or where it has none the source
    /// itself, as luaL_loadstring names it
    [[nodiscard]] std::string chunk_name(const std::string &aSource, std::string_view aName);

    /// \brief compiled to what `load_compiled` loads: Luau's bytecode, or under Lua 5.1 the source as it was
    [[nodiscard]] std::string compile(const std::string &aSource);

    /// \brief what `compile` made, loaded under aChunkName as a function of the globals it is loaded among, on top of the
    /// stack: LUA_OK, or another status with the error on top. Luau resolves none of its globals as it loads, though the
    /// environment is safe: each is read when it is read, as under Lua 5.1, and only builtins are called directly
    [[nodiscard]] int load_compiled(lua_State *L, const std::string &aCompiled, const std::string &aChunkName);

    /// \brief aSource compiled and loaded, under aName where it has one. \see load_compiled
    [[nodiscard]] int load(lua_State *L, const std::string &aSource, std::string_view aName);

    /// \brief where the function on top of the stack was written, which is left there: its script's name as the chunk
    /// carries it, and its first line. False for a function not written in lua
    [[nodiscard]] bool defined_at(lua_State *L, std::string &aSource, int &aLine);
}

#endif
