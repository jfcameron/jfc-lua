// © Joseph Cameron - All Rights Reserved

#include <jfc/lua/internal.h>

#include <jfc/lua/exception.h>
#include <jfc/lua/interpreter.h>
#include <jfc/lua/userdata.h>

#include <cstdio>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace {
    //! the metatable of a box whose type has none registered
    constexpr const char *GENERIC_METATABLE = "jfc.lua.userdata";

    const char OURS = 0;    //!< marks every metatable of ours
    const char TYPES = 0;   //!< registry: type key -> that type's metatable
    const char BOXES = 0;   //!< registry: type key -> weak table of address -> box
    const char METHODS = 0; //!< in a registered type's metatable: its methods table

    const std::string CALL = "__call";

    void _push_key(lua_State *const L, const void *const aKey) {
        lua_pushlightuserdata(L, const_cast<void *>(aKey));
    }

    int _destroy_box(lua_State *L) {
        auto *const box = static_cast<jfc::lua::userdata *>(lua_touserdata(L, 1));

        box->~userdata();

        new (box) jfc::lua::userdata();

        return 0;
    }

    void _push_registry_table(lua_State *const L, const void *const aKey, const char *const aMode) {
        _push_key(L, aKey);
        lua_rawget(L, LUA_REGISTRYINDEX);

        if (lua_istable(L, -1)) return;

        lua_pop(L, 1);

        lua_newtable(L);

        if (aMode) {
            lua_newtable(L);
            lua_pushstring(L, aMode);
            lua_setfield(L, -2, "__mode");
            lua_setmetatable(L, -2);
        }

        _push_key(L, aKey);
        lua_pushvalue(L, -2);
        lua_rawset(L, LUA_REGISTRYINDEX);
    }

    void _fill_common(lua_State *const L) {
        lua_pushcfunction(L, _destroy_box);
        lua_setfield(L, -2, "__gc");

        lua_pushboolean(L, 0);
        lua_setfield(L, -2, "__metatable");

        _push_key(L, &OURS);
        lua_pushboolean(L, 1);
        lua_rawset(L, -3);
    }

    void _push_generic_metatable(lua_State *const L) {
        if (luaL_newmetatable(L, GENERIC_METATABLE)) _fill_common(L);
    }

    [[nodiscard]] bool _push_type_metatable(lua_State *const L, const void *const aTypeKey) {
        _push_registry_table(L, &TYPES, nullptr);
        _push_key(L, aTypeKey);
        lua_rawget(L, -2);
        lua_remove(L, -2);

        if (lua_istable(L, -1)) return true;

        lua_pop(L, 1);

        return false;
    }

    int _index(lua_State *L) {
        lua_pushvalue(L, 2);
        lua_rawget(L, lua_upvalueindex(1));

        if (!lua_isnil(L, -1)) return 1;

        lua_pop(L, 1);

        lua_getfenv(L, 1);

        if (!lua_istable(L, -1)) return 0;

        lua_pushvalue(L, 2);
        lua_rawget(L, -2);

        return 1;
    }

    int _newindex(lua_State *L) {
        lua_pushvalue(L, 2);
        lua_rawget(L, lua_upvalueindex(1));

        if (!lua_isnil(L, -1)) {
            const char *const key = lua_tostring(L, 2);

            return luaL_error(L, "%s is a method of %s and cannot be assigned to",
                key ? key : "that", lua_tostring(L, lua_upvalueindex(2)));
        }

        lua_pop(L, 1);

        lua_getfenv(L, 1);

        lua_pushvalue(L, 2);
        lua_pushvalue(L, 3);
        lua_rawset(L, -3);

        return 0;
    }

    int _tostring(lua_State *L) {
        const auto *const box = static_cast<const jfc::lua::userdata *>(lua_touserdata(L, 1));

        lua_pushfstring(L, "%s: %p", lua_tostring(L, lua_upvalueindex(1)),
            box ? box->address() : nullptr);

        return 1;
    }

    int _method_trampoline(lua_State *p) {
        const auto *const self = jfc::lua::detail::to_userdata(p, 1);

        const void *const typeKey = lua_touserdata(p, lua_upvalueindex(2));

        if (!self || self->empty() || self->type_key() != typeKey)
            return luaL_error(p, "%s is a method of %s: call it on one, with a colon -- "
                "object:%s(...)", lua_tostring(p, lua_upvalueindex(4)),
                lua_tostring(p, lua_upvalueindex(3)), lua_tostring(p, lua_upvalueindex(4)));

        char failure[256] = {};

        bool failed = false;

        int results = 0;

        try {
            auto *pMethod = static_cast<jfc::lua::interpreter::untyped_method_type *>(
                lua_touserdata(p, lua_upvalueindex(1)));

            const jfc::lua::userdata object = *self;

            jfc::lua::value_list_type args;

            for (int i = 2; i <= lua_gettop(p); ++i) {
                if (jfc::lua::detail::to_param(p, i, args)) continue;

                throw jfc::lua::exception("argument " + std::to_string(i - 1) + " is a "
                    + lua_typename(p, lua_type(p, i)) + ", which cannot cross into c++");
            }

            auto returned = (*pMethod)(object, std::move(args));

            lua_settop(p, 0);

            if (!lua_checkstack(p, static_cast<int>(returned.size())))
                throw jfc::lua::exception("the lua stack cannot grow enough to return this many "
                    "values");

            for (const auto &value : returned) jfc::lua::detail::push_param(p, value);

            results = static_cast<int>(returned.size());
        }
        catch (const std::exception &e) {
            std::snprintf(failure, sizeof failure, "%s", e.what());

            failed = true;
        }
        catch (...) {
            std::snprintf(failure, sizeof failure, "%s",
                "a method threw something that is not a std::exception");

            failed = true;
        }

        if (failed) return luaL_error(p, "%s", failure);

        return results;
    }

    int _destroy_method(lua_State *p) {
        using method_type = jfc::lua::interpreter::untyped_method_type;

        static_cast<method_type *>(lua_touserdata(p, 1))->~method_type();

        return 0;
    }
}

namespace jfc::lua::detail {
    void push_userdata(lua_State *const L, const userdata &aValue) {
        _push_registry_table(L, &BOXES, nullptr);
        _push_key(L, aValue.type_key());
        lua_rawget(L, -2);

        if (!lua_istable(L, -1)) {
            lua_pop(L, 1);

            _push_registry_table(L, &BOXES, nullptr);
            lua_pop(L, 1);

            lua_newtable(L);
            lua_newtable(L);
            lua_pushstring(L, "v");
            lua_setfield(L, -2, "__mode");
            lua_setmetatable(L, -2);

            _push_key(L, aValue.type_key());
            lua_pushvalue(L, -2);
            lua_rawset(L, -4);
        }

        lua_remove(L, -2);

        if (!aValue.empty()) {
            _push_key(L, aValue.address());
            lua_rawget(L, -2);

            if (const auto *const existing = static_cast<const userdata *>(lua_touserdata(L, -1));
                existing && lua_type(L, -1) == LUA_TUSERDATA && *existing == aValue) {
                lua_remove(L, -2);

                return;
            }

            lua_pop(L, 1);
        }

        new (lua_newuserdata(L, sizeof(userdata))) userdata(aValue);

        if (_push_type_metatable(L, aValue.type_key())) {
            lua_setmetatable(L, -2);

            lua_newtable(L);
            lua_setfenv(L, -2);
        }
        else {
            _push_generic_metatable(L);
            lua_setmetatable(L, -2);
        }

        if (!aValue.empty()) {
            _push_key(L, aValue.address());
            lua_pushvalue(L, -2);
            lua_rawset(L, -4);
        }

        lua_remove(L, -2);
    }

    const userdata *to_userdata(lua_State *const L, const int aIndex) {
        if (lua_type(L, aIndex) != LUA_TUSERDATA) return nullptr;

        if (!lua_getmetatable(L, aIndex)) return nullptr;

        _push_key(L, &OURS);
        lua_rawget(L, -2);

        const bool ours = lua_toboolean(L, -1);

        lua_pop(L, 2);

        if (!ours) return nullptr;

        return static_cast<const userdata *>(lua_touserdata(L, aIndex));
    }

    bool indexable_userdata(lua_State *const L, const int aIndex) {
        if (!to_userdata(L, aIndex)) return false;

        lua_getmetatable(L, aIndex);
        lua_getfield(L, -1, "__index");

        const bool indexable = !lua_isnil(L, -1);

        lua_pop(L, 2);

        return indexable;
    }

    bool push_field_table(lua_State *const L, const int aBox, const int aKey) {
        if (!indexable_userdata(L, aBox)) return false;

        lua_getmetatable(L, aBox);
        _push_key(L, &METHODS);
        lua_rawget(L, -2);

        lua_pushvalue(L, aKey);
        lua_rawget(L, -2);

        const bool method = !lua_isnil(L, -1);

        lua_pop(L, 3);

        if (method) return false;

        lua_getfenv(L, aBox);

        return true;
    }
}

namespace jfc::lua {
    void interpreter::_register_type(std::string aName, const void *const aTypeKey,
        std::vector<std::pair<std::string, untyped_method_type>> aMethods) {
        auto *const L = m_pState.get();

        const stack_guard guard(L);

        lua_newtable(L); 
        _fill_common(L);

        const int metatable = lua_gettop(L);

        lua_newtable(L); 

        const int methods = lua_gettop(L);

        for (auto &[name, method] : aMethods) {
            auto *const pStorage = static_cast<untyped_method_type *>(
                lua_newuserdata(L, sizeof(untyped_method_type)));

            new (pStorage) untyped_method_type(std::move(method));

            lua_newtable(L);
            lua_pushcfunction(L, _destroy_method);
            lua_setfield(L, -2, "__gc");
            lua_setmetatable(L, -2);

            _push_key(L, aTypeKey);
            lua_pushstring(L, aName.c_str());
            lua_pushstring(L, name.c_str());

            lua_pushcclosure(L, _method_trampoline, 4);

            lua_setfield(L, name == CALL ? metatable : methods, name.c_str());
        }

        _push_key(L, &METHODS);
        lua_pushvalue(L, methods);
        lua_rawset(L, metatable);

        lua_pushvalue(L, methods);
        lua_pushcclosure(L, _index, 1);
        lua_setfield(L, metatable, "__index");

        lua_pushvalue(L, methods);
        lua_pushstring(L, aName.c_str());
        lua_pushcclosure(L, _newindex, 2);
        lua_setfield(L, metatable, "__newindex");

        lua_pushstring(L, aName.c_str());
        lua_pushcclosure(L, _tostring, 1);
        lua_setfield(L, metatable, "__tostring");

        _push_registry_table(L, &TYPES, nullptr);
        _push_key(L, aTypeKey);
        lua_pushvalue(L, metatable);
        lua_rawset(L, -3);
    }

    reference interpreter::hold(const userdata &aObject) {
        auto *const L = m_pState.get();

        const stack_guard guard(L);

        detail::push_userdata(L, aObject);

        return reference(m_pState, luaL_ref(L, LUA_REGISTRYINDEX));
    }
}
