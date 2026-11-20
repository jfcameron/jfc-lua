// © Joseph Cameron - MIT License

#ifndef JFC_LUA_SHAPE_H
#define JFC_LUA_SHAPE_H

#include <jfc/lua/data_table.h>
#include <jfc/lua/signature.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace jfc::lua {
    /// \brief **a table a function gives, built against a shape**, so what it says it gives is what it gives: a field
    /// the shape has not is refused as it is set, and a table missing one the shape does not let be left out is refused
    /// as it is handed over. A signature names it by its shape (`thing`), and Luau's declarations write the shape out.
    ///
    /// shape_type says what it is: `static const jfc::lua::shape &describe()`, made once.
    template <typename shape_type>
    class shaped final {
    public:
        shaped() = default;

        /// \brief aName set to aValue: a field of the shape, of its kind -- a number, a boolean, a string, or a table
        /// for a table, a list, a map or a shape. A std::logic_error naming the shape and the field otherwise
        shaped &set(const std::string_view aName, data_table::value_type aValue) {
            const auto *const pField = field_named(aName);

            if (!pField) throw std::logic_error(describe().name + " has no field " + std::string(aName));

            if (!fits(pField->type, aValue))
                throw std::logic_error(describe().name + "'s " + std::string(aName) + " is not that kind of value");

            m_Table.set(std::string(aName), std::move(aValue));

            return *this;
        }

        shaped &set(const std::string_view aName, const double aValue) {
            return set(aName, data_table::value_type(aValue));
        }

        shaped &set(const std::string_view aName, const bool aValue) {
            return set(aName, data_table::value_type(aValue));
        }

        shaped &set(const std::string_view aName, std::string aValue) {
            return set(aName, data_table::value_type(std::move(aValue)));
        }

        shaped &set(const std::string_view aName, const char *const aValue) { return set(aName, std::string(aValue)); }

        shaped &set(const std::string_view aName, const data_table &aValue) {
            return set(aName, data_table::value_type(std::make_shared<const data_table>(aValue)));
        }

        template <typename other_type>
        shaped &set(const std::string_view aName, const shaped<other_type> &aValue) {
            return set(aName, aValue.table());
        }

        /// \brief the table, handed over: a std::logic_error naming the first field the shape does not let be left out
        /// that is not set
        [[nodiscard]] const data_table &table() const {
            for (const auto &each : describe().fields)
                if (!each.type.optional && !m_Table.contains(each.name))
                    throw std::logic_error(describe().name + " is missing its " + each.name);

            return m_Table;
        }

        /// \brief the shape it is built against
        [[nodiscard]] static const shape &describe() { return shape_type::describe(); }

    private:
        data_table m_Table;

        [[nodiscard]] static const shape::field *field_named(const std::string_view aName) {
            for (const auto &each : describe().fields)
                if (each.name == aName) return &each;

            return nullptr;
        }

        [[nodiscard]] static bool fits(const signature::value &aType, const data_table::value_type &aValue) {
            if (aType.list || !aType.key.empty() || aType.shape || aType.type == "table")
                return std::holds_alternative<std::shared_ptr<const data_table>>(aValue);

            if (aType.type == "number" || aType.type == "integer") return std::holds_alternative<double>(aValue);
            if (aType.type == "boolean") return std::holds_alternative<bool>(aValue);
            if (aType.type == "string") return std::holds_alternative<std::string>(aValue);

            return true;
        }
    };

    template <typename> struct is_shaped : std::false_type {};
    template <typename shape_type> struct is_shaped<shaped<shape_type>> : std::true_type {};
}

#endif
