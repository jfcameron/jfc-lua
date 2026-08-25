// © Joseph Cameron - All Rights Reserved

#include <jfc/lua/reference.h>

#include <lua.hpp>

#include <utility>

namespace jfc::lua {
    reference::reference(std::shared_ptr<lua_State> aState, const int aRegistryReference)
    : m_pState(std::move(aState))
    , m_pReference(new int(aRegistryReference),
        [state = m_pState](const int *const aReference) {
            luaL_unref(state.get(), LUA_REGISTRYINDEX, *aReference);

            delete aReference;
        })
    {}

    reference::~reference() = default;

    lua_State *reference::state() const { return m_pState.get(); }

    int reference::registry_reference() const { return *m_pReference; }

    std::string reference::type_name() const {
        auto *const L = m_pState.get();

        lua_rawgeti(L, LUA_REGISTRYINDEX, *m_pReference);

        const std::string out = lua_typename(L, lua_type(L, -1));

        lua_pop(L, 1);

        return out;
    }

    bool reference::is_table() const { return type_name() == "table"; }

    bool reference::is_function() const { return type_name() == "function"; }

    bool reference::operator==(const reference &a) const {
        if (m_pState != a.m_pState) return false;

        if (m_pReference == a.m_pReference) return true;

        auto *const L = m_pState.get();

        lua_rawgeti(L, LUA_REGISTRYINDEX, *m_pReference);
        lua_rawgeti(L, LUA_REGISTRYINDEX, *a.m_pReference);

        const bool same = lua_rawequal(L, -1, -2);

        lua_pop(L, 2);

        return same;
    }
}
