// © Joseph Cameron - MIT License

#include <jfc/lua/exception.h>

namespace jfc::lua {
    exception::exception()
    : exception("jfc::lua::exception")
    {}

    exception::exception(const std::string &aWhat)
    : mpWhat(std::make_shared<const std::string>(aWhat))
    {}

    const char *exception::what() const noexcept { return mpWhat->c_str(); }
}
