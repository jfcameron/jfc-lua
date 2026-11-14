// © Joseph Cameron - MIT License

#include <jfc/lua/script.h>

#include <jfc/lua/backend.h>

namespace jfc::lua {
    script::script(const std::string &aSource, const std::string_view aName)
        : m_Compiled(backend::compile(aSource))
        , m_ChunkName(backend::chunk_name(aSource, aName)) {}
}
