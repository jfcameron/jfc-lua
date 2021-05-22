// © Joseph Cameron - All Rights Reserved

#ifndef JFC_LUA_H
#define JFC_LUA_H

#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>

struct lua_State;

namespace jfc::lua
{
    /// \brief datamodel type for table.
    ///
    /// can be used to: send messages between interpeters, serialize state to disk,
    /// serialize data for messaging over a network
    ///
    /// \note Key and Value types are limited to: [string, bool, number]
    class table final
    {
    public:
        /// \brief serialize to a string containing a lua table definition
        friend std::ostream &operator<<(std::ostream &stream, const table &a);

        /// \brief construct a table from an existing table within a lua state
        table(lua_State *L, int aIndex);

        /// \brief construct a table with no content
        table() = default;

        /// \brief writes a value to this table (or to a subtable)
        //void write_value(, value)???
       
        /// \brief writes the table to a lua state
        void push_to_lua_state(lua_State *L) const;

    private:
        using value_type = std::variant<double, bool, std::string, std::shared_ptr<table>>;

        /// \brief ordered map of number key fields
        std::map<double, value_type> m_NumberFields;

        /// \brief unordered map of string key fields
        std::unordered_map<std::string, value_type> m_StringFields;
        
        /// \brief unordered map of bool key fields
        std::unordered_map<bool, value_type> m_BooleanFields;
    };

    /// \brief parameter list for functions that commuicate across c++/lua barrier
    using params_type = std::vector<std::variant<double, bool, std::string, decltype(nullptr), table>>;

    /// \brief a lua interpreter
    class interpreter final
    {
    public:
        /// \brief methods that can fail return this
        using error_type = std::optional<std::string>;

        /// \brief c++'s implementation of the closure is a lambda with a non-empty capture list
        using closure_type = std::function<params_type(params_type)>;

        /// \brief writes a [string, boolean] to the lua context
        void write_value(const std::string &aPath, const bool aValue);
        /// \brief writes a [string, number] to the lua context
        void write_value(const std::string &aPath, const double aValue);
        /// \brief writes a [string, string] to the lua context
        void write_value(const std::string &aPath, const std::string &aValue);
        /// \brief writes a [string, string] to the lua context
        void write_value(const std::string &aPath, const std::string::value_type *aValue);
        /// \brief writes a [string, table] to the lua context
        void write_value(const std::string &aPath, const table &);
        
        /*
        /// \brief writes a [boolean, boolean] to the lua context
        void write_value(const std::string &aPath, const bool aKey, const bool aValue);
        /// \brief writes a [boolean, number] to the lua context
        void write_value(const std::string &aPath, const bool aKey, const double aValue);
        /// \brief writes a [boolean, string] to the lua context
        void write_value(const std::string &aPath, const bool aKey, const std::string &aValue);
        /// \brief writes a [boolean, string] to the lua context
        void write_value(const std::string &aPath, const bool aKey, const std::string::value_type *aValue);
        /// \brief writes a [boolean, table] to the lua context
        void write_value(const std::string &aPath, const bool aKey, const table &);
        */
        
        /*
        /// \brief writes a [number, boolean] to the lua context
        void write_value(const double aPath, std::optional<double> aKey, const bool aValue);
        /// \brief writes a [number, number] to the lua context
        void write_value(const double aPath, std::optional<double> aKey, const double aValue);
        /// \brief writes a [number, string] to the lua context
        void write_value(const double aPath, std::optional<double> aKey, const std::string &aValue);
        /// \brief writes a [number, string] to the lua context
        void write_value(const double aPath, std::optional<double> aKey, const std::string::value_type *aValue);
        /// \brief writes a [number, table] to the lua context
        void write_value(const double aPath, const std::optional<double> aKey, const table &);
        */
        
        /*
        /// \brief deletes the value of a string property from the lua context 
        void delete_value(const std::string &aPath);
        /// \brief deletes the value of a boolean property from the lua context 
        void delete_value(const std::string &aPath, const bool aKey);
        /// \brief deletes the value of a number property from the lua context 
        void delete_value(const double aPath, const double aKey);
        */

        /// \brief reads a boolean from the lua context
        [[nodiscard]] std::optional<bool> read_boolean(const std::string &aPath) const;
        /// \brief reads a number from the lua context
        [[nodiscard]] std::optional<double> read_number(const std::string &aPath) const;
        /// \brief reads a string from the lua context
        [[nodiscard]] std::optional<std::string> read_string(const std::string &aPath) const;
        /// \brief reads a table from the lua context
        [[nodiscard]] std::optional<table> read_table(const std::string &aPath) const;
        /// \brief reads a value of unknown type
        //[[nodiscard]] std::optional<std::variant<bool, double, std::string, table> read_any(const std::string &aPath) const;

        //TODO: calls a lua function if it exists: use for eg callbacks
        //[[nodiscard]] std::optional<return_list> call_function(const std::string &aPath, param_list_type params);

        /// \brief registers a closure (c++ lambda with captured data)
        void register_function(const std::string &aName, closure_type a);

        /// \brief registers a instanced closure
        ///void register_function(type, type *instance, name, a);

        /// \brief point all instanced closures of some type to a new instance of that type
        ///void update_instanced_closures(type, type *instance);

        /// \brief checks for basic synatx errors. 
        ///
        /// Not required to be called before run_script, but useful if
        /// the user is trying to catch lua issues early
        [[nodiscard]] error_type validate_syntax(const std::string &aLuaScript) const;
        
        /// \brief run a script, returns an error if something went wrong
        [[nodiscard]] error_type run(const std::string &aLuaScript) const;
        
        /// \brief construct an interpreter
        interpreter();

    private:
        /// \brief state of the internal lua interpreter
        std::unique_ptr<lua_State, std::function<void(lua_State *)>> m_pState;

        /// \brief closures that have been registered to this interpreter
        std::unordered_map<std::string, closure_type> m_RegisteredClosures;
    };
}

#endif

