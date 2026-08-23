// © Joseph Cameron - All Rights Reserved

#ifndef JFC_LUA_EXCEPTION_H
#define JFC_LUA_EXCEPTION_H

#include <jfc/lua_types.h>

#include <exception>
#include <string>

namespace jfc {
    /// \brief root exception type for this project
    class lua_exception : public std::exception {
    public:
        lua_exception() = default;
        
        lua_exception(std::string aWhat);
       
         virtual ~lua_exception() override = default;

        virtual const char *what() const noexcept override;

    private:
        std::string mWhat = "jfc::lua_exception";
    };
}

#endif

