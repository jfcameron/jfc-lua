// © Joseph Cameron - All Rights Reserved

#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>

#include <lua.hpp>

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

static void _write_value(lua_State *L, const std::string &aPathString, std::function<void()> &&aPushValueFunctor)
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

    /*if (auto pVal = std::get_if<bool>(&aValue)) lua_pushboolean(L, static_cast<int>(*pVal));
    else if (auto pVal = std::get_if<double>(&aValue)) lua_pushnumber(L, *pVal);
    else if (auto pVal = std::get_if<std::string>(&aValue)) lua_pushstring(L, pVal->c_str());
    else lua_pushnil(L);*/
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
{
    lua_newtable(m_pState.get());
    lua_setglobal(m_pState.get(), "b");

    lua_getglobal(m_pState.get(), "b");
    lua_pushstring(m_pState.get(), "blar");
    lua_pushstring(m_pState.get(), "this is not a test");
    lua_settable(m_pState.get(), -3);
}

interpreter::error_type interpreter::try_run_script(const std::string &aLuaScript) const
{
    //TODO: replace with load and run, hash the string, keep loaded around?
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
    interpreter second_interp;

    interp.try_run_script(init);

    interp.write_value("written", "This is not a test");
    interp.write_value("debug.written", 456.);
    interp.write_value("debug.a.b.c.d.written", 6545.);
    interp.write_value("global_number", 123.);

    /*int w = 123;

    interp.register_function("b.very_cool", [&w](lua_State *p)
    {
        double d = lua_tonumber(p, 1);
           
        d = std::sin(d) + w;

        lua_pushnumber(p, d);
        return 1;
    });

    interp.register_function("very_silly", [](lua_State *p)
    {
        double d = lua_tonumber(p, 1);
           
        d -= 123;

        lua_pushnumber(p, d);
        return 1;
    });*/
    
    interp.register_function("print", [](lua_State *p)
    {
        size_t len;
        const char *str = lua_tolstring(p, 1, &len);
        
        std::cout << str << "\n";

        return 0;
    });

    if (auto error = interp.try_run_script(script))
        std::cout << "fist_interp: " << *error << "\n";

    return EXIT_SUCCESS;
}

