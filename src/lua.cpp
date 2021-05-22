// © Joseph Cameron - All Rights Reserved

#include <jfc/lua.h>

#include <lua.hpp>

#include <cmath>
#include <sstream>
#include <stdexcept>
#include <tuple>

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

namespace jfc::lua
{
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

    interpreter::error_type interpreter::run(const std::string &aLuaScript) const
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
}

