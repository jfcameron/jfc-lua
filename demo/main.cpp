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

class variable;

/// \brief value type representation of a lua table
class table
{
public:
    //void insert(const std::string &aName, variable &);
    
private:
    //std::map<std::string, variable> m_fields;
};

/*class variable
{
public:*/
    using value_type = std::variant<bool,
        double,
        std::string,
        table,
        decltype(nullptr)>;
//};

/*void table::insert(const std::string &aName, variable &v)
{
    m_fields[aName] = v;
}*/

/// \brief a lua interpreter
class interpreter final
{
public:
    using error_type = std::optional<std::string>;
    using function_type = auto (*)(lua_State*) -> int;
    using closure_type = std::function<int(lua_State *)>;

    void write_value(const std::string &path, const value_type &var);

    //TODO: calls a lua function if it exists: use for eg callbacks
    //error try_call_function(path..to..function, paramlist...);

    //TODO plain function version. faster, less mem
    /// \brief registers a function (c function pointer)
    //void register_function(...)
    //TODO handle errors etc
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
void interpreter::write_value(const std::string &path, const value_type &var)
{
    static const std::string delimiter(".");

    std::vector<std::string> table_names;

    size_t last(0), next; 

    while ((next = path.find(delimiter, last)) != std::string::npos) 
    {   
        table_names.push_back(path.substr(last, next - last));

        last = next + 1; 
    }

    const std::string variableName = path.substr(last);

    if (table_names.size())
    {
        auto asdf = lua_getglobal(m_pState.get(), table_names[0].c_str());
        {
            //LUA_TTABLE
        }

        

        //ilua_getglobalf (global
    }

    /*
    if (table.size())
    {
        if (globaltable table[0] !exist) makeglobaltable table[0]

        if (table.size() > 1)
        {
            if globaltable !has table[1] maketable in globaltable

            for i(2) < table.size; ++i
            {
                if table[i - 1] !has table[i] maketable in table[i - 1]

                if (i == table.size - 1) write value to table
            }
        }
    {
    */
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

static const std::string script((R"V0G0N(
    a = 10
    print(b.blar)
    print(b.very_cool("asdf"))
    print(very_silly(a))
)V0G0N"));

int main(int argc, char *argv[])
{
    interpreter interp;
    interpreter second_interp;

    interp.write_value("debug.logging.utils", 123.);

    int w = 123;

    interp.register_function("b.very_cool", [&w](lua_State *p)
    {
        double d = lua_tonumber(p, 1);
           
        d = std::sin(d) + w;

        lua_pushnumber(p, d);
        return 1;
    });

    interp.register_function("print", [](lua_State *p)
    {
        size_t len;
        const char *str = lua_tolstring(p, 1, &len);
        
        std::cout << str << "\n";

        return 0;
    });

    interp.register_function("very_silly", [](lua_State *p)
    {
        double d = lua_tonumber(p, 1);
           
        d -= 123;

        lua_pushnumber(p, d);
        return 1;
    });

    if (auto error = interp.try_run_script(script))
        std::cout << "fist_interp: " << *error << "\n";

    /*if (auto error = second_interp.try_run_script(script))
        std::cout << "second_itnerp: " << *error << "\n";*/

    return EXIT_SUCCESS;
}

