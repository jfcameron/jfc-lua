// © Joseph Cameron - All Rights Reserved

#ifndef JFC_LUA_TYPES_H
#define JFC_LUA_TYPES_H

#include <memory>
#include <variant>
#include <optional>
#include <string>

struct lua_State;

namespace jfc::lua
{
    class environment;
    class interpreter;
    class data_table;

    struct environment_policy;
    struct interpreter_policy;

    struct interpreter_limits;

    class key;
    class path;
    class reference;
    class userdata;

    /// \brief what a lua table may be indexed by
    using key_type = std::variant<double, bool, std::string>;

    /// \brief methods that can fail return this.
    using error_type = std::optional<std::string>;

    /// \brief environments are handed out by shared_ptr
    using environment_shared_ptr_type = std::shared_ptr<environment>;
}

#endif
