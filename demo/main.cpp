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

bool _read_value(lua_State *L, const std::string &aPath)
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
            
            if (!nestedTableExists)
            {
                return false;
            }
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
class table //TODO
{
public:
    /// \brief values contained in this table
    using values_collection_type = std::unordered_map<std::string, 
        std::variant<double, bool, std::string, decltype(nullptr)>>;

    /// \brief tables contained in this table
    using children_collection_type = std::unordered_map<std::string,
        std::shared_ptr<table>>;

    /// \brief construct a table from an existing table within a lua state
    table(lua_State *L, int aIndex);

    /// \brief construct a table with no content
    table() = default;

    /// \brief writes a value to this table (or to a subtable)
    //void write_value(path, value)
   
    /// \brief writes the table to a lua state
    //void write_to_state(lua_State *L, std::string aPath);

    /// \brief serialize to string
    //friend operator<<

private:
    children_collection_type m_Children;

    values_collection_type m_Values;
};

//TODO: implement deserialization here.
// depth first is likely the easiest to implement
table::table(lua_State *L, int aIndex)
{
    if (!lua_istable(L, aIndex)) throw std::runtime_error("index must point to a table");

    std::cout << "table::table from lua\n";

    /*lua_pushnil(L);
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
    }*/
}

using params_type = std::vector<std::variant<double, bool, std::string, decltype(nullptr), table>>;

//table to string serializer. 
//std::string string_table_serializer(const table &) {return "";} //TODO
/*
class table_serializer
{
    ???? serialize(table)
    table deserialize(????)
}; //TODO: do some reading about good serialization interface design
*/

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
    //void write_value(const std::string &aPath, table);

    /// \brief reads a boolean from the lua context
    std::optional<bool> read_boolean(const std::string &aPath) const;
    /// \brief reads a number from the lua context
    std::optional<double> read_number(const std::string &aPath) const;
    /// \brief reads a string from the lua context
    std::optional<std::string> read_string(const std::string &aPath) const;
    /// \brief reads a table from the lua context
    //std::optional<table> read_table(const std::string &aPath) const;
    /// \brief reads a value of unknown type
    //std::optional<std::variant<bool, double, std::string, table> read_any(const std::string &aPath) const;

    //TODO: calls a lua function if it exists: use for eg callbacks
    //error try_call_function(path..to..function, paramlist...);

    /// \brief registers a closure (c++ lambda with captured data)
    void register_function(const std::string &aName, closure_type a);

    /// \brief checks for basic synatx errors. 
    ///
    /// Not required to be called before run_script, but useful if
    /// the user is trying to catch lua issues early
    error_type validate_syntax(const std::string &aLuaScript) const;
    
    /// \brief run a script, returns an error if something went wrong
    error_type run_script(const std::string &aLuaScript) const;
    
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

/*std::optional<table> interpreter::read_table(const std::string &aPath) const
{
    std::cout << "read_table\n";

    auto *L = m_pState.get();

    const auto processed_path = _parse_pathstring(aPath);
    const auto path(&std::get<0>(processed_path));
    const auto variable_name(&std::get<1>(processed_path));
    
    if (_read_value(L, aPath) && lua_istable(L, -1))
    {
        //recurse it, coping data out

    }

    return {};
}*/

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
            else if (lua_istable(p, i)) 
            {
                std::cout << "table found\n";
                args.push_back(table(p, i));
            }
            else throw std::runtime_error("unsupported parameter type");
        }

        auto return_values = (*pImpl)(args);

        for (const auto &val : return_values) std::visit([&p](auto &&val)
        {
            using return_type = std::decay_t<decltype(val)>;
            
            if constexpr (std::is_same_v<return_type, double>) lua_pushnumber(p, val);        
            else if constexpr (std::is_same_v<return_type, bool>) lua_pushboolean(p, val);        
            else if constexpr (std::is_same_v<return_type, std::string>) lua_pushstring(p, val.c_str());        
            else if constexpr (std::is_same_v<return_type, table>)
            {
                //TODO: support table
                std::cout << "table param!\n";
            }
            else
            {
                throw std::runtime_error("unsupported return type");
            }
        }, val);

        return static_cast<int>(return_values.size());
    };

    _write_value(m_pState.get(), aName, [L = m_pState.get(), &aName, wrapper, this]()
    { 
        lua_pushlightuserdata(L, &(this->m_RegisteredClosures[aName]));
        lua_pushcclosure(L, wrapper, 1); 
    });
}

interpreter::interpreter()
: m_pState(luaL_newstate(), [](lua_State *p){lua_close(p);})
{}

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
        goof = "hello"
    }
}
)V0G0N"));

static const std::string script((R"V0G0N(
debug.print("-------------------")
--debug.print("global: " .. written)
--debug.print("nested: ", debug.a.b.c.d.written)
--debug.print("this ", "is ", "not ", "a ", "test")
--debug.print("adding numbers: ", math.add(1, 2, 3))
debug.print("table: ", debug.a.b )
)V0G0N"));

int main(int argc, char *argv[])
{
    interpreter interp;

    interp.run_script(init);

    interp.write_value("written", "This is not a test");
    interp.write_value("debug.written", 456.);
    interp.write_value("debug.a.b.c.d.written", 6545.);
    interp.write_value("global_number", 123.);
    interp.write_value("nested.number", 1.);
    interp.write_value("nes.ted.number", 2.);
    
    interp.register_function("debug.print", [](params_type args) -> params_type
    {
        std::stringstream stream;

        for (const auto &arg : args) std::visit([&stream](auto &&arg)
        {
            using arg_type = std::decay_t<decltype(arg)>;

            if constexpr (!std::is_same_v<arg_type, table>) stream << arg;
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
        std::cout << "fist_interp: " << *error << "\n";

    /*auto val = interp.read_number("global_number");
    std::cout << "val: " << *val << "\n";

    {
        auto nestval = interp.read_number("nested.number");
        std::cout << "nest val: "; 
        if (nestval.has_value()) std::cout << *nestval;
        else std::cout << "NOPE";
        std::cout << "\n";
    }
    {
        auto nestval = interp.read_number("nes.ted.number");
        std::cout << "nest val: "; 
        if (nestval.has_value()) std::cout << *nestval;
        else std::cout << "NOPE";
        std::cout << "\n";
    }*/

    return EXIT_SUCCESS;
}

