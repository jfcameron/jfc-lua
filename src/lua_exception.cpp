// © Joseph Cameron - All Rights Reserved

#include <jfc/lua_exception.h>

using namespace jfc;

const char *lua_exception::what() const noexcept {
    return mWhat.c_str();
}

lua_exception::lua_exception(std::string aWhat)
: mWhat(std::move(aWhat))
{}
