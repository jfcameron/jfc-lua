// © Joseph Cameron - MIT License

#ifndef JFC_LUA_SIGNATURE_H
#define JFC_LUA_SIGNATURE_H

#include <map>
#include <string>
#include <vector>

namespace jfc::lua {
    /// \brief what a registered function takes and gives, as its c++ signature says
    struct signature final {
        /// \brief one argument or result
        struct value final {
            /// \brief `boolean`, `integer`, `number`, `string`, `table`, `function`, `value` (any lua value),
            /// `userdata`, `nil`, or the name a bound type was registered with
            std::string type;

            /// \brief an argument that may be left out or nil, or a result that may be nil
            bool optional{false};

            [[nodiscard]] bool operator==(const value &) const = default;
        };

        std::vector<value> arguments;

        /// \brief takes every argument after these, of any type
        bool takes_rest{false};

        std::vector<value> results;

        /// \brief gives any number of results after these
        bool gives_rest{false};

        /// \brief suspends the coroutine calling it
        bool yields{false};

        [[nodiscard]] bool operator==(const signature &) const = default;
    };

    /// \brief where a function written in lua was written: its script's name, as `run` was given it, and its line
    struct definition final {
        std::string source;

        int line{0};

        [[nodiscard]] bool operator==(const definition &) const = default;
    };

    /// \brief what an interpreter has had registered: each named environment's functions, and each bound type's methods
    struct catalogue final {
        /// \brief by the environment's name (environment_policy::NAME), then by path: `world.give_item`
        std::map<std::string, std::map<std::string, signature>> environments;

        /// \brief by the name each type was registered with, then by method
        std::map<std::string, std::map<std::string, signature>> types;

        /// \brief the functions written in lua that each named environment reaches from its globals, through tables
        /// to a few deep, by the environment's name, then by path: `ui.window`. Read from the latest environment made
        /// with each name, where it still lives
        std::map<std::string, std::map<std::string, definition>> defined;
    };
}

#endif
