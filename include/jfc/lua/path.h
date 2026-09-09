// © Joseph Cameron - All Rights Reserved

#ifndef JFC_LUA_PATH_H
#define JFC_LUA_PATH_H

#include <jfc/lua/key.h>
#include <jfc/lua/types.h>

#include <cstddef>
#include <initializer_list>
#include <iosfwd>
#include <string>
#include <vector>

namespace jfc::lua
{
    /// \brief path lets you access nested values in a lua environment 
    /// from C++ using the same convention that lua does
    /// e.g: `list_of_names[1]`
    /// e.g: `player.hitbox.bounds.x`
    class path final
    {
    public:
        /// \brief the keys written out, which is the form with no parsing and no ambiguity
        path(std::initializer_list<key> aSegments);

        /// \brief the keys written out, for a path assembled a segment at a time
        explicit path(std::vector<key> aSegments);

        /// \brief parse the string form
        /// \warning throws exception if it does not parse
        path(const std::string &aPathString);

        /// \brief parse the string form
        path(const char *const aPathString);

        [[nodiscard]] std::size_t size() const;
        [[nodiscard]] bool empty() const;

        /// \brief the key at aIndex, which is not bounds checked
        [[nodiscard]] const key &operator[](const std::size_t aIndex) const;

        [[nodiscard]] const std::vector<key> &segments() const;

        [[nodiscard]] bool operator==(const path &a) const;

        /// \brief write the string form
        friend std::ostream &operator<<(std::ostream &stream, const path &a);

    private:
        std::vector<key> m_Segments;
    };
}

#endif
