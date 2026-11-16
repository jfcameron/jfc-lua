// © Joseph Cameron - MIT License

#ifndef JFC_LUA_CHECKER_H
#define JFC_LUA_CHECKER_H

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace jfc::lua {
    /// \brief Luau's type checker, over scripts named as its caller names them: each checked against the declarations
    /// of its environment, what it requires followed to the file it names. Built with the luau backend alone, as a
    /// target of its own, `jfclua_checker`: Luau's analysis is large, and only what checks scripts needs it
    class checker final {
    public:
        /// \brief something a script has wrong: where, and what
        struct problem final {
            std::string file{};
            int line{0};
            int column{0};
            std::string message{};
        };

        /// \brief something Luau offers to be typed at a place in a script
        struct suggestion final {
            /// \brief a table's field, a name in scope, a keyword, or a type's name where a type is written
            enum class kind : std::uint8_t { property, binding, keyword, type };

            std::string name{};

            kind type{kind::binding};

            /// \brief its type as Luau writes it, `(number) -> string`; empty for a keyword
            std::string annotation{};

            /// \brief a function called on its table, with `:`
            bool method{false};
        };

        /// \brief a file's source, by its name; nothing where there is no such file
        using reader = std::function<std::optional<std::string>(const std::string &aFile)>;

        /// \brief the name of the file a script requires, from the name of the file requiring it and what it gives
        /// require; nothing where it names none
        using resolver = std::function<std::optional<std::string>(const std::string &aFrom, const std::string &aName)>;

        /// \brief the environment a file is checked in, one the declarations name; empty for the standard library alone
        using placer = std::function<std::string(const std::string &aFile)>;

        /// \brief aDeclarations by the environment each is, as luau_declarations writes them. Throws where one does not
        /// load
        checker(const std::map<std::string, std::string> &aDeclarations, reader aRead, resolver aResolve,
            placer aPlace);

        ~checker();

        checker(const checker &) = delete;
        checker &operator=(const checker &) = delete;

        /// \brief what Luau finds wrong in aFile, checked with what it requires
        [[nodiscard]] std::vector<problem> check(const std::string &aFile);

        /// \brief aFile read again at the next check, and what requires it checked again: it changed. A file is read
        /// once, and kept, until it is said to have
        void changed(const std::string &aFile);

        /// \brief what Luau offers to be typed at aLine and aColumn of aFile, both from 1, by name: aFile checked
        /// held to its types, as an editor's completion is. Names in a string, a require's path and the functions
        /// Luau would write out are left out
        [[nodiscard]] std::vector<suggestion> complete(const std::string &aFile, int aLine, int aColumn);

        /// \brief whether aSource asks to be held to its types: `--!strict` among its first comments
        [[nodiscard]] static bool strict(const std::string &aSource);

    private:
        struct impl;

        std::unique_ptr<impl> m_pImpl;
    };
}

#endif
