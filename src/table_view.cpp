// © Joseph Cameron - MIT License

#include <jfc/lua/internal.h>

#include <jfc/lua/exception.h>
#include <jfc/lua/table_view.h>

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace jfc::lua {
    namespace {
        //! room for a key and its value. Allocates, so an unbudgeted_scope must be open.
        void _make_room(lua_State *const L) {
            if (!backend::check_stack(L, 2)) throw exception("the lua stack cannot grow enough to read this table");
        }
    }

    table_view::table_view(lua_State *const L, const int aIndex, const bool aNested)
    : m_pState(L)
    , m_Index(aIndex)
    , m_Nested(aNested)
    {}

    table_view::table_view(table_view &&aOther) noexcept
    : m_pState(aOther.m_pState)
    , m_Index(aOther.m_Index)
    , m_Nested(std::exchange(aOther.m_Nested, false))
    {}

    table_view::~table_view() {
        if (m_Nested && lua_gettop(m_pState) == m_Index) lua_pop(m_pState, 1);
    }

    void table_view::_push_value(const key &aKey) const {
        const unbudgeted_scope unbudgeted(m_pState);

        _make_room(m_pState);

        detail::push_key(m_pState, aKey);

        lua_rawget(m_pState, m_Index);
    }

    std::optional<double> table_view::get_number(const key &aKey) const {
        _push_value(aKey);

        std::optional<double> out;

        if (lua_type(m_pState, -1) == LUA_TNUMBER) out = lua_tonumber(m_pState, -1);

        lua_pop(m_pState, 1);

        return out;
    }

    std::optional<bool> table_view::get_boolean(const key &aKey) const {
        _push_value(aKey);

        std::optional<bool> out;

        if (lua_isboolean(m_pState, -1)) out = lua_toboolean(m_pState, -1) != 0;

        lua_pop(m_pState, 1);

        return out;
    }

    std::optional<std::string_view> table_view::get_string(const key &aKey) const {
        _push_value(aKey);

        std::optional<std::string_view> out;

        if (lua_type(m_pState, -1) == LUA_TSTRING) {
            std::size_t length = 0;

            const char *const text = lua_tolstring(m_pState, -1, &length);

            out = std::string_view(text, length);
        }

        lua_pop(m_pState, 1);

        return out;
    }

    std::optional<table_view> table_view::get_table(const key &aKey) const {
        _push_value(aKey);

        if (lua_istable(m_pState, -1)) return table_view(m_pState, lua_gettop(m_pState), true);

        lua_pop(m_pState, 1);

        return {};
    }

    bool table_view::contains(const key &aKey) const {
        _push_value(aKey);

        const bool found = !lua_isnil(m_pState, -1);

        lua_pop(m_pState, 1);

        return found;
    }

    std::size_t table_view::size() const {
        {
            const unbudgeted_scope unbudgeted(m_pState);

            _make_room(m_pState);
        }

        std::size_t count = 0;

        lua_pushnil(m_pState);

        while (lua_next(m_pState, m_Index)) {
            ++count;

            lua_pop(m_pState, 1);
        }

        return count;
    }

    bool table_view::empty() const {
        {
            const unbudgeted_scope unbudgeted(m_pState);

            _make_room(m_pState);
        }

        lua_pushnil(m_pState);

        if (!lua_next(m_pState, m_Index)) return true;

        lua_pop(m_pState, 2);

        return false;
    }

    std::vector<key> table_view::keys() const {
        {
            const unbudgeted_scope unbudgeted(m_pState);

            _make_room(m_pState);
        }

        std::vector<key> out;

        lua_pushnil(m_pState);

        while (lua_next(m_pState, m_Index)) {
            switch (lua_type(m_pState, -2)) {
                case LUA_TNUMBER: out.emplace_back(lua_tonumber(m_pState, -2)); break;
                case LUA_TBOOLEAN: out.emplace_back(lua_toboolean(m_pState, -2) != 0); break;

                case LUA_TSTRING: {
                    std::size_t length = 0;

                    const char *const text = lua_tolstring(m_pState, -2, &length);

                    out.emplace_back(std::string(text, length));

                    break;
                }

                default: break;
            }

            lua_pop(m_pState, 1);
        }

        return out;
    }

    data_table table_view::to_data_table() const {
        const unbudgeted_scope unbudgeted(m_pState);

        return data_table(m_pState, m_Index);
    }
}
