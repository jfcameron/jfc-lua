// © Joseph Cameron - MIT License

#ifndef JFC_LUA_TABLE_VIEW_H
#define JFC_LUA_TABLE_VIEW_H

#include <jfc/lua/data_table.h>
#include <jfc/lua/key.h>
#include <jfc/lua/types.h>

#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

namespace jfc::lua
{
    /// \brief a script's table as a typed function's argument, read where it is instead of copied
    ///
    /// Reads what the table holds, running no metamethods, as a data_table does. The table may hold anything: only what
    /// is asked for is looked at. A read cannot raise a lua error, so it is safe in any c++.
    ///
    /// \warning valid for the call it was given to, like a std::string_view. Use to_data_table to keep the contents.
    class table_view final
    {
    public:
        /// \brief the number at aKey, or nothing if there is no such key or it holds another type
        [[nodiscard]] std::optional<double> get_number(const key &aKey) const;
        /// \brief the boolean at aKey, or nothing if there is no such key or it holds another type
        [[nodiscard]] std::optional<bool> get_boolean(const key &aKey) const;
        /// \brief the string at aKey, or nothing if there is no such key or it holds another type
        ///
        /// \warning valid for the call, unless a script changes the table in the meantime
        [[nodiscard]] std::optional<std::string_view> get_string(const key &aKey) const;

        /// \brief the subtable at aKey, or nothing if there is no such key or it holds another type
        ///
        /// It takes a slot on lua's stack, given back when it is destroyed if nothing newer is above it, or else when
        /// the call ends.
        [[nodiscard]] std::optional<table_view> get_table(const key &aKey) const;

        [[nodiscard]] bool contains(const key &aKey) const;

        /// \brief how many keys this table holds, counting them
        [[nodiscard]] std::size_t size() const;

        [[nodiscard]] bool empty() const;

        /// \brief every key that a key can name, in no particular order
        [[nodiscard]] std::vector<key> keys() const;

        /// \brief a copy to keep
        ///
        /// \warning throws exception for what a data_table cannot hold
        [[nodiscard]] data_table to_data_table() const;

        table_view(table_view &&aOther) noexcept;
        ~table_view();

        table_view(const table_view &) = delete;
        table_view &operator=(const table_view &) = delete;
        table_view &operator=(table_view &&) = delete;

    private:
        friend class detail::call_frame;

        table_view(lua_State *L, int aIndex, bool aNested);

        //! pushes what is at aKey, which must be popped
        void _push_value(const key &aKey) const;

        lua_State *m_pState;
        int m_Index;
        bool m_Nested;
    };
}

#endif
