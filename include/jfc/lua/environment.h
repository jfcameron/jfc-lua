// © Joseph Cameron - All Rights Reserved

#ifndef JFC_LUA_ENVIRONMENT_H
#define JFC_LUA_ENVIRONMENT_H

#include <jfc/lua/path.h>
#include <jfc/lua/reference.h>
#include <jfc/lua/data_table.h>
#include <jfc/lua/types.h>
#include <jfc/lua/userdata.h>

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace jfc::lua
{
    /// \brief how much of the lua standard library an environment can reach
    enum class standard_library
    {
        /// \brief computation only: string, table, math, coroutine, bit, the clock parts of os, and the
        /// base functions that cannot reach outside the vm
        safe,

        /// \brief everything luaL_openlibs opened
        ///
        /// \warn includes debug, whose getregistry() function can be used to access every other environment 
        /// in the same lua context as well as run commands on the system
        unrestricted
    };


    /// \brief user-definable configuration for one environment
    struct environment_policy final
    {
        /// \brief how much of the lua standard library scripts running here can reach
        const standard_library LIBRARY{standard_library::safe};

        /// \brief vm instructions one call to this environment's `run` may execute
        ///
        /// Empty defers to the interpreter's figure, which is the usual case. 
        const std::optional<std::size_t> INSTRUCTION_BUDGET{};
    };

    /// \brief an independent set of global variables, and the scripts that run against it
    class environment final : public reference
    {
    public:
        /// \brief c++'s implementation of the closure is a lambda with a non-empty capture list
        using closure_type = std::function<value_list_type(value_list_type)>;

        /// \brief call a lua function
        [[nodiscard]] error_type call(const path &aPath, const value_list_type &aArguments,
            value_list_type &aResults);

        /// \brief call a lua function, discarding whatever it returns
        [[nodiscard]] error_type call(const path &aPath, const value_list_type &aArguments = {});

        /// \brief registers a closure (c++ lambda with captured data) into this environment alone
        void register_function(const path &aName, closure_type a);

        /// \brief checks for basic syntax errors.
        ///
        /// This is not required to called before running a script, a typical use case
        /// would be in a script editor, in order to catch syntax errors in a script
        /// that is actively being developed.
        [[nodiscard]] error_type validate_syntax(const std::string &aLuaScript) const;

        /// \brief run a script here, returns an error if something went wrong
        [[nodiscard]] error_type run(const std::string &aLuaScript);

        environment(const environment &) = delete;
        environment &operator=(const environment &) = delete;

    private:
        friend class interpreter;

        environment(std::shared_ptr<lua_State> aState, const int aRootReference,
            const std::optional<std::size_t> aInstructionBudget);

        std::optional<std::size_t> m_InstructionBudget;
    };
}

#endif
