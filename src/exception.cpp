// © Joseph Cameron - MIT License

#include <jfc/lua/exception.h>

using namespace jfc::lua;

const char *exception::what() const noexcept {
    return mWhat.c_str();
}

exception::exception(std::string aWhat)
: mWhat(std::move(aWhat))
{}
