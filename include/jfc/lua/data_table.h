// © Joseph Cameron - All Rights Reserved

#ifndef JFC_LUA_DATA_TABLE_H
#define JFC_LUA_DATA_TABLE_H

#include <jfc/lua/key.h>
#include <jfc/lua/types.h>

#include <array>
#include <cstddef>
#include <iosfwd>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace jfc::lua
{
    /// \brief what to do about a lua value this cannot store
    enum class unsupported {
        reject, ///< a value that cannot be stored is an error
        skip    ///< keep what can be stored and step over the rest
    };

    /// \brief a lua table, restricted to what can be stored
    ///
    /// data tables are a restricted category of table, meant to be used for serialization. 
    /// it therefore it does not support functions, coroutines or userdata, as those types 
    /// cannot be meaningfully serialized.
    class data_table final
    {
    public:
        /// \brief serialize to a string containing a lua table definition
        friend std::ostream &operator<<(std::ostream &stream, const data_table &a);

        /// \brief construct a table from an existing table within a lua state
        ///
        /// \warning throws exception when the lua table holds something that cannot be stored and
        /// aPolicy is reject, when a key is of an unstorable kind, when the table reaches itself, or
        /// when it nests deeper than MAXIMUM_DEPTH.
        data_table(lua_State *L, int aIndex, const unsupported aPolicy = unsupported::reject);

        /// \brief how deep a nesting this will read
        ///
        /// a recursive table read limit needs to exist to prevent
        /// excessively nested table structures causing a stack overflow
        /// when the program tries to serialize it
        static constexpr std::size_t MAXIMUM_DEPTH = 128;

        /// \brief construct a table with no content
        data_table() = default;

        /// \brief writes the table to a lua state
        void push_to_lua_state(lua_State *L) const;

        /// \brief the table as a lua table literal
        [[nodiscard]] std::string to_string() const;

        /// \brief read back what to_string produced, **without running it as lua**
        ///
        /// \warning throws exception if it does not parse
        [[nodiscard]] static data_table from_string(const std::string &aText);

        /// \brief the number at aKey, or nothing if there is no such key or it holds another type
        [[nodiscard]] std::optional<double> get_number(const key &aKey) const;
        /// \brief the boolean at aKey, or nothing if there is no such key or it holds another type
        [[nodiscard]] std::optional<bool> get_boolean(const key &aKey) const;
        /// \brief the string at aKey, or nothing if there is no such key or it holds another type
        [[nodiscard]] std::optional<std::string> get_string(const key &aKey) const;

        /// \brief the subtable at aKey, or null if there is no such key or it holds another type
        [[nodiscard]] std::shared_ptr<const data_table> get_data_table(const key &aKey) const;

        void set(const key &aKey, const double aValue);
        void set(const key &aKey, const bool aValue);
        void set(const key &aKey, std::string aValue);
        void set(const key &aKey, const char *const aValue);
        void set(const key &aKey, const data_table &aValue);

        /// \brief removes aKey, if it is there
        void erase(const key &aKey);

        [[nodiscard]] bool contains(const key &aKey) const;

        /// \brief how many keys this table holds
        [[nodiscard]] std::size_t size() const;

        [[nodiscard]] bool empty() const;

        /// \brief every key this table holds
        [[nodiscard]] std::vector<key> keys() const;

    private:
        using value_type = std::variant<double, bool, std::string, std::shared_ptr<data_table>>;

        /// \brief the value at aKey, whichever container holds that kind of key, or null
        [[nodiscard]] const value_type *_find(const key &aKey) const;

        /// \brief put aValue at aKey, in whichever container holds that kind of key
        void _assign(const key &aKey, value_type aValue);

        /// \brief where a read has got to: the policy, the chain for messages, and the tables above it
        struct read_context;

        /// \brief one level of the chain an error message is built from
        struct read_frame;

        /// \brief read the lua table at aIndex into this one
        void _read(lua_State *L, const int aIndex, read_context &aContext);

        [[nodiscard]] static std::optional<key> _to_key(lua_State *L, const int aIndex,
            const read_context &aContext);

        [[nodiscard]] static std::optional<value_type> _to_value(lua_State *L, const int aIndex,
            read_context &aContext);

        /// \brief throw unless the policy says to step over it
        static void _reject(const read_context &aContext, const std::string &aWhy);

        /// \brief the dense run of number keys, 1..n, which is what a lua list literal produces
        std::vector<value_type> m_Array;

        /// \brief every other number key: sparse, negative, zero, or not a whole number
        std::map<double, value_type> m_NumberFields;

        /// \brief the slot aKey names within a run of aSize, if it names one
        [[nodiscard]] static std::optional<std::size_t> _array_slot(const double aKey,
            const std::size_t aSize);

        /// \brief ordered map of string key fields
        std::map<std::string, value_type> m_StringFields;
        
        /// \brief the at most two boolean keyed fields, indexed by the key itself
        std::array<std::optional<value_type>, 2> m_BooleanFields;
    };
}

#endif
