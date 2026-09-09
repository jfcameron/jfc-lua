// © Joseph Cameron - All Rights Reserved

#ifndef JFC_LUA_INTERPRETER_H
#define JFC_LUA_INTERPRETER_H

#include <jfc/lua/environment.h>
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
#include <vector>

namespace jfc::lua {
    /// \brief user-definable configuration for an interpreter
    struct interpreter_policy final
    {
        /// \brief bytes of lua heap this interpreter may hold before allocation starts failing.
        /// Zero is unbounded. This is a rule for the entire interpreter (lua state), so the limit is
        /// shared by every environment created by that interpreter
        const std::size_t MEMORY_BUDGET_IN_BYTES{0};

        /// \brief bytes one call to `run` may *add* to the heap before allocation starts failing
        /// Zero is unbounded. Where MEMORY_BUDGET_IN_BYTES bounds the whole interpreter for its whole
        /// life, this bounds one script's usage
        const std::size_t MEMORY_GROWTH_BUDGET_IN_BYTES_PER_RUN{0};

        /// \brief vm instructions one call to `run` may execute before it is interrupted
        /// Zero is unbounded.
        const std::size_t INSTRUCTION_BUDGET{0};

        /// \brief what to do when a script reaches INSTRUCTION_BUDGET
        const std::function<bool(const std::size_t aInstructionsExecuted)>
            ON_INSTRUCTION_BUDGET_EXHAUSTED{};
    };

    /// \brief a lua interpreter
    ///
    /// Owns the lua state and makes the environments that run on it
    class interpreter final
    {
    public:
        using error_type = environment::error_type;
        using closure_type = environment::closure_type;

        /// \brief make an environment with a globals table of its own
        [[nodiscard]] environment_shared_ptr_type make_environment(environment_policy aPolicy = {});

        /// \brief writes a [string, boolean] to the lua context
        void write(const path &aPath, const bool aValue);
        /// \brief writes a [string, number] to the lua context
        void write(const path &aPath, const double aValue);
        /// \brief writes a [string, string] to the lua context
        void write(const path &aPath, const std::string &aValue);
        /// \brief writes a [string, string] to the lua context
        void write(const path &aPath, const std::string::value_type *aValue);
        /// \brief writes a [string, table] to the lua context
        void write(const path &aPath, const data_table &);


        /// \brief reads a dense array: a table whose keys are exactly 1..n, all holding value_type
        template <typename value_type>
        [[nodiscard]] std::optional<std::vector<value_type>> read_vector(const path &aPath) const;

        /// \brief writes a dense array, as keys 1..n
        void write(const path &aPath, const std::vector<double> &aValue);
        /// \brief writes a dense array, as keys 1..n
        void write(const path &aPath, const std::vector<bool> &aValue);
        /// \brief writes a dense array, as keys 1..n
        void write(const path &aPath, const std::vector<std::string> &aValue);


        /// \brief writes a c++ object for a script to hold
        ///
        /// Both sides own it: the object lives until neither c++ nor lua wants it, so neither can leave
        /// the other with a dangling pointer. \see userdata
        void write(const path &aPath, const userdata &aValue);

        /// \brief writes a c++ object for a script to hold
        template <typename object_type>
        void write(const path &aPath, const std::shared_ptr<object_type> &aObject)
            { write(aPath, userdata::make(aObject)); }


        /// \brief takes a handle to the lua value at aPath
        [[nodiscard]] std::optional<reference> read_reference(const path &aPath) const;

        /// \brief reads a bound c++ object, whatever it holds
        [[nodiscard]] std::optional<userdata> read_userdata(const path &aPath) const;

        /// \brief reads a bound c++ object, if it is an object_type
        template <typename object_type>
        [[nodiscard]] std::optional<std::shared_ptr<object_type>> read_userdata(
            const path &aPath) const
        {
            const auto held = _read_userdata(aPath);

            if (!held) return {};

            auto object = held->get<object_type>();

            if (!object) return {};

            return object;
        }

        /// \brief reads a boolean from the lua context
        [[nodiscard]] std::optional<bool> read_boolean(const path &aPath) const;
        /// \brief reads a number from the lua context
        [[nodiscard]] std::optional<double> read_number(const path &aPath) const;
        /// \brief reads a string from the lua context
        [[nodiscard]] std::optional<std::string> read_string(const path &aPath) const;
        /// \brief reads a table from the lua context
        [[nodiscard]] std::optional<data_table> read_data_table(const path &aPath,
            const unsupported aPolicy = unsupported::reject) const;


        /// \brief call a lua function
        [[nodiscard]] error_type call(const path &aPath, const value_list_type &aArguments,
            value_list_type &aResults);

        /// \brief call a lua function, discarding whatever it returns
        [[nodiscard]] error_type call(const path &aPath, const value_list_type &aArguments = {});

        /// \brief registers a closure (c++ lambda with captured data)
        void register_function(const path &aName, closure_type a);

        /// \brief checks for basic synatx errors. 
        [[nodiscard]] error_type validate_syntax(const std::string &aLuaScript) const;
        
        /// \brief run a script, returns an error if something went wrong
        [[nodiscard]] error_type run(const std::string &aLuaScript);
        
        /// \brief construct an interpreter
        ///
        /// \warning throws if the lua state cannot be created
        interpreter(interpreter_policy aPolicy = {});

    private:
       [[nodiscard]] std::optional<userdata> _read_userdata(const path &aPath) const;

        std::shared_ptr<interpreter_limits> m_pLimits;
        std::shared_ptr<lua_State> m_pState;
        int m_StandardLibraryReference;
        int m_SafeLibraryReference;
        environment_shared_ptr_type m_pGlobals;
    };
}

#endif
