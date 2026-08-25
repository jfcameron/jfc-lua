// © Joseph Cameron - All Rights Reserved

#ifndef JFC_LUA_EXCEPTION_H
#define JFC_LUA_EXCEPTION_H

#include <jfc/lua/types.h>

#include <exception>
#include <string>

namespace jfc::lua {
    /// \brief root exception type for this library
    ///
    /// One type, so a caller can catch everything this library throws without naming each case.
    class exception : public std::exception {
    public:
        exception() = default;

        exception(std::string aWhat);

        virtual ~exception() override = default;

        virtual const char *what() const noexcept override;

    private:
        std::string mWhat = "jfc::lua::exception";
    };
}

#endif
