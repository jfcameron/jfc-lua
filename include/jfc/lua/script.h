// © Joseph Cameron - MIT License

#ifndef JFC_LUA_SCRIPT_H
#define JFC_LUA_SCRIPT_H

#include <string>
#include <string_view>

namespace jfc::lua
{
    /// \brief a script compiled once, to run in any environment of any interpreter, as often as wanted: Luau's
    /// bytecode, or under Lua 5.1 its source
    class script final
    {
    public:
        /// \brief aSource compiled. One that does not compile gives its error where it is run.
        ///
        /// \param aName what errors call this script. \see environment::run
        explicit script(const std::string &aSource, std::string_view aName = {});

    private:
        friend class environment;

        std::string m_Compiled;
        std::string m_ChunkName;
    };
}

#endif
