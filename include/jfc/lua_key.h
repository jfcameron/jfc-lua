// © Joseph Cameron - All Rights Reserved

#ifndef JFC_LUA_KEY_H
#define JFC_LUA_KEY_H

#include <jfc/lua_types.h>

#include <string>
#include <type_traits>
#include <variant>

namespace jfc::lua
{
    /// \brief what a lua value is indexed by: a number, a boolean, or a string
    class key final
    {
    public:
        /// \brief a number key
        template <typename number_type, typename = std::enable_if_t<
            std::is_arithmetic_v<number_type> && !std::is_same_v<std::decay_t<number_type>, bool>>>
        key(const number_type aNumber)
        : m_Value(static_cast<double>(aNumber))
        {}

        /// \brief a boolean key
        key(const bool aBoolean)
        : m_Value(aBoolean)
        {}

        /// \brief a string key
        key(std::string aName)
        : m_Value(std::move(aName))
        {}

        /// \brief a string key
        key(const char *const aName)
        : m_Value(std::string(aName))
        {}

        [[nodiscard]] bool is_number() const { return std::holds_alternative<double>(m_Value); }
        [[nodiscard]] bool is_boolean() const { return std::holds_alternative<bool>(m_Value); }
        [[nodiscard]] bool is_string() const { return std::holds_alternative<std::string>(m_Value); }

        /// \brief the key as stored, for visiting
        [[nodiscard]] const key_type &value() const { return m_Value; }

        [[nodiscard]] bool operator==(const key &a) const { return m_Value == a.m_Value; }
        [[nodiscard]] bool operator!=(const key &a) const { return m_Value != a.m_Value; }

    private:
        key_type m_Value;
    };
}

#endif
