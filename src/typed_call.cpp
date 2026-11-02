// © Joseph Cameron - MIT License

#include <jfc/lua/internal.h>

#include <jfc/lua/exception.h>
#include <jfc/lua/typed_call.h>

#include <cmath>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace jfc::lua::detail {
    namespace {
        [[nodiscard]] std::string _describe(lua_State *const L, const int aIndex) {
            const int type = lua_type(L, aIndex);

            if (type == LUA_TNIL || type == LUA_TNONE) return lua_typename(L, type);

            if (const auto *const pObject = to_userdata(L, aIndex))
                if (const auto name = type_name(L, pObject->type_key())) return "a " + *name;

            return std::string("a ") + lua_typename(L, type);
        }

        [[nodiscard]] const interpreter_limits *_limits(lua_State *const L) {
            void *ud = nullptr;

            lua_getallocf(L, &ud);

            return static_cast<const interpreter_limits *>(ud);
        }

        [[nodiscard]] int _anchor(lua_State *const L, const int aIndex) {
            const unbudgeted_scope unbudgeted(L);

            lua_pushvalue(L, aIndex);

            return luaL_ref(L, LUA_REGISTRYINDEX);
        }
    }

    call_frame::call_frame(lua_State *const L, const std::string_view aName, const int aFirstArgument,
        const char *const aNoun)
    : m_pState(L)
    , m_Name(aName)
    , m_FirstArgument(aFirstArgument)
    , m_Noun(aNoun)
    {}

    std::string call_frame::_where(const int aIndex) const {
        return (m_Name.empty() ? std::string() : std::string(m_Name) + ": ") + m_Noun + " " + std::to_string(aIndex);
    }

    void call_frame::_refuse(const int aIndex, const std::string &aWanted) const {
        throw exception(_where(aIndex) + " wants " + aWanted + ", got " + _describe(m_pState, aIndex + m_FirstArgument));
    }

    void call_frame::expect_at_most(const int aCount) const {
        const int given = lua_gettop(m_pState) - m_FirstArgument;

        if (given <= aCount) return;

        const std::string takes = aCount == 0 ? "no arguments"
            : "at most " + std::to_string(aCount) + (aCount == 1 ? " argument" : " arguments");

        throw exception(std::string(m_Name) + " takes " + takes + ", got " + std::to_string(given));
    }

    bool call_frame::absent(const int aIndex) const { return lua_isnoneornil(m_pState, aIndex + m_FirstArgument); }

    double call_frame::number(const int aIndex) const {
        const int index = aIndex + m_FirstArgument;

        if (lua_type(m_pState, index) != LUA_TNUMBER) _refuse(aIndex, "a number");

        return lua_tonumber(m_pState, index);
    }

    double call_frame::whole(const int aIndex, const int aDigits, const bool aSigned) const {
        const int index = aIndex + m_FirstArgument;

        if (lua_type(m_pState, index) != LUA_TNUMBER) _refuse(aIndex, "a whole number");

        const double value = lua_tonumber(m_pState, index);
        const double limit = std::ldexp(1.0, aDigits);

        if (value == std::floor(value) && value < limit && value >= (aSigned ? -limit : 0.0)) return value;

        const auto highest = ~0ull >> (64 - aDigits);

        std::ostringstream wanted;

        wanted << "a whole number from " << (aSigned ? "-" + std::to_string(highest + 1) : "0") << " to " << highest
            << ", got ";

        write_number(wanted, value);

        throw exception(_where(aIndex) + " wants " + wanted.str());
    }

    bool call_frame::boolean(const int aIndex) const {
        const int index = aIndex + m_FirstArgument;

        if (!lua_isboolean(m_pState, index)) _refuse(aIndex, "a boolean");

        return lua_toboolean(m_pState, index);
    }

    std::string_view call_frame::text(const int aIndex) const {
        const int index = aIndex + m_FirstArgument;

        if (lua_type(m_pState, index) != LUA_TSTRING) _refuse(aIndex, "a string");

        std::size_t length = 0;

        const char *const text = lua_tolstring(m_pState, index, &length);

        return {text, length};
    }

    data_table call_frame::table(const int aIndex) const {
        const int index = aIndex + m_FirstArgument;

        if (!lua_istable(m_pState, index)) _refuse(aIndex, "a table");

        return data_table(m_pState, index);
    }

    value_list_type call_frame::arguments(const int aIndex) const {
        const int first = aIndex + m_FirstArgument;
        const int last = lua_gettop(m_pState);

        value_list_type out;

        if (last >= first) out.reserve(static_cast<std::size_t>(last - first + 1));

        for (int i = first; i <= last; ++i) {
            if (to_param(m_pState, i, out)) continue;

            throw exception(_where(i - m_FirstArgument) + " is a " + lua_typename(m_pState, lua_type(m_pState, i))
                + ", which cannot cross into c++");
        }

        return out;
    }

    table_view call_frame::view(const int aIndex) const {
        const int index = aIndex + m_FirstArgument;

        if (!lua_istable(m_pState, index)) _refuse(aIndex, "a table");

        return table_view(m_pState, index, false);
    }

    reference call_frame::value(const int aIndex) const {
        const int index = aIndex + m_FirstArgument;

        if (lua_isnoneornil(m_pState, index)) _refuse(aIndex, "a value");

        return reference(_limits(m_pState)->state.lock(), _anchor(m_pState, index));
    }

    function_reference call_frame::function(const int aIndex) const {
        const int index = aIndex + m_FirstArgument;

        if (!lua_isfunction(m_pState, index)) _refuse(aIndex, "a function");

        const auto *const pLimits = _limits(m_pState);

        return function_reference(pLimits->state.lock(), _anchor(m_pState, index), pLimits->instructionBudget);
    }

    userdata call_frame::object(const int aIndex) const {
        const auto *const pObject = to_userdata(m_pState, aIndex + m_FirstArgument);

        if (!pObject || pObject->empty()) _refuse(aIndex, "an object");

        return *pObject;
    }

    userdata call_frame::object(const int aIndex, const void *const aTypeKey) const {
        const auto *const pObject = to_userdata(m_pState, aIndex + m_FirstArgument);

        if (!pObject || pObject->empty() || pObject->type_key() != aTypeKey) {
            const auto name = type_name(m_pState, aTypeKey);

            _refuse(aIndex, name ? "a " + *name : "a different object");
        }

        return *pObject;
    }

    void call_frame::reserve(const std::size_t aCount) {
        if (!lua_checkstack(m_pState, static_cast<int>(aCount)))
            throw exception("the lua stack cannot grow enough to return this many values");
    }

    void call_frame::push_nil() { lua_pushnil(m_pState); }

    void call_frame::push(const double aValue) { lua_pushnumber(m_pState, aValue); }

    void call_frame::push(const bool aValue) { lua_pushboolean(m_pState, aValue); }

    void call_frame::push(const std::string_view aValue) { lua_pushlstring(m_pState, aValue.data(), aValue.size()); }

    void call_frame::push(const data_table &aValue) { aValue.push_to_lua_state(m_pState); }

    void call_frame::push(const userdata &aValue) { push_userdata(m_pState, aValue); }

    void call_frame::push(const reference &aValue) {
        if (aValue.m_pState != _limits(m_pState)->state.lock())
            throw exception(std::string(m_Name) + " returned a value that belongs to another interpreter");

        lua_rawgeti(m_pState, LUA_REGISTRYINDEX, aValue.registry_reference());
    }

    int call_frame::push(const value_list_type &aValues) {
        reserve(aValues.size());

        for (const auto &value : aValues) push_param(m_pState, value);

        return static_cast<int>(aValues.size());
    }

    void call_frame::throw_unknown() const {
        throw exception(std::string(m_Name) + " threw something that is not a std::exception");
    }
}
