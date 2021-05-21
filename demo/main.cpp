// © Joseph Cameron - All Rights Reserved

#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <tuple>
#include <unordered_map>
#include <variant>

#include <lua.hpp>

static std::tuple<std::vector<std::string>, std::string> _parse_pathstring(const std::string &aPathString)
{
    static const std::string delimiter(".");

    std::vector<std::string> path;

    size_t last(0), next; 

    while ((next = aPathString.find(delimiter, last)) != std::string::npos) 
    {   
        path.push_back(aPathString.substr(last, next - last));

        last = next + 1; 
    }

    const std::string variableName = aPathString.substr(last);

    return { path, variableName };
}

static void _write_value(lua_State *L, const std::string &aPathString, std::function<void()> &&aPushValueFunctor)
{
    auto path_data = _parse_pathstring(aPathString);

    std::vector<std::string> &path(std::get<0>(path_data));
    const std::string &variableName(std::get<1>(path_data));
   
    if (!path.empty())
    {
        lua_getglobal(L, path[0].c_str());
        if (!lua_istable(L, -1))
        {
            lua_pop(L, 1);

            lua_newtable(L);
            lua_setglobal(L, path[0].c_str());
            lua_getglobal(L, path[0].c_str());
        }

        for (size_t i(1); i < path.size(); ++i)
        {
            bool nestedTableExists(false);

            lua_pushnil(L);
            while (lua_next(L, -2))
            {
                lua_pushvalue(L, -2);

                if (path[i] == lua_tostring(L, -1) && lua_istable(L, -2))
                {
                    nestedTableExists = true;

                    lua_pushvalue(L, -2);
                    lua_pop(L, 2);

                    break;
                }

                lua_pop(L, 2);
            }
            
            if (!nestedTableExists)
            {
                lua_newtable(L);
                lua_setfield(L, -2, path[i].c_str());
                lua_getfield(L, -1, path[i].c_str());
            }
        }
    }
    
    aPushValueFunctor();

    if (path.empty()) lua_setglobal(L, variableName.c_str());
    else lua_setfield(L, -2, variableName.c_str());
}

static bool _read_value(lua_State *L, const std::string &aPath)
{
    auto path_data = _parse_pathstring(aPath);

    std::vector<std::string> &path(std::get<0>(path_data));
    const std::string &variableName(std::get<1>(path_data));

    if (!path.empty())
    {
        lua_getglobal(L, path[0].c_str());
        if (!lua_istable(L, -1))
        {
            lua_pop(L, 1);

            return false;
        }

        for (size_t i(1); i < path.size(); ++i)
        {
            bool nestedTableExists(false);

            lua_pushnil(L);
            while (lua_next(L, -2))
            {
                lua_pushvalue(L, -2);

                if (path[i] == lua_tostring(L, -1) && lua_istable(L, -2))
                {
                    nestedTableExists = true;

                    lua_pushvalue(L, -2);
                    lua_pop(L, 2);

                    break;
                }

                lua_pop(L, 2);
            }
            
            if (!nestedTableExists) return false;
        }
    
        lua_getfield(L, -1, variableName.c_str());
    }
    else
    {
        lua_getglobal(L, variableName.c_str());
    }
   
    return true;
}

/// \brief datamodel type for table.
///
/// Key and Value types are limited to: [string, bool, number]
class table final
{
public:
    friend std::ostream &operator<<(std::ostream &stream, const table &a);

    /// \brief construct a table from an existing table within a lua state
    table(lua_State *L, int aIndex);

    /// \brief construct a table with no content
    table() = default;

    /// \brief writes a value to this table (or to a subtable)
    //void write_value(path, value)
   
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

std::ostream &operator<<(std::ostream &out, const table &a)
{
    std::stringstream stream;

    stream << "{";

    const size_t size(a.m_NumberFields.size() +
        + a.m_BooleanFields.size()
        + a.m_StringFields.size());
    size_t i(0);

    for (const auto &[key, value] : a.m_NumberFields) 
    {
        if (key < 1 || std::fmod(key, 1) != 0) stream << "[" << key << "]" << "=";

        std::visit([&stream](auto &&value)
        {
            using value_type = std::decay_t<decltype(value)>;
            
            if constexpr (std::is_same_v<value_type, double>) stream << value;
            else if constexpr (std::is_same_v<value_type, bool>) stream << (value ? "true" : "false");
            else if constexpr (std::is_same_v<value_type, std::string>) stream << "\"" << value << "\"";
            else if constexpr (std::is_same_v<value_type, decltype(nullptr)>) stream << "nil";
            else if constexpr (std::is_same_v<value_type, std::shared_ptr<table>>) stream << *value;
            else throw std::runtime_error("table operator<<: unsupported type");
        }, value);

        if (++i != size) stream << ",";
    }
    
    for (const auto &[key, value] : a.m_BooleanFields) 
    {
        stream << "[" << (key ? "true" : "false") << "]" << "=";

        std::visit([&stream](auto &&value)
        {
            using value_type = std::decay_t<decltype(value)>;
            
            if constexpr (std::is_same_v<value_type, double>) stream << value;
            else if constexpr (std::is_same_v<value_type, bool>) stream << (value ? "true" : "false");
            else if constexpr (std::is_same_v<value_type, std::string>) stream << "\"" << value << "\"";
            else if constexpr (std::is_same_v<value_type, decltype(nullptr)>) stream << "nil";
            else if constexpr (std::is_same_v<value_type, std::shared_ptr<table>>) stream << *value;
            else throw std::runtime_error("table operator<<: unsupported type");
        }, value);

        if (++i != size) stream << ",";
    }

    for (const auto &[key, value] : a.m_StringFields) 
    {
        stream << key << "=";

        std::visit([&stream](auto &&value)
        {
            using value_type = std::decay_t<decltype(value)>;
            
            if constexpr (std::is_same_v<value_type, double>) stream << value;
            else if constexpr (std::is_same_v<value_type, bool>) stream << (value ? "true" : "false");
            else if constexpr (std::is_same_v<value_type, std::string>) stream << "\"" << value << "\"";
            else if constexpr (std::is_same_v<value_type, decltype(nullptr)>) stream << "nil";
            else if constexpr (std::is_same_v<value_type, std::shared_ptr<table>>) stream << *value;
            else throw std::runtime_error("table operator<<: unsupported type");
        }, value);

        if (++i != size) stream << ",";
    }
    
    stream << "}";

    out << stream.str();

    return out;
}

void table::push_to_lua_state(lua_State *L) const
{
    lua_newtable(L);

    for (const auto &[key, value] : m_NumberFields) std::visit([L, key](auto &&value)
    {
        using value_type = std::decay_t<decltype(value)>;
        
        if constexpr (std::is_same_v<value_type, double>)
        {
            lua_pushnumber(L, key);
            lua_pushnumber(L, value);
            lua_settable(L, -3);
        }
        else if constexpr (std::is_same_v<value_type, bool>) 
        {
            lua_pushnumber(L, key);
            lua_pushboolean(L, value);
            lua_settable(L, -3);
        }
        else if constexpr (std::is_same_v<value_type, std::string>) 
        {
            lua_pushnumber(L, key);
            lua_pushstring(L, value.c_str());
            lua_settable(L, -3);
        }
        else if constexpr (std::is_same_v<value_type, std::shared_ptr<table>>)
        {
            lua_pushnumber(L, key);
            value->push_to_lua_state(L);
            lua_settable(L, -3);
        }
        else throw std::runtime_error("table::push_to_lua_state");
    }, value);

    for (const auto &[key, value] : m_StringFields) std::visit([L, key](auto &&value)
    {
        using value_type = std::decay_t<decltype(value)>;
        
        if constexpr (std::is_same_v<value_type, double>)
        {
            lua_pushnumber(L, value);
            lua_setfield(L, -2, key.c_str());
        }
        else if constexpr (std::is_same_v<value_type, bool>) 
        {
            lua_pushboolean(L, value);
            lua_setfield(L, -2, key.c_str());
        }
        else if constexpr (std::is_same_v<value_type, std::string>) 
        {
            lua_pushstring(L, value.c_str());
            lua_setfield(L, -2, key.c_str());
        }
        else if constexpr (std::is_same_v<value_type, std::shared_ptr<table>>)
        {
            value->push_to_lua_state(L);
            lua_setfield(L, -2, key.c_str());
        }
        else throw std::runtime_error("table::push_to_lua_state");

    }, value);
    
    for (const auto &[key, value] : m_BooleanFields) std::visit([L, key](auto &&value)
    {
        using value_type = std::decay_t<decltype(value)>;
        
        if constexpr (std::is_same_v<value_type, double>)
        {
            lua_pushboolean(L, key);
            lua_pushnumber(L, value);
            lua_settable(L, -3);
        }
        else if constexpr (std::is_same_v<value_type, bool>) 
        {
            lua_pushboolean(L, key);
            lua_pushboolean(L, value);
            lua_settable(L, -3);
        }
        else if constexpr (std::is_same_v<value_type, std::string>) 
        {
            lua_pushboolean(L, key);
            lua_pushstring(L, value.c_str());
            lua_settable(L, -3);
        }
        else if constexpr (std::is_same_v<value_type, std::shared_ptr<table>>)
        {
            lua_pushboolean(L, key);
            value->push_to_lua_state(L);
            lua_settable(L, -3);
        }
        else throw std::runtime_error("table::push_to_lua_state");
    }, value);

    //for i in maps, push...)
}

table::table(lua_State *L, int aIndex)
{
    if (!lua_istable(L, aIndex)) throw std::runtime_error("index must point to a table");

    lua_pushvalue(L, aIndex);
    
    lua_pushnil(L);
    while (lua_next(L, -2))
    {
        lua_pushvalue(L, -2);

        std::variant<std::string, double, bool> key;

        switch(lua_type(L, -1))
        {
            case(LUA_TSTRING):  key = std::string(lua_tostring(L, -1)); break;
            case(LUA_TNUMBER):  key = lua_tonumber(L, -1); break;
            case(LUA_TBOOLEAN): key = static_cast<bool>(lua_toboolean(L, -1)); break;
            default: throw std::runtime_error("table only supports key types of [bool, number, string]");
        }

        std::visit([L, this](auto &&key)
        {
            using key_type = std::decay_t<decltype(key)>;

            if constexpr (std::is_same_v<key_type, std::string>)
            {
                std::string &fieldName = key;

                switch(lua_type(L, -2))
                {
                    case(LUA_TSTRING):  m_StringFields[fieldName] = std::string(lua_tostring(L, -2)); break;
                    case(LUA_TNUMBER):  m_StringFields[fieldName] = lua_tonumber(L, -2); break;
                    case(LUA_TBOOLEAN): m_StringFields[fieldName] = static_cast<bool>(lua_toboolean(L, -2)); break;
                    case(LUA_TTABLE):   m_StringFields[fieldName] = std::make_shared<table>(table(L, -2)); break;
                    default: throw std::runtime_error("table::table: unsupported value type\n");
                }
            }
            else if constexpr (std::is_same_v<key_type, double>)
            {
                auto &fieldName = key;

                switch(lua_type(L, -2))
                {
                    case(LUA_TSTRING):  m_NumberFields[fieldName] = std::string(lua_tostring(L, -2)); break;
                    case(LUA_TBOOLEAN): m_NumberFields[fieldName] = static_cast<bool>(lua_toboolean(L, -2)); break;
                    case(LUA_TNUMBER):  m_NumberFields[fieldName] = lua_tonumber(L, -2); break;
                    case(LUA_TTABLE):   m_NumberFields[fieldName] = std::make_shared<table>(table(L, -2)); break;
                    default: throw std::runtime_error("table::table: unsupported value type\n");
                }
            }
            else if constexpr (std::is_same_v<key_type, bool>)
            {
                auto &fieldName = key;

                switch(lua_type(L, -2))
                {
                    case(LUA_TSTRING):  m_BooleanFields[fieldName] = std::string(lua_tostring(L, -2)); break;
                    case(LUA_TBOOLEAN): m_BooleanFields[fieldName] = static_cast<bool>(lua_toboolean(L, -2)); break;
                    case(LUA_TNUMBER):  m_BooleanFields[fieldName] = lua_tonumber(L, -2); break;
                    case(LUA_TTABLE):   m_BooleanFields[fieldName] = std::make_shared<table>(table(L, -2)); break;
                    default: throw std::runtime_error("table::table: unsupported value type\n");
                }
            }
        }, key);

        lua_pop(L, 2);
    }

    lua_pop(L, 1);
}

using params_type = std::vector<std::variant<double, bool, std::string, decltype(nullptr), table>>;

/// \brief a lua interpreter
class interpreter final
{
public:
    /// \brief methods that can fail return this
    using error_type = std::optional<std::string>;

    /// \brief c++'s implementation of the closure is a lambda with a non-empty capture list
    using closure_type = std::function<params_type(params_type)>;

    /// \brief writes a boolean to the lua context
    void write_value(const std::string &aPath, const bool aValue);
    /// \brief writes a number to the lua context
    void write_value(const std::string &aPath, const double aValue);
    /// \brief writes a string to the lua context
    void write_value(const std::string &aPath, const std::string &aValue);
    /// \brief writes a string to the lua context
    void write_value(const std::string &aPath, const std::string::value_type *aValue);
    /// \brief writes a nil to the lua context
    void write_value(const std::string &aPath, std::nullptr_t);
    /// \brief writes a table to the lua context
    void write_value(const std::string &aPath, const table &);

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
    [[nodiscard]] error_type run_script(const std::string &aLuaScript) const;
    
    /// \brief construct an interpreter
    interpreter();

private:
    std::unique_ptr<lua_State, std::function<void(lua_State *)>>
        m_pState;

    std::unordered_map<std::string, closure_type> 
        m_RegisteredClosures;
};

std::optional<double> interpreter::read_number(const std::string &aPath) const
{
    auto *L(m_pState.get());

    std::optional<double> val;

    if (_read_value(L, aPath) && lua_isnumber(L, -1)) 
        val = lua_tonumber(L, -1);

    return val;
}

std::optional<bool> interpreter::read_boolean(const std::string &aPath) const
{
    auto *L(m_pState.get());

    std::optional<bool> val;

    if (_read_value(L, aPath) && lua_isboolean(L, -1)) 
        val = lua_toboolean(L, -1);

    return val;
}

std::optional<std::string> interpreter::read_string(const std::string &aPath) const
{
    auto *L(m_pState.get());

    std::optional<std::string> val;

    if (_read_value(L, aPath) && lua_isstring(L, -1))
    {
        size_t len;
        const char *str = lua_tolstring(L, -1, &len);

        std::string msg(str, len);
        val = msg;
    }

    return val;
}

std::optional<table> interpreter::read_table(const std::string &aPath) const
{
    auto *L = m_pState.get();
    
    if (_read_value(L, aPath) && lua_istable(L, -1)) return table(L, -1);

    return {};
}

void interpreter::write_value(const std::string &aPath, const bool aValue)
{
    _write_value(m_pState.get(), aPath, [L = m_pState.get(), aValue]()
        { lua_pushboolean(L, aValue); });
}

void interpreter::write_value(const std::string &aPath, const double aValue)
{
    _write_value(m_pState.get(), aPath, [L = m_pState.get(), aValue]()
        { lua_pushnumber(L, aValue); });
}

void interpreter::write_value(const std::string &aPath, const std::string &aValue)
{
    _write_value(m_pState.get(), aPath, [L = m_pState.get(), &aValue]()
        { lua_pushstring(L, aValue.c_str()); });
}

void interpreter::write_value(const std::string &aPath, const std::string::value_type *aValue)
{
    _write_value(m_pState.get(), aPath, [L = m_pState.get(), &aValue]()
        { lua_pushstring(L, aValue); });
}

void interpreter::write_value(const std::string &aPath, const table &aValue)
{
    _write_value(m_pState.get(), aPath, [L = m_pState.get(), &aValue]()
        { aValue.push_to_lua_state(L); });
}

void interpreter::register_function(const std::string &aName, closure_type aClosure)
{
    m_RegisteredClosures[aName] = aClosure;

    auto wrapper = [](lua_State *p)
    {
        auto *pImpl = static_cast<decltype(m_RegisteredClosures)::mapped_type *>(
            lua_touserdata(p, lua_upvalueindex(1)));

        params_type args;

        for (size_t i(1); i < 1 + lua_gettop(p); ++i)
        {
            if (lua_isnumber(p, i)) args.push_back(lua_tonumber(p, i));
            else if (lua_isboolean(p, i)) args.push_back(static_cast<bool>(lua_toboolean(p, i)));
            else if (lua_isstring(p, i))
            {
                size_t len;
                const char *str = lua_tolstring(p, i, &len);

                args.push_back(std::string(str, len));
            }
            else if (lua_istable(p, i)) args.push_back(table(p, i));
            else throw std::runtime_error("unsupported parameter type");
        }

        auto return_values = (*pImpl)(args);

        for (const auto &val : return_values) std::visit([&p](auto &&val)
        {
            using return_type = std::decay_t<decltype(val)>;
            
            if constexpr (std::is_same_v<return_type, double>) lua_pushnumber(p, val);        
            else if constexpr (std::is_same_v<return_type, bool>) lua_pushboolean(p, val);        
            else if constexpr (std::is_same_v<return_type, std::string>) lua_pushstring(p, val.c_str());        
            else if constexpr (std::is_same_v<return_type, table>) val.push_to_lua_state(p);
            else throw std::runtime_error("unsupported return type");
        }, val);

        return static_cast<int>(return_values.size());
    };

    _write_value(m_pState.get(), aName, [L = m_pState.get(), &aName, wrapper, this]()
    { 
        lua_pushlightuserdata(L, &(this->m_RegisteredClosures[aName]));
        lua_pushcclosure(L, wrapper, 1); 
    });
}

interpreter::interpreter() : m_pState(luaL_newstate(), [](lua_State *p){lua_close(p);}) {}

interpreter::error_type interpreter::run_script(const std::string &aLuaScript) const
{
    switch (const auto status = luaL_dostring(m_pState.get(), aLuaScript.c_str()))
    {
        case LUA_OK: return {};
        default: return {lua_tostring(m_pState.get(), -1)};
    }
}

interpreter::error_type interpreter::validate_syntax(const std::string &aLuaScript) const
{
    switch (const auto status = luaL_loadstring(m_pState.get(), aLuaScript.c_str()))
    {
        case LUA_OK: 
        {
            lua_pop(m_pState.get(), 1);

            return {};
        }

        default: return {lua_tostring(m_pState.get(), -1)};
    }
}

static const std::string init((R"V0G0N(
junk = {}
debug = {
    a = {
        b = "wowee", --This gets overwritten by a table in main
        goof = "hello",
        troop = 564,
    }
}
very = {
    [-1] = "negative1_val",
    [0] = "zero_value",
    "one_value",
    "anon_one",
    [3.1415] = "pi",
    "anon_two",
    "anon_three",
    123,
    ["asdf"] = "asdf value",
    { "nothing" },
    cool = {
        "one",
        "two",
        "three!"
    },
    [true] = "lol",
    [0.1256] = "whoa",
    "月",
    "火",
    "水",
    "木",
    "金",
    "土",
    "日"
}

)V0G0N"));

static const std::string script((R"V0G0N(
debug.print("-------------------")
debug.print("table: ", very )
)V0G0N"));

int main(int argc, char *argv[])
{
    interpreter interp;

    if (interp.run_script(init)) std::cout << "error in init\n";

    interp.write_value("written", "This is not a test");
    interp.write_value("debug.written", 456.);
    interp.write_value("debug.a.b.c.d.written", 6545.);
    interp.write_value("debug.a.b.c.d.stringy", "Whats going on");
    interp.write_value("global_number", 123.);
    interp.write_value("nested.number", 1.);
    interp.write_value("nes.ted.number", 2.);
    interp.write_value("debug.a.b.mybool", true);
    interp.write_value("debug.a.b.mynumber", 678.);
    
    interp.register_function("debug.print", [](params_type args) -> params_type
    {
        std::stringstream stream;

        for (const auto &arg : args) std::visit([&stream](auto &&arg)
        {
            stream << arg;
        }, arg);

        stream << "\n";

        std::cout << stream.str();

        return {};
    });

    interp.register_function("math.add", [](params_type args) -> params_type
    {
        double out(0);

        for (const auto &arg : args) std::visit([&out](auto &&arg)
        {
            using arg_type = std::decay_t<decltype(arg)>;

            if constexpr (std::is_same_v<arg_type, double>) out += arg;
            else throw std::runtime_error("unsupported arg type");
        }, arg);

        return {out};
    });

    if (auto error = interp.run_script(script))
        std::cout << "interp error: " << *error << "\n";

    auto val = interp.read_table("very");

    interpreter second_interpreter;
    second_interpreter.register_function("debug.print", [](params_type args) -> params_type
    {
        std::stringstream stream;

        for (const auto &arg : args) std::visit([&stream](auto &&arg)
        {
            stream << arg;
        }, arg);

        stream << "\n";

        std::cout << stream.str();

        return {};
    });
    
    std::stringstream ss;

    ss << "debug.print(\"---- INTERP 2 ----\")\n";
    ss << "message=" << *val << "\n";
    //ss << "debug.print(\"table: \", message)";
    ss << "debug.print(\"copy: \", another_table)";

    second_interpreter.write_value("another_table", *val);

    if (auto error = second_interpreter.run_script(ss.str()))
        std::cout << "interp error: " << *error << "\n";

    return EXIT_SUCCESS;
}

