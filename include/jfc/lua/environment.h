// © Joseph Cameron - MIT License

#ifndef JFC_LUA_ENVIRONMENT_H
#define JFC_LUA_ENVIRONMENT_H

#include <jfc/lua/coroutine.h>
#include <jfc/lua/path.h>
#include <jfc/lua/reference.h>
#include <jfc/lua/data_table.h>
#include <jfc/lua/typed_call.h>
#include <jfc/lua/types.h>
#include <jfc/lua/userdata.h>

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
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
        /// \warning includes debug, whose getregistry() function can be used to access every other environment 
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
        void register_function(const path &aName, closure_type aClosure);

        /// \brief registers a function that says what it takes in its own signature
        ///
        /// Its arguments are read from the script as it asks, and a call that does not fit is an error naming the
        /// function and the argument. A whole number must fit the integer type asked for. A std::optional may be left
        /// out or nil. No more arguments may be given than it takes. A std::string_view or a table_view is valid for the
        /// call, and a function_reference or a reference can be kept. A trailing value_list_type takes every argument
        /// left. Results may be a std::tuple for several, and a std::optional is nil when empty.
        template <typename function_type>
        requires (!std::is_convertible_v<function_type, closure_type>)
        void register_function(const path &aName, function_type &&aFunction)
            { _register(aName, detail::make_typed_closure(std::forward<function_type>(aFunction)), false); }

        /// \brief registers a closure that suspends the coroutine calling it
        void register_yielding_function(const path &aName, closure_type aClosure);

        /// \brief registers a function like register_function's, that suspends the coroutine calling it
        template <typename function_type>
        requires (!std::is_convertible_v<function_type, closure_type>)
        void register_yielding_function(const path &aName, function_type &&aFunction)
            { _register(aName, detail::make_typed_closure(std::forward<function_type>(aFunction)), true); }

        /// \brief the function at aFunction, ready to run as a coroutine against this environment
        [[nodiscard]] std::optional<coroutine> make_coroutine(const path &aFunction);

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

        //! registers aClosure at aName, yielding its results if aYields. \see register_function
        void _register(const path &aName, detail::typed_closure_type aClosure, bool aYields);

        std::optional<std::size_t> m_InstructionBudget;
    };
}

#endif
