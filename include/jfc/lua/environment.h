// © Joseph Cameron - MIT License

#ifndef JFC_LUA_ENVIRONMENT_H
#define JFC_LUA_ENVIRONMENT_H

#include <jfc/lua/coroutine.h>
#include <jfc/lua/path.h>
#include <jfc/lua/reference.h>
#include <jfc/lua/script.h>
#include <jfc/lua/data_table.h>
#include <jfc/lua/typed_call.h>
#include <jfc/lua/types.h>
#include <jfc/lua/userdata.h>

#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace jfc::lua
{
    /// \brief how much of the lua standard library an environment can reach
    enum class standard_library
    {
        /// \brief computation only: the dialect's libraries that cannot reach outside the vm -- string, table, math,
        /// coroutine, and LuaJIT's bit, or Luau's bit32, utf8, buffer and vector -- the clock parts of os, and the base
        /// functions that cannot reach outside it
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

        /// \brief how long one call to this environment's `run` may take
        ///
        /// Empty defers to the interpreter's figure, which is the usual case. \see interpreter_policy::TIME_BUDGET
        const std::optional<std::chrono::nanoseconds> TIME_BUDGET{};

        /// \brief what kind of environment this is, under which interpreter::registered lists what is registered here
        ///
        /// Empty lists nothing. Environments of one kind share a name, and are listed together.
        const std::string NAME{};
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

        /// \brief call a lua function without going through value lists \see reference::call
        template <typename result_type = void, typename... argument_types>
        [[nodiscard]] call_outcome<result_type> call(const path &aPath, const argument_types &...aArguments) {
            return detail::typed_outcome<result_type>([&](const detail::stack_work &aPush, const detail::stack_work &aRead)
                { return _call(aPath, aPush, aRead); }, aArguments...);
        }

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
        void register_function(const path &aName, function_type &&aFunction) {
            _register(aName, detail::make_typed_closure(std::forward<function_type>(aFunction)),
                detail::signature_maker_of<function_type>(), false);
        }

        /// \brief registers a closure that suspends the coroutine calling it
        void register_yielding_function(const path &aName, closure_type aClosure);

        /// \brief registers a function like register_function's, that suspends the coroutine calling it
        template <typename function_type>
        requires (!std::is_convertible_v<function_type, closure_type>)
        void register_yielding_function(const path &aName, function_type &&aFunction) {
            _register(aName, detail::make_typed_closure(std::forward<function_type>(aFunction)),
                detail::signature_maker_of<function_type>(), true);
        }

        /// \brief the function at aFunction, ready to run as a coroutine against this environment
        [[nodiscard]] std::optional<coroutine> make_coroutine(const path &aFunction);

        /// \brief checks for basic syntax errors.
        ///
        /// This is not required to called before running a script, a typical use case
        /// would be in a script editor, in order to catch syntax errors in a script
        /// that is actively being developed.
        ///
        /// \param aName the script's name in errors. \see run
        [[nodiscard]] error_type validate_syntax(const std::string &aLuaScript, std::string_view aName = {}) const;

        /// \brief run a script here, returns an error if something went wrong
        ///
        /// \param aName what errors call this script, and the functions it defines, for as long as they live: usually its
        /// file, as in `rules.lua:3: the bucket leaks`. Empty leaves lua to quote the script.
        [[nodiscard]] error_type run(const std::string &aLuaScript, std::string_view aName = {});

        /// \brief run a compiled script here, as run runs its source, without compiling it again
        [[nodiscard]] error_type run(const script &aScript);

        environment(const environment &) = delete;
        environment &operator=(const environment &) = delete;

    private:
        friend class interpreter;

        environment(std::shared_ptr<lua_State> aState, const int aRootReference,
            const std::optional<std::chrono::nanoseconds> aTimeBudget, std::string aName);

        //! registers aClosure at aName, yielding its results if aYields, kept in the catalogue by aDescribe where this
        //! environment has a name. \see register_function
        void _register(const path &aName, detail::typed_closure_type aClosure, detail::signature_maker aDescribe,
            bool aYields);

        //! a typed call of what is at aPath
        [[nodiscard]] error_type _call(const path &aPath, const detail::stack_work &aPush, const detail::stack_work &aRead);

        std::optional<std::chrono::nanoseconds> m_TimeBudget;

        std::string m_Name;
    };
}

#endif
