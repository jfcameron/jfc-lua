// © Joseph Cameron - All Rights Reserved

#include <any>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>

#include <lua.hpp>

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

    aPushValueFunctor();

    if (path.empty()) lua_setglobal(L, variableName.c_str());
    else lua_setfield(L, -2, variableName.c_str());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////////////////////////

template<class F>
struct function_traits;

// function pointer
template<class R, class... Args>
struct function_traits<R(*)(Args...)> : public function_traits<R(Args...)>
{};

template<class R, class... Args>
struct function_traits<R(Args...)>
{
    using return_type = R;

    static constexpr std::size_t arity = sizeof...(Args);

    template <std::size_t N>
    struct argument
    {
        static_assert(N < arity, "error: invalid parameter index.");
        using type = typename std::tuple_element<N,std::tuple<Args...>>::type;
    };

    using parameter_list_type = std::tuple<Args...>;
};

// member function pointer
template<class C, class R, class... Args>
struct function_traits<R(C::*)(Args...)> : public function_traits<R(C&,Args...)>
{};

// const member function pointer
template<class C, class R, class... Args>
struct function_traits<R(C::*)(Args...) const> : public function_traits<R(C&,Args...)>
{};

// member object pointer
template<class C, class R>
struct function_traits<R(C::*)> : public function_traits<R(C&)>
{};

///////////////////////////////////////////////////////////////////////////////////////////////////

/// \brief a lua interpreter
class interpreter final
{
public:
    /// \brief methods that can fail return this
    using error_type = std::optional<std::string>;
    /// \brief c++'s implementation of the closure is a lambda with a non-empty capture list
    using closure_type = std::function<int(lua_State *)>;

    /// \brief writes a boolean to the lua context
    void write_value(const std::string &aName, const bool aValue);
    /// \brief writes a number to the lua context
    void write_value(const std::string &aName, const double aValue);
    /// \brief writes a string to the lua context
    void write_value(const std::string &aName, const std::string &aValue);
    /// \brief writes a c string to the lua context
    void write_value(const std::string &aName, const std::string::value_type *aValue);
    /// \brief writes a nil to the lua context
    void write_value(const std::string &aName, std::nullptr_t);
    
    /// \brief registers a closure (c++ lambda with captured data)
    void register_function(const std::string &aName, closure_type a);

    /// TODO: workinprogress. templatized function reg
    std::map<std::string, std::shared_ptr<std::any>> m_any; //func name -> ptr to any
    template<class _FunctionType>
    void register_function_(const std::string &aName, std::function<_FunctionType> a) 
    {
        /*using traits = function_traits<void(int)>;//_FunctionType>;
        typename traits::parameter_list_type params;
        std::apply([](auto ... x)
        {
            ((
                [x]()
                {
                    using arg_t = decltype(x);

                    if constexpr(std::is_same<arg_t, std::string &>::value)
                    {
                        //x = "strings are bad and I dont like them!"; 
                    }
                    else
                    if constexpr(std::is_same<arg_t, double &>::value)
                    {
                        x *= 2;
                    }
                }()
             ), ...);
        }, params);*/

        //TODO: obviously storage needs to be instanced
        static std::unordered_map<std::string, decltype(a)> m_Registered;
        /*m_any_back(std::make_shared<std::any>(
            std::unordered_map<std::string, decltype(a)>()
        ));*/
        //auto m_Registered = std::any_cast<std::unordered_map<std::string, decltype(a)>>(*m_any.back());

        m_Registered[aName] = a;


        _write_value(m_pState.get(), aName, [L = m_pState.get(), aName, this, a]()
        {
            std::cout << "registering new func: " << aName << "\n";

            //TODO: constexpr assert param and return types are legal

            auto wrapper = [](lua_State *p) -> int
            {
                auto *pImpl = reinterpret_cast<typename decltype(m_Registered)::mapped_type *>(
                    lua_touserdata(p, lua_upvalueindex(1)));
               
                using traits = function_traits<_FunctionType>;
              
                typename traits::parameter_list_type params;

                /*std::array<std::variant<double, bool, std::string>, traits::arity> params_values;
                std::apply([p, &params_values](auto&... args) 
                {
                    size_t count(0);
                    ((
                        [p, &args, &count, &params_values]()
                        {
                            using arg_type = typename std::remove_reference<decltype(args)>::type;
                            
                            count++;

                            if (std::is_same<arg_type, double>::value)
                            {
                                std::cout << count << " is double\n";
                                
                                params_values[count - 1] = lua_tonumber(p, count);

                                //args = double(123);
                            }
                            else if (std::is_same<arg_type, std::string>::value)
                            {
                                std::cout << count << " is string\n";
                                
                                params_values[count - 1] = lua_tostring(p, count);

                                //args = std::string();
                            }
                            else if (std::is_same<arg_type, bool>::value)
                            {
                                std::cout << count << " is bool\n";

                                params_values[count - 1] = static_cast<bool>(lua_toboolean(p, count));
                            }
                            // ... handle rest of types ...
                        }()
                    ), ...); 
                }, params);*/

                // call it, capture val
                auto return_value = std::apply((*pImpl),params);

                if constexpr(std::is_same<decltype(return_value), double>::value) 
                {
                    std::cout << "return type is a number\n";
                    lua_pushnumber(p, return_value);
                    return 1;
                }
                else if constexpr(std::is_same<decltype(return_value), std::string>::value) 
                {
                    std::cout << "return type is a string\n";
                    lua_pushstring(p, return_value.c_str());
                    return 1;
                }
                //TODO handle remaining types
                //...
                else
                {
                    std::cout << "return type is void\n";
                    return 0; //TODO: support multi return?
                }
            };

            //auto m_Registered = std::any_cast<std::unordered_map<std::string, decltype(a)>>(*(this->m_any.back()));

            lua_pushlightuserdata(L, &(m_Registered[aName]));

            lua_pushcclosure(L, wrapper, 1);
        });
    }

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
    std::unique_ptr<lua_State, std::function<void(lua_State *)>> m_pState;

    std::unordered_map<std::string, closure_type> m_RegisteredClosures;
};

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

    _write_value(m_pState.get(), aName, [L = m_pState.get(), aName, this, aClosure]()
    { 
        auto wrapper = [](lua_State *p)
        {
            auto *pImpl = static_cast<decltype(m_RegisteredClosures)::mapped_type *>(
                lua_touserdata(p, lua_upvalueindex(1)));
            
            return (*pImpl)(p);
        };

        lua_pushlightuserdata(L, &(this->m_RegisteredClosures[aName]));

        lua_pushcclosure(L, wrapper, 1); 
    });
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

interpreter::error_type interpreter::run_script(const std::string &aLuaScript) const
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
    --cpp_var = debug.a.b.c.d.very_cool(1, 2)

    --print("c++ defined func return: " .. cpp_var)
    --print("c++ defined global: " .. written)
    --print("c++ defined nested: " .. debug.written)
    --print("c++ defined nested: " .. debug.a.b.c.d.written)
    --print("adhoc: " .. debug.a.goof)
    --print(debug.a.b)

    --print("asfd: " .. Very.Cool(123, "blar"))
    Very.Cool(123, "blar", true)
)V0G0N"));

template<typename... arg_types>
void log(arg_types... args)
{
    std::stringstream ss;

    (( ss << args ), ...);

    ss << "\n";
    
    std::cout << ss.str();
}

int main(int argc, char *argv[])
{
    interpreter interp;
    interpreter second_interp;

    interp.run_script(init);

    interp.write_value("written", "This is not a test");
    interp.write_value("debug.written", 456.);
    interp.write_value("debug.a.b.c.d.written", 6545.);
    interp.write_value("global_number", 123.);

    int w = 100;

    //incomplete, better interface
    interp.register_function_<double(double, std::string, bool)>("Very.Cool", [](double a, std::string b, bool c)
    {
        std::cout << "Very.Cool was called\n";
        //std::cout << a << b << c << "\n";

        return double(123);
    });

    interp.register_function("debug.a.b.c.d.very_cool", [&w](lua_State *p)
    {
        double a = lua_tonumber(p, 1);
        double b = lua_tonumber(p, 2);
           
        a = w + a + b;

        lua_pushnumber(p, a);

        return 1;
    });
    
    interp.register_function("print", [](lua_State *p)
    {
        size_t len;
        const char *str = lua_tolstring(p, 1, &len);
        
        std::cout << str << "\n";

        return 0;
    });

    if (auto error = interp.run_script(script))
        std::cout << "fist_interp: " << *error << "\n";

    //
    
    std::cout << "\n";
    using traits = function_traits<void(int, std::string)>;
    typename traits::parameter_list_type params;

    std::apply([](auto &... x)
    {
        ((
            [&x]()
            {
                using arg_t = decltype(x);

                if constexpr(std::is_same<arg_t, std::string &>::value)
                {
                    x = "strings are bad and I dont like them!"; 
                }
                else
                if constexpr(std::is_same<arg_t, double &>::value)
                {
                    x *= 2;
                }
            }()
        ), ...);
    }, params);

    std::apply([](auto &&... x)
    {
        (( std::cout << x << "\n" ), ...);
    }, params);

    log("asdf: ", 1, " goober: ", 2.);

    return EXIT_SUCCESS;
}

