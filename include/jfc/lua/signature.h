// © Joseph Cameron - MIT License

#ifndef JFC_LUA_SIGNATURE_H
#define JFC_LUA_SIGNATURE_H

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace jfc::lua {
    struct shape;

    /// \brief what a registered function takes and gives, as its c++ signature says
    struct signature final {
        /// \brief one argument or result
        struct value final {
            /// \brief `boolean`, `integer`, `number`, `string`, `table`, `function`, `value` (any lua value),
            /// `userdata`, `nil`, the name a bound type was registered with, or the name of a shape
            std::string type;

            /// \brief an argument that may be left out or nil, or a result that may be nil
            bool optional{false};

            /// \brief a list of the type, from 1
            bool list{false};

            /// \brief a table from keys of this type to the type: `string` for a map of names; empty for none
            std::string key{};

            /// \brief the fields a table of the type has, where its type names a shape
            std::shared_ptr<const jfc::lua::shape> shape{};

            /// \brief equal where everything is, a shape by what it says rather than where it is
            [[nodiscard]] bool operator==(const value &aOther) const;
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

    /// \brief **what a table holds, by name**: its fields, each with its type, as Luau's checker is told it. A shaped
    /// table is built against one (shaped<S>, shape.h), so what a function gives and what it says it gives agree
    struct shape final {
        /// \brief one of its fields
        struct field final {
            std::string name;

            /// \brief its type: optional where the table may leave it out
            signature::value type;

            /// \brief what it is, written over it
            std::string words{};

            [[nodiscard]] bool operator==(const field &) const = default;
        };

        /// \brief what scripts call it: `thing`
        std::string name;

        std::vector<field> fields;

        /// \brief what it is, written over it
        std::string words{};

        [[nodiscard]] bool operator==(const shape &) const = default;
    };

    inline bool signature::value::operator==(const value &aOther) const {
        if (type != aOther.type || optional != aOther.optional || list != aOther.list || key != aOther.key) return false;

        if (shape == aOther.shape) return true;

        return shape && aOther.shape && *shape == *aOther.shape;
    }

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
