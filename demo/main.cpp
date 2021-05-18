// © Joseph Cameron - All Rights Reserved

#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <variant>

#include <lua.hpp>

class table
{
    using value_type = std::unordered_map<std::string, 
        std::variant<double, bool, std::string>>;

    using children_type = std::unordered_map<std::string,
        std::unique_ptr<table>>;

    //...
};

/// \brief a lua interpreter
class interpreter final
{
public:
    /// \brief methods that can fail return this
    using error_type = std::optional<std::string>;
    /// \brief c++'s implementation of the closure is a lambda with a non-empty capture list
    using closure_type = std::function<int(lua_State *)>;

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

    /// \brief reads a boolean from the lua context
    std::optional<bool> read_boolean(const std::string &aPath);
    /// \brief reads a number from the lua context
    std::optional<double> read_number(const std::string &aPath);
    /// \brief reads a string from the lua context
    std::optional<std::string> read_string(const std::string &aPath);

    //TODO: calls a lua function if it exists: use for eg callbacks
    //error try_call_function(path..to..function, paramlist...);

    /// \brief registers a closure (c++ lambda with captured data)
    void register_function(const std::string &aName, closure_type a);

    /// \brief checks for basic synatx errors. 
    ///
    /// Not required to be called before try_run_script, but useful if
    /// the user is trying to catch lua issues early
    error_type validate_syntax(const std::string &aLuaScript) const;
    
    /// \brief run a script, returns an error if something went wrong
    error_type try_run_script(const std::string &aLuaScript) const;
    
    /// \brief construct an interpreter
    interpreter();

private:
    std::unique_ptr<lua_State, std::function<void(lua_State *)>>
        m_pState;

    std::unordered_map<std::string, closure_type> 
        m_RegisteredClosures;
};

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

std::optional<double> interpreter::read_number(const std::string &aPath)
{
    auto *L(m_pState.get());

    std::optional<double> val;

    if (_read_value(L, aPath) && lua_isnumber(L, -1)) 
        val = lua_tonumber(L, -1);

    return val;
}

std::optional<bool> interpreter::read_boolean(const std::string &aPath)
{
    auto *L(m_pState.get());

    std::optional<bool> val;

    if (_read_value(L, aPath) && lua_isboolean(L, -1)) 
        val = lua_toboolean(L, -1);

    return val;
}

std::optional<std::string> interpreter::read_string(const std::string &aPath)
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

void interpreter::register_function(const std::string &aName, 
    closure_type aClosure)
{
    m_RegisteredClosures[aName] = aClosure;

    auto wrapper = [](lua_State *p)
    {
        auto *pImpl = static_cast<decltype(m_RegisteredClosures)::mapped_type *>(
            lua_touserdata(p, lua_upvalueindex(1)));
        
        return (*pImpl)(p);
    };

    lua_pushlightuserdata(m_pState.get(), &(this->m_RegisteredClosures[aName]));
    lua_pushcclosure(m_pState.get(), wrapper, 1); 
    
    lua_setglobal(m_pState.get(), aName.c_str());
}

interpreter::interpreter()
: m_pState(luaL_newstate(), [](lua_State *p){lua_close(p);})
{}

interpreter::error_type interpreter::try_run_script(const std::string &aLuaScript) const
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
            b = "123",
            goof = "hello"
        }
    }
)V0G0N"));

static const std::string script((R"V0G0N(
    print("-------------------")
    print("c++ defined global: " .. written)
    print("c++ defined nested: " .. debug.written)
    print("c++ defined nested: " .. debug.a.b.c.d.written)
    print("adhoc: " .. debug.a.goof)
    print(debug.a.b)
)V0G0N"));

int main(int argc, char *argv[])
{
    interpreter interp;

    interp.try_run_script(init);

    interp.write_value("written", "This is not a test");
    interp.write_value("debug.written", 456.);
    interp.write_value("debug.a.b.c.d.written", 6545.);
    interp.write_value("global_number", 123.);
    interp.write_value("nested.number", 1.);
    interp.write_value("nes.ted.number", 2.);
    
    interp.register_function("print", [](lua_State *p)
    {
        size_t len;
        const char *str = lua_tolstring(p, 1, &len);

        std::string msg(str, len);
        
        std::cout << msg << "\n";

        return 0;
    });

    if (auto error = interp.try_run_script(script))
        std::cout << "fist_interp: " << *error << "\n";

    auto val = interp.read_number("global_number");
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
    }

    return EXIT_SUCCESS;
}

