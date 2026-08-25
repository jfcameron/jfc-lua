// © Joseph Cameron - All Rights Reserved

#ifndef JFC_LUA_REFERENCE_H
#define JFC_LUA_REFERENCE_H

#include <jfc/lua/data_table.h>
#include <jfc/lua/path.h>
#include <jfc/lua/types.h>
#include <jfc/lua/userdata.h>

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace jfc::lua
{
    /// \brief a list of value types that can cross the c++/lua barrier
    using value_list_type = std::vector<std::variant<
        double, // lua's number type
        bool, // lua's boolean type
        std::string, // lua's string type
        decltype(nullptr), // lua's nil type
        data_table, // a restricted form of lua's table type (see data_table.h)
        userdata // lua's userdata type
    >>;

    /// \brief a handle to a value that lives in lua
    class reference
    {
    public:
        /// \brief methods that can fail return this \see jfc::lua::error_type
        using error_type = jfc::lua::error_type;

        /// \brief what lua calls the referenced value type: "table", "function", "number" and so on
        [[nodiscard]] std::string type_name() const;

        [[nodiscard]] bool is_table() const;
        [[nodiscard]] bool is_function() const;

        /// \brief call the referenced value
        [[nodiscard]] error_type call(const value_list_type &aArguments, value_list_type &aResults);

        /// \brief call the referenced value, discarding whatever it returns
        [[nodiscard]] error_type call(const value_list_type &aArguments = {});

        [[nodiscard]] bool operator==(const reference &a) const;

        /// \brief writes a [string, boolean] through this handle
        void write(const path &aPath, const bool aValue);
        /// \brief writes a [string, number] through this handle
        void write(const path &aPath, const double aValue);
        /// \brief writes a [string, string] through this handle
        void write(const path &aPath, const std::string &aValue);
        /// \brief writes a [string, string] through this handle
        void write(const path &aPath, const std::string::value_type *aValue);
        /// \brief writes a [string, table] through this handle
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

        /// \brief reads a boolean through this handle
        [[nodiscard]] std::optional<bool> read_boolean(const path &aPath) const;
        /// \brief reads a number through this handle
        [[nodiscard]] std::optional<double> read_number(const path &aPath) const;
        /// \brief reads a string through this handle
        [[nodiscard]] std::optional<std::string> read_string(const path &aPath) const;
        /// \brief reads a table through this handle
        [[nodiscard]] std::optional<data_table> read_data_table(const path &aPath,
            const unsupported aPolicy = unsupported::reject) const;

        virtual ~reference();

    protected:
        /// \brief anchor the value on top of the stack, popping it
        reference(std::shared_ptr<lua_State> aState, const int aRegistryReference);

        /// \brief the state the referenced value lives in
        [[nodiscard]] lua_State *state() const;

        /// \brief the registry slot the value is anchored in
        [[nodiscard]] int registry_reference() const;

        /// \brief the bound object at aPath
        [[nodiscard]] std::optional<userdata> _read_userdata(const path &aPath) const;

    private:
        friend class environment;
        friend class interpreter;

        std::shared_ptr<lua_State> m_pState;

        std::shared_ptr<const int> m_pReference;
    };
}

#endif
