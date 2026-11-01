// © Joseph Cameron - MIT License

#include <jfc/lua/internal.h>

#include <jfc/lua/reference.h>

#include <lua.hpp>

#include <utility>

namespace jfc::lua {
    reference::reference(std::shared_ptr<lua_State> aState, const int aRegistryReference)
    : m_pState(std::move(aState))
    , m_pReference(new int(aRegistryReference),
        [state = m_pState](const int *const aReference) {
            {
                const unbudgeted_scope unbudgeted(state.get());

                luaL_unref(state.get(), LUA_REGISTRYINDEX, *aReference);
            }

            delete aReference;
        })
    {}

    reference::~reference() = default;

    function_reference::function_reference(std::shared_ptr<lua_State> aState, const int aRegistryReference,
        const std::optional<std::size_t> aInstructionBudget)
    : reference(std::move(aState), aRegistryReference)
    , m_InstructionBudget(aInstructionBudget)
    {}

    error_type function_reference::call(const value_list_type &aArguments, value_list_type &aResults)
        { return _call(aArguments, aResults, m_InstructionBudget); }

    error_type function_reference::call(const value_list_type &aArguments) {
        value_list_type discarded;

        return call(aArguments, discarded);
    }

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

    bool reference::operator==(const reference &aOther) const {
        if (m_pState != aOther.m_pState) return false;

        if (m_pReference == aOther.m_pReference) return true;

        auto *const L = m_pState.get();

        lua_rawgeti(L, LUA_REGISTRYINDEX, *m_pReference);
        lua_rawgeti(L, LUA_REGISTRYINDEX, *aOther.m_pReference);

        const bool same = lua_rawequal(L, -1, -2);

        lua_pop(L, 2);

        return same;
    }
}
