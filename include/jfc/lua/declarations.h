// © Joseph Cameron - MIT License

#ifndef JFC_LUA_DECLARATIONS_H
#define JFC_LUA_DECLARATIONS_H

#include <jfc/lua/signature.h>

#include <map>
#include <string>
#include <vector>

namespace jfc::lua {
    /// \brief a function as Luau's checker is told of it
    struct declared final {
        /// \brief where scripts reach it, `world.give_item`; a method's name alone
        std::string path{};

        /// \brief what it takes and gives
        jfc::lua::signature signature{};

        /// \brief its parameters' names, as many as are known
        std::vector<std::string> parameters{};

        /// \brief what it does, written over it
        std::string words{};
    };

    /// \brief a definitions file Luau's checker loads: each type a `declare extern type` with its methods, then each
    /// function under its path, a table of tables where the path has dots.
    ///
    /// A value list is `...any`; a table `{ [any]: any }`, and a function `(...any) -> ...any`, until something says
    /// more. A name that is not one in Luau is written `["name"]`.
    [[nodiscard]] std::string luau_declarations(const std::vector<declared> &aFunctions,
        const std::map<std::string, std::vector<declared>> &aTypes);
}

#endif
