// © Joseph Cameron - MIT License

#ifndef JFC_LUA_COMPILES_KEPT_H
#define JFC_LUA_COMPILES_KEPT_H

#include <cstddef>

/// \brief what `backend::compile` keeps: a source compiled again is given what it compiled to before, until the sources
/// kept and what they compiled to pass MOST_KEPT_BYTES, when all of them are let go. Lua 5.1 compiles nothing, and keeps
/// nothing
namespace jfc::lua::backend {
    inline constexpr std::size_t MOST_KEPT_BYTES = std::size_t{32} << 20;

    struct kept_compiles final {
        /// \brief sources kept now
        std::size_t sources = 0;

        /// \brief bytes kept now, of those sources and what they compiled to
        std::size_t bytes = 0;

        /// \brief compiles done since the program began, a source found kept not among them
        std::size_t compiled = 0;
    };

    [[nodiscard]] kept_compiles compiles_kept();
}

#endif
