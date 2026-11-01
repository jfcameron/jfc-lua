// © Joseph Cameron - MIT License

#ifndef JFC_LUA_EXCEPTION_H
#define JFC_LUA_EXCEPTION_H

#include <jfc/lua/types.h>

#include <exception>
#include <memory>
#include <string>

namespace jfc::lua {
    /// \brief root exception type for this library
    ///
    /// One type, so a caller can catch everything this library throws without naming each case.
    class exception : public std::exception {
        std::shared_ptr<const std::string> mpWhat;

    public:
        exception();

        exception(const std::string &aWhat);

        exception(const exception &aOther) noexcept = default;

        exception &operator=(const exception &aOther) noexcept = default;

        const char *what() const noexcept override;
    };
}

#endif
