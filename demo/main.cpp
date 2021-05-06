// © Joseph Cameron - All Rights Reserved

#include <cstdlib>
#include <memory>
#include <functional>

#include <lua.hpp>

int main(int argc, char *argv[])
{
    std::unique_ptr<lua_State, std::function<void(lua_State *)>> 
        pL(luaL_newstate(), [](lua_State *p){lua_close(p);});

    luaL_openlibs(pL.get());
    
    luaL_dostring(pL.get(),(R"V0G0N(
        print("greetings from lua")
    )V0G0N"));

    return EXIT_SUCCESS;
}

