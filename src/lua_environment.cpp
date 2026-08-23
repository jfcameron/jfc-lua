// © Joseph Cameron - All Rights Reserved

#include <jfc/lua_internal.h>

#include <jfc/lua_exception.h>
#include <jfc/lua_environment.h>
#include <jfc/lua_key.h>
#include <jfc/lua_path.h>
#include <jfc/lua_reference.h>
#include <jfc/lua_userdata.h>
#include <jfc/lua_data_table.h>

#include <cstdio>
#include <functional>
#include <new>
#include <optional>
#include <sstream>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

using jfc::lua::interpreter_limits;

namespace {
    void _push_userdata(lua_State *const L, const jfc::lua::userdata &aValue);

    [[nodiscard]] const jfc::lua::userdata *_to_userdata(lua_State *const L, const int aIndex);
}

static bool _to_param(lua_State *const L, const int aIndex, jfc::lua::value_list_type &aOut) {
    switch (lua_type(L, aIndex)) {
        case LUA_TNUMBER:  aOut.push_back(lua_tonumber(L, aIndex)); return true;
        case LUA_TBOOLEAN: aOut.push_back(static_cast<bool>(lua_toboolean(L, aIndex))); return true;
        case LUA_TNIL:     aOut.push_back(nullptr); return true;

        case LUA_TSTRING: {
            size_t length;
            const char *const text = lua_tolstring(L, aIndex, &length);

            aOut.push_back(std::string(text, length));

            return true;
        }

        case LUA_TTABLE: aOut.push_back(jfc::lua::data_table(L, aIndex)); return true;

        case LUA_TUSERDATA: {
            const auto *const bound = _to_userdata(L, aIndex);

            if (!bound) return false;

            aOut.push_back(*bound);

            return true;
        }

        default: return false;
    }
}

static void _push_param(lua_State *const L, const jfc::lua::value_list_type::value_type &aValue) {
    std::visit([L](auto &&value) {
        using value_type = std::decay_t<decltype(value)>;

        if constexpr (std::is_same_v<value_type, double>) lua_pushnumber(L, value);
        else if constexpr (std::is_same_v<value_type, bool>) lua_pushboolean(L, value);
        else if constexpr (std::is_same_v<value_type, std::string>) lua_pushstring(L, value.c_str());
        else if constexpr (std::is_same_v<value_type, jfc::lua::data_table>) value.push_to_lua_state(L);
        else if constexpr (std::is_same_v<value_type, jfc::lua::userdata>) _push_userdata(L, value);
        else lua_pushnil(L);
    }, aValue);
}

static void _push_key(lua_State *const L, const jfc::lua::key &aKey) {
    std::visit([L](auto &&value) {
        using value_type = std::decay_t<decltype(value)>;

        if constexpr (std::is_same_v<value_type, double>) lua_pushnumber(L, value);
        else if constexpr (std::is_same_v<value_type, bool>) lua_pushboolean(L, value);
        else lua_pushstring(L, value.c_str());
    }, aKey.value());
}

static bool _read_value(lua_State *L, const int aRoot, const jfc::lua::path &aPath) {
    lua_rawgeti(L, LUA_REGISTRYINDEX, aRoot);

    for (std::size_t i = 0; i < aPath.size(); ++i) {
        if (!lua_istable(L, -1)) {
            lua_pop(L, 1);

            return false;
        }

        _push_key(L, aPath[i]);

        lua_gettable(L, -2);

        lua_remove(L, -2);
    }

    return true;
}

static void _write_value(lua_State *L, const int aRoot, const jfc::lua::path &aPath,
    std::function<void()> &&aPushValueFunctor) {
    lua_rawgeti(L, LUA_REGISTRYINDEX, aRoot);

    const int container = lua_gettop(L);

    for (std::size_t i = 0; i + 1 < aPath.size(); ++i) {
        _push_key(L, aPath[i]);
        lua_rawget(L, container);

        if (!lua_istable(L, -1)) {
            lua_pop(L, 1);

            _push_key(L, aPath[i]);
            lua_gettable(L, container);

            const bool inheritedATable = lua_istable(L, -1);

            lua_newtable(L);

            if (inheritedATable) {
                lua_newtable(L);

                lua_pushvalue(L, -3);
                lua_setfield(L, -2, "__index");

                lua_setmetatable(L, -2);
            }

            _push_key(L, aPath[i]);
            lua_pushvalue(L, -2);
            lua_settable(L, container);

            lua_remove(L, -2);
        }

        lua_replace(L, container);
    }

    aPushValueFunctor();

    _push_key(L, aPath[aPath.size() - 1]);
    lua_pushvalue(L, -2);

    lua_settable(L, container);
}

namespace {
    class root_scope final {
        lua_State *m_pState;
        int m_SavedIndex;

    public:
        root_scope(const root_scope &) = delete;
        root_scope &operator=(const root_scope &) = delete;

        root_scope(lua_State *const aState, const int aRootReference)
        : m_pState(aState) {
            lua_pushvalue(aState, LUA_GLOBALSINDEX);

            m_SavedIndex = lua_gettop(aState);

            lua_rawgeti(aState, LUA_REGISTRYINDEX, aRootReference);
            lua_replace(aState, LUA_GLOBALSINDEX);
        }

        ~root_scope() {
            lua_pushvalue(m_pState, m_SavedIndex);
            lua_replace(m_pState, LUA_GLOBALSINDEX);
        }
    };

    void _disable_jit_for_chunk(lua_State *const L, interpreter_limits *const aLimits) {
        if (!aLimits || aLimits->jitOffReference == -2 /* LUA_NOREF */) return;

        lua_rawgeti(L, LUA_REGISTRYINDEX, aLimits->jitOffReference);

        lua_pushvalue(L, -2);
        lua_pushboolean(L, 1);

        if (lua_pcall(L, 2, 0, 0) != LUA_OK) lua_pop(L, 1);
    }

    constexpr const char *USERDATA_METATABLE = "jfc.lua.userdata";

    int _destroy_userdata(lua_State *L) {
        static_cast<jfc::lua::userdata *>(lua_touserdata(L, 1))->~userdata();
        return 0;
    }

    void _push_userdata(lua_State *const L, const jfc::lua::userdata &aValue) {
        new (lua_newuserdata(L, sizeof(jfc::lua::userdata))) jfc::lua::userdata(aValue);

        if (luaL_newmetatable(L, USERDATA_METATABLE)) {
            lua_pushcfunction(L, _destroy_userdata);
            lua_setfield(L, -2, "__gc");

            lua_pushboolean(L, 0);
            lua_setfield(L, -2, "__metatable");
        }

        lua_setmetatable(L, -2);
    }

    [[nodiscard]] const jfc::lua::userdata *_to_userdata(lua_State *const L, const int aIndex) {
        if (lua_type(L, aIndex) != LUA_TUSERDATA) return nullptr;

        if (!lua_getmetatable(L, aIndex)) return nullptr;

        luaL_getmetatable(L, USERDATA_METATABLE);

        const bool ours = lua_rawequal(L, -1, -2);

        lua_pop(L, 2);

        if (!ours) return nullptr;

        return static_cast<const jfc::lua::userdata *>(lua_touserdata(L, aIndex));
    }

    class budget_scope final {
        interpreter_limits *m_pLimits;
        std::size_t m_OutermostBudget;
        bool m_Outermost;

    public:
        budget_scope(const budget_scope &) = delete;
        budget_scope &operator=(const budget_scope &) = delete;

        budget_scope(lua_State *const aState, const std::optional<std::size_t> aBudgetOverride)
        : m_pLimits(nullptr)
        , m_OutermostBudget(0)
        , m_Outermost(false) {
            void *ud = nullptr;

            lua_getallocf(aState, &ud);

            m_pLimits = static_cast<interpreter_limits *>(ud);

            if (!m_pLimits) return;

            m_Outermost = m_pLimits->depth++ == 0;

            if (!m_Outermost) return;

            m_pLimits->instructionsSeen = 0;
            m_pLimits->aborted = false;
            m_pLimits->usedAtRunStart = m_pLimits->used;

            m_OutermostBudget = m_pLimits->instructionBudget;

            if (aBudgetOverride) m_pLimits->instructionBudget = *aBudgetOverride;
        }

        ~budget_scope() {
            if (!m_pLimits) return;

            --m_pLimits->depth;

            if (m_Outermost) m_pLimits->instructionBudget = m_OutermostBudget;
        }

        [[nodiscard]] bool aborted() const { return m_pLimits && m_pLimits->aborted; }

        [[nodiscard]] std::size_t instruction_budget() const
            { return m_pLimits ? m_pLimits->instructionBudget : 0; }

        [[nodiscard]] interpreter_limits *limits() const { return m_pLimits; }
    };


    /// \brief the string at aIndex, or nothing if lua has none to give
    [[nodiscard]] std::optional<std::string> _to_string(lua_State *const L, const int aIndex) {
        const auto *const text = lua_tostring(L, aIndex);

        if (!text) return {};

        return std::string(text);
    }
}

namespace jfc::lua {
    environment::environment(std::shared_ptr<lua_State> aState, const int aRootReference,
        const std::optional<std::size_t> aInstructionBudget)
    : reference(std::move(aState), aRootReference)
    , m_InstructionBudget(aInstructionBudget)
    {}

    std::optional<double> reference::read_number(const path &aPath) const {
        auto *L = state();

        const stack_guard guard(L);

        std::optional<double> val;

        if (_read_value(L, registry_reference(), aPath) && lua_type(L, -1) == LUA_TNUMBER) val = lua_tonumber(L, -1);

        return val;
    }

    std::optional<bool> reference::read_boolean(const path &aPath) const {
        auto *L = state();

        const stack_guard guard(L);

        std::optional<bool> val;

        if (_read_value(L, registry_reference(), aPath) && lua_isboolean(L, -1)) val = lua_toboolean(L, -1);

        return val;
    }

    std::optional<std::string> reference::read_string(const path &aPath) const {
        auto *L = state();

        const stack_guard guard(L);

        std::optional<std::string> val;

        if (_read_value(L, registry_reference(), aPath) && lua_type(L, -1) == LUA_TSTRING) {
            size_t len;
            const char *str = lua_tolstring(L, -1, &len);

            val = std::string(str, len);
        }

        return val;
    }

    std::optional<data_table> reference::read_data_table(const path &aPath,
        const unsupported aPolicy) const {
        auto *L = state();

        const stack_guard guard(L);

        if (_read_value(L, registry_reference(), aPath) && lua_istable(L, -1)) return data_table(L, -1, aPolicy);

        return {};
    }

    void reference::write(const path &aPath, const bool aValue) {
        auto *L = state();

        const stack_guard guard(L);

        _write_value(L, registry_reference(), aPath, [L, aValue]() { lua_pushboolean(L, aValue); });
    }

    void reference::write(const path &aPath, const double aValue) {
        auto *L = state();

        const stack_guard guard(L);

        _write_value(L, registry_reference(), aPath, [L, aValue]() { lua_pushnumber(L, aValue); });
    }

    void reference::write(const path &aPath, const std::string &aValue) {
        auto *L = state();

        const stack_guard guard(L);

        _write_value(L, registry_reference(), aPath, [L, &aValue]() { lua_pushstring(L, aValue.c_str()); });
    }

    void reference::write(const path &aPath, const std::string::value_type *aValue) {
        auto *L = state();

        const stack_guard guard(L);

        _write_value(L, registry_reference(), aPath, [L, &aValue]() { lua_pushstring(L, aValue); });
    }

    void reference::write(const path &aPath, const data_table &aValue) {
        auto *L = state();

        const stack_guard guard(L);

        _write_value(L, registry_reference(), aPath, [L, &aValue]() { aValue.push_to_lua_state(L); });
    }

    template <typename value_type>
    [[nodiscard]] std::optional<std::vector<value_type>> _to_vector(const data_table &aTable) {
        std::vector<value_type> out;

        out.reserve(aTable.size());

        for (std::size_t i = 1; i <= aTable.size(); ++i) {
            std::optional<value_type> element;

            if constexpr (std::is_same_v<value_type, double>) element = aTable.get_number(i);
            else if constexpr (std::is_same_v<value_type, bool>) element = aTable.get_boolean(i);
            else element = aTable.get_string(i);

            if (!element) return {};

            out.push_back(*element);
        }

        return out;
    }

    template <typename container_type>
    void _write_as_array(reference &aReference, const path &aPath, const container_type &aValues) {
        data_table out;

        std::size_t index = 1;

        for (const auto &value : aValues) out.set(index++, value);

        aReference.write(aPath, out);
    }

    template <typename value_type>
    std::optional<std::vector<value_type>> reference::read_vector(const path &aPath) const {
        const auto found = read_data_table(aPath);

        if (!found) return {};

        return _to_vector<value_type>(*found);
    }

    template std::optional<std::vector<double>>
        reference::read_vector<double>(const path &) const;
    template std::optional<std::vector<bool>>
        reference::read_vector<bool>(const path &) const;
    template std::optional<std::vector<std::string>>
        reference::read_vector<std::string>(const path &) const;

    void reference::write(const path &aPath, const std::vector<double> &aValue)
        { _write_as_array(*this, aPath, aValue); }

    void reference::write(const path &aPath, const std::vector<bool> &aValue) {
        data_table out;

        std::size_t index = 1;

        for (const bool value : aValue) out.set(index++, value);

        write(aPath, out);
    }

    void reference::write(const path &aPath, const std::vector<std::string> &aValue) { _write_as_array(*this, aPath, aValue); }

    namespace {
        [[nodiscard]] error_type _invoke(lua_State *const L, const std::string &aWhat,
            const value_list_type &aArguments, value_list_type &aResults)
        {
            if (lua_type(L, -1) != LUA_TFUNCTION)
                return aWhat + " holds a " + lua_typename(L, lua_type(L, -1)) + ", not a function";

            if (!lua_checkstack(L, static_cast<int>(aArguments.size()) + 2))
                return "the lua stack cannot grow enough to make this call";

            // Where the results will begin once lua_pcall has replaced the function and its arguments.
            const int base = lua_gettop(L);

            for (const auto &argument : aArguments) _push_param(L, argument);

            if (lua_pcall(L, static_cast<int>(aArguments.size()), LUA_MULTRET, 0) != LUA_OK)
                return _to_string(L, -1).value_or("lua reported an error it could not describe");

            for (int i = base; i <= lua_gettop(L); ++i)
                if (!_to_param(L, i, aResults))
                {
                    aResults.clear();

                    return aWhat + " returned a " + lua_typename(L, lua_type(L, i))
                        + " in position " + std::to_string(i - base + 1)
                        + ", which cannot cross into c++";
                }

            return {};
        }
    }

    environment::error_type environment::call(const path &aPath, const value_list_type &aArguments,
        value_list_type &aResults) {
        auto *L = state();

        const stack_guard guard(L);
        const root_scope root(L, registry_reference());
        const budget_scope budget(L, m_InstructionBudget);

        if (budget.aborted()) return "script exceeded its instruction budget";

        aResults.clear();

        std::stringstream where;

        where << aPath;

        if (!lua_checkstack(L, 2)) return "the lua stack cannot grow enough to make this call";

        if (!_read_value(L, registry_reference(), aPath)) return where.str() + " does not resolve";

        return _invoke(L, where.str(), aArguments, aResults);
    }

    error_type reference::call(const value_list_type &aArguments,
        value_list_type &aResults) {
        auto *L = state();

        const stack_guard guard(L);

        const budget_scope budget(L, {});

        if (budget.aborted()) return "script exceeded its instruction budget";

        aResults.clear();

        if (!lua_checkstack(L, 2)) return "the lua stack cannot grow enough to make this call";

        lua_rawgeti(L, LUA_REGISTRYINDEX, registry_reference());

        return _invoke(L, "the reference", aArguments, aResults);
    }

    error_type reference::call(const value_list_type &aArguments) {
        value_list_type discarded;

        return call(aArguments, discarded);
    }

    environment::error_type environment::call(const path &aPath, const value_list_type &aArguments) {
        value_list_type discarded;

        return call(aPath, aArguments, discarded);
    }

    std::optional<reference> reference::read_reference(const path &aPath) const {
        auto *L = state();

        const stack_guard guard(L);

        if (!_read_value(L, registry_reference(), aPath) || lua_isnil(L, -1)) return {};

        lua_pushvalue(L, -1);

        return reference(m_pState, luaL_ref(L, LUA_REGISTRYINDEX));
    }

    void reference::write(const path &aPath, const userdata &aValue) {
        auto *L = state();

        const stack_guard guard(L);

        _write_value(L, registry_reference(), aPath, [L, &aValue]() { _push_userdata(L, aValue); });
    }

    std::optional<userdata> reference::_read_userdata(const path &aPath) const {
        auto *L = state();

        const stack_guard guard(L);

        if (!_read_value(L, registry_reference(), aPath)) return {};

        if (const auto *const held = _to_userdata(L, -1)) return *held;

        return {};
    }

    std::optional<userdata> reference::read_userdata(const path &aPath) const
        { return _read_userdata(aPath); }

    void environment::register_function(const path &aName, closure_type aClosure) {
        auto *L = state();

        const stack_guard guard(L);
        const root_scope root(L, registry_reference());

        auto wrapper = [](lua_State *p) -> int {
            char failure[256] = {};

            bool failed = false;

            int results = 0;

            try {
                auto *pImpl = static_cast<closure_type *>(lua_touserdata(p, lua_upvalueindex(1)));

                value_list_type args;

                for (int i(1); i <= lua_gettop(p); ++i) {
                    if (_to_param(p, i, args)) continue;

                    throw lua_exception("argument " + std::to_string(i) + " is a "
                        + lua_typename(p, lua_type(p, i)) + ", which cannot cross into c++");
                }

                auto return_values = (*pImpl)(std::move(args));

                for (const auto &val : return_values) _push_param(p, val);

                results = static_cast<int>(return_values.size());
            }
            catch (const std::exception &e) {
                std::snprintf(failure, sizeof failure, "%s", e.what());

                failed = true;
            }
            catch (...)
            {
                std::snprintf(failure, sizeof failure, "%s",
                    "a registered function threw something that is not a std::exception");

                failed = true;
            }

            if (failed) return luaL_error(p, "%s", failure);

            return results;
        };

        _write_value(L, registry_reference(), aName, [L, &aClosure, wrapper]() {
            auto *const pStorage = static_cast<closure_type *>(
                lua_newuserdata(L, sizeof(closure_type)));

            new (pStorage) closure_type(aClosure);

            if (luaL_newmetatable(L, "jfc.lua.closure")) {
                lua_pushcfunction(L, [](lua_State *p) {
                    static_cast<closure_type *>(lua_touserdata(p, 1))->~closure_type();
                    return 0;
                });

                lua_setfield(L, -2, "__gc");
            }

            lua_setmetatable(L, -2);

            lua_pushcclosure(L, wrapper, 1);
        });
    }

    environment::error_type environment::run(const std::string &aLuaScript) {
        auto *L = state();

        const stack_guard guard(L);
        const root_scope root(L, registry_reference());
        const budget_scope budget(L, m_InstructionBudget);

        if (budget.aborted()) return "script exceeded its instruction budget";

        if (luaL_loadstring(L, aLuaScript.c_str()) != LUA_OK)
            return _to_string(L, -1).value_or("lua reported an error it could not describe");

        if (budget.instruction_budget()) _disable_jit_for_chunk(L, budget.limits());

        if (lua_pcall(L, 0, 0, 0) != LUA_OK)
            return _to_string(L, -1).value_or("lua reported an error it could not describe");

        return {};
    }

    environment::error_type environment::validate_syntax(const std::string &aLuaScript) const {
        auto *L = state();

        const stack_guard guard(L);
        const root_scope root(L, registry_reference());

        if (luaL_loadstring(L, aLuaScript.c_str()) == LUA_OK) return {};

        return _to_string(L, -1).value_or("lua reported an error it could not describe");
    }
}
