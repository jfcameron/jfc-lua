// © Joseph Cameron - All Rights Reserved

#ifndef JFC_LUA_USERDATA_H
#define JFC_LUA_USERDATA_H

#include <jfc/lua_types.h>

#include <memory>
#include <type_traits>

namespace jfc::lua
{
    /// \brief gives every bound type an unique identity 
    template <typename object_type>
    struct userdata_tag final { static const char id; };

    template <typename object_type>
    const char userdata_tag<object_type>::id = 0;

    /// \brief a c++ object a script may hold
    class userdata final
    {
    public:
        /// \brief bind an object, remembering what it is
        template <typename object_type>
        [[nodiscard]] static userdata make(std::shared_ptr<object_type> aObject)
        {
            userdata out;

            out.m_Object = std::move(aObject);
            out.m_TypeKey = &userdata_tag<std::remove_cv_t<object_type>>::id;

            return out;
        }

        /// \brief gets the object, if it is really an object_type
        template <typename object_type>
        [[nodiscard]] std::shared_ptr<object_type> get() const
        {
            if (!holds<object_type>()) return {};

            return std::static_pointer_cast<object_type>(m_Object);
        }

        /// \brief whether this holds an object_type
        template <typename object_type>
        [[nodiscard]] bool holds() const
        {
            return m_TypeKey == &userdata_tag<std::remove_cv_t<object_type>>::id;
        }

        /// \brief whether this holds anything at all
        [[nodiscard]] bool empty() const { return !m_Object; }

        /// \brief how many owners the bound object has, counting this one
        [[nodiscard]] long use_count() const { return m_Object.use_count(); }

        /// \brief an identity for the bound type, comparable but not readable
        [[nodiscard]] const void *type_key() const { return m_TypeKey; }

        /// \brief whether both refer to the same object
        [[nodiscard]] bool operator==(const userdata &a) const
            { return m_Object == a.m_Object && m_TypeKey == a.m_TypeKey; }

        [[nodiscard]] bool operator!=(const userdata &a) const { return !(*this == a); }

    private:
        //! type erased, and keeping the object alive: a shared_ptr<void> made from a shared_ptr<T>
        //! carries T's deleter, so the right destructor runs whoever lets go last
        std::shared_ptr<void> m_Object;

        //! what it really is, so get() can refuse
        const void *m_TypeKey = nullptr;
    };
}

#endif
