// © Joseph Cameron - All Rights Reserved

#include <jfc/lua/internal.h>

#include <jfc/lua/exception.h>
#include <jfc/lua/environment.h>
#include <jfc/lua/interpreter.h>
#include <jfc/lua/data_table.h>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
#include <utility>

using jfc::lua::interpreter_limits;

namespace {
    int _reject_standard_library_write(lua_State *const L) {
        return luaL_error(L, "the standard library is shared by every environment and cannot be "
            "modified from within one");
    }

    void _make_readonly(lua_State *const L) {
        lua_newtable(L);

        lua_newtable(L);

        lua_pushvalue(L, -3);
        lua_setfield(L, -2, "__index");

        lua_pushcfunction(L, _reject_standard_library_write);
        lua_setfield(L, -2, "__newindex");

        lua_pushboolean(L, 0);
        lua_setfield(L, -2, "__metatable");

        lua_setmetatable(L, -2);

        lua_remove(L, -2);
    }

    constexpr int INSTRUCTION_CHECK_INTERVAL = 1000;

    void *_capped_alloc(void *ud, void *p, std::size_t aOldSize, std::size_t aNewSize) {
        auto *const limits = static_cast<interpreter_limits *>(ud);
        const std::size_t held = p ? aOldSize : 0;

        if (aNewSize == 0) {
            limits->used -= held;
            std::free(p);
            return nullptr;
        }

        const std::size_t prospective = limits->used - held + aNewSize;

        if (limits->cap && prospective > limits->cap) return nullptr;

        if (limits->growthCap && limits->depth > 0 && prospective > limits->usedAtRunStart
            && prospective - limits->usedAtRunStart > limits->growthCap) return nullptr;

        void *const allocated = std::realloc(p, aNewSize);

        if (allocated) limits->used = prospective;

        return allocated;
    }

    void _instruction_hook(lua_State *L, lua_Debug *) {
        void *ud = nullptr;

        lua_getallocf(L, &ud);

        auto *const limits = static_cast<interpreter_limits *>(ud);

        if (!limits || !limits->instructionBudget) return;

        if (limits->aborted) luaL_error(L, "script exceeded its instruction budget");

        limits->instructionsSeen += INSTRUCTION_CHECK_INTERVAL;

        if (limits->instructionsSeen <= limits->instructionBudget) return;

        bool carryOn = false;

        if (limits->onExhausted) {
            // A c++ exception cannot cross the longjmp lua raises errors with, so one thrown here
            // would unwind through the vm and leave it in pieces, so its treated as "stop".
            try { carryOn = limits->onExhausted(limits->instructionsSeen); }
            catch (...) { carryOn = false; }
        }

        if (carryOn) {
            limits->instructionsSeen = 0;

            return;
        }

        limits->aborted = true;

        luaL_error(L, "script exceeded its instruction budget");
    }

    void _ensure_instruction_hook(lua_State *const L, interpreter_limits *const aLimits) {
        if (!aLimits || aLimits->hookInstalled) return;

        lua_sethook(L, _instruction_hook, LUA_MASKCOUNT, INSTRUCTION_CHECK_INTERVAL);

        aLimits->hookInstalled = true;
    }

    //! base values that cannot reach outside the vm
    constexpr const char *SAFE_VALUES[] = {
        "_VERSION", "assert", "error", "getmetatable", "ipairs", "next", "pairs", "pcall", "print",
        "rawequal", "rawget", "rawset", "select", "setmetatable", "tonumber", "tostring", "type",
        "unpack", "xpcall"
    };

    //! libraries with no route to the host, taken whole
    constexpr const char *SAFE_LIBRARIES[] = { "coroutine", "math", "table", "bit" };

    //! string, less the one entry that produces loadable bytecode
    constexpr const char *UNSAFE_STRING_FIELDS[] = { "dump" };

    //! os is not safe whole; these are the parts that only report the time
    constexpr const char *SAFE_OS_FIELDS[] = { "clock", "date", "difftime", "time" };

    /// \brief copy the named fields of the table at aSourceIndex into a fresh table on top of the stack
    void _push_filtered_copy(lua_State *const L, const int aSourceIndex,
        const char *const *const aFields, const std::size_t aCount) {
        const int source = aSourceIndex < 0 ? lua_gettop(L) + aSourceIndex + 1 : aSourceIndex;

        lua_newtable(L);

        for (std::size_t i = 0; i < aCount; ++i) {
            lua_getfield(L, source, aFields[i]);

            if (lua_isnil(L, -1)) {
                lua_pop(L, 1);

                continue;
            }

            lua_setfield(L, -2, aFields[i]);
        }
    }

    [[nodiscard]] int _snapshot_safe_globals(lua_State *const L) {
        lua_newtable(L);

        for (const auto *const name : SAFE_VALUES) {
            lua_getglobal(L, name);

            if (lua_isnil(L, -1)) {
                lua_pop(L, 1);
                continue;
            }

            lua_setfield(L, -2, name);
        }

        for (const auto *const name : SAFE_LIBRARIES) {
            lua_getglobal(L, name);

            if (!lua_istable(L, -1)) {
                lua_pop(L, 1);
                continue;
            }

            _make_readonly(L);
            lua_setfield(L, -2, name);
        }

        lua_getglobal(L, "string");
        if (lua_istable(L, -1)) {
            lua_newtable(L);

            lua_pushnil(L);
            while (lua_next(L, -3)) {
                bool omitted = false;

                for (const auto *const field : UNSAFE_STRING_FIELDS)
                    if (lua_type(L, -2) == LUA_TSTRING && std::string(field) == lua_tostring(L, -2))
                        omitted = true;

                if (omitted) lua_pop(L, 1);
                else {
                    lua_pushvalue(L, -2); // key
                    lua_pushvalue(L, -2); // value

                    lua_rawset(L, -5);

                    lua_pop(L, 1);
                }
            }

            _make_readonly(L);
            lua_setfield(L, -3, "string");
        }
        lua_pop(L, 1);

        lua_getglobal(L, "os");
        if (lua_istable(L, -1)) {
            _push_filtered_copy(L, -1, SAFE_OS_FIELDS,
                sizeof(SAFE_OS_FIELDS) / sizeof(*SAFE_OS_FIELDS));

            _make_readonly(L);
            lua_setfield(L, -3, "os");
        }
        lua_pop(L, 1);

        return luaL_ref(L, LUA_REGISTRYINDEX);
    }

    [[nodiscard]] int _snapshot_globals(lua_State *const L) {
        lua_newtable(L);

        lua_pushnil(L);
        while (lua_next(L, LUA_GLOBALSINDEX)) {
            if (lua_type(L, -2) == LUA_TSTRING && std::string("_G") == lua_tostring(L, -2)) {
                lua_pop(L, 1);
                continue;
            }

            lua_pushvalue(L, -2); // key
            lua_pushvalue(L, -2); // value

            if (lua_istable(L, -1)) _make_readonly(L);

            lua_rawset(L, -5); 

            lua_pop(L, 1);    
        }

        return luaL_ref(L, LUA_REGISTRYINDEX);
    }
}

namespace jfc::lua {
    interpreter::interpreter(interpreter_policy aPolicy)
    : m_pLimits(std::make_shared<interpreter_limits>()) {
        m_pLimits->cap = aPolicy.MEMORY_BUDGET_IN_BYTES;
        m_pLimits->growthCap = aPolicy.MEMORY_GROWTH_BUDGET_IN_BYTES_PER_RUN;
        m_pLimits->instructionBudget = aPolicy.INSTRUCTION_BUDGET;
        m_pLimits->onExhausted = aPolicy.ON_INSTRUCTION_BUDGET_EXHAUSTED;

        auto *const state = lua_newstate(_capped_alloc, m_pLimits.get());

        if (!state) throw exception("could not create a lua state");

        m_pState = std::shared_ptr<lua_State>(state,
            [limits = m_pLimits](lua_State *p) { lua_close(p); });

        auto *L = m_pState.get();

        lua_atpanic(L, [](lua_State *p) {
            const char *const message = lua_tostring(p, -1);

            std::fprintf(stderr, "PANIC: unprotected error in call to lua api (%s)\n",
                message ? message : "no description");

            return 0;
        });

        luaL_openlibs(L);

        const stack_guard guard(L);

        lua_getglobal(L, "jit");

        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, "off");

            if (lua_isfunction(L, -1)) m_pLimits->jitOffReference = luaL_ref(L, LUA_REGISTRYINDEX);
            else lua_pop(L, 1);
        }

        lua_pop(L, 1);

        if (m_pLimits->instructionBudget) _ensure_instruction_hook(L, m_pLimits.get());

        lua_pushliteral(L, "");

        if (lua_getmetatable(L, -1))
        {
            lua_pushboolean(L, 0);
            lua_setfield(L, -2, "__metatable");

            lua_pop(L, 1);
        }

        lua_pop(L, 1);

        m_StandardLibraryReference = _snapshot_globals(L);
        m_SafeLibraryReference = _snapshot_safe_globals(L);

        lua_pushvalue(L, LUA_GLOBALSINDEX);

        m_pGlobals = environment_shared_ptr_type(
            new environment(m_pState, luaL_ref(L, LUA_REGISTRYINDEX), {}));
    }

    environment_shared_ptr_type interpreter::make_environment(environment_policy aPolicy) {
        auto *L = m_pState.get();

        const stack_guard guard(L);

        if (aPolicy.INSTRUCTION_BUDGET.value_or(0)) _ensure_instruction_hook(L, m_pLimits.get());

        lua_newtable(L);

        lua_newtable(L);
        lua_rawgeti(L, LUA_REGISTRYINDEX, aPolicy.LIBRARY == standard_library::safe
            ? m_SafeLibraryReference
            : m_StandardLibraryReference);
        lua_setfield(L, -2, "__index");
        lua_setmetatable(L, -2);

        lua_pushvalue(L, -1);
        lua_setfield(L, -2, "_G");

        return environment_shared_ptr_type(
            new environment(m_pState, luaL_ref(L, LUA_REGISTRYINDEX), aPolicy.INSTRUCTION_BUDGET));
    }

    std::optional<double> interpreter::read_number(const path &aPath) const
        { return m_pGlobals->read_number(aPath); }

    std::optional<bool> interpreter::read_boolean(const path &aPath) const
        { return m_pGlobals->read_boolean(aPath); }

    std::optional<std::string> interpreter::read_string(const path &aPath) const
        { return m_pGlobals->read_string(aPath); }

    std::optional<data_table> interpreter::read_data_table(const path &aPath,
        const unsupported aPolicy) const
        { return m_pGlobals->read_data_table(aPath, aPolicy); }

    void interpreter::write(const path &aPath, const bool aValue)
        { m_pGlobals->write(aPath, aValue); }

    void interpreter::write(const path &aPath, const double aValue)
        { m_pGlobals->write(aPath, aValue); }

    void interpreter::write(const path &aPath, const std::string &aValue)
        { m_pGlobals->write(aPath, aValue); }

    void interpreter::write(const path &aPath, const std::string::value_type *aValue)
        { m_pGlobals->write(aPath, aValue); }

    void interpreter::write(const path &aPath, const data_table &aValue)
        { m_pGlobals->write(aPath, aValue); }

    template <typename value_type>
    std::optional<std::vector<value_type>> interpreter::read_vector(const path &aPath) const
        { return m_pGlobals->read_vector<value_type>(aPath); }

    template std::optional<std::vector<double>>
        interpreter::read_vector<double>(const path &) const;
    template std::optional<std::vector<bool>>
        interpreter::read_vector<bool>(const path &) const;
    template std::optional<std::vector<std::string>>
        interpreter::read_vector<std::string>(const path &) const;

    void interpreter::write(const path &aPath, const std::vector<double> &aValue)
        { m_pGlobals->write(aPath, aValue); }

    void interpreter::write(const path &aPath, const std::vector<bool> &aValue)
        { m_pGlobals->write(aPath, aValue); }

    void interpreter::write(const path &aPath, const std::vector<std::string> &aValue)
        { m_pGlobals->write(aPath, aValue); }

    interpreter::error_type interpreter::call(const path &aPath, const value_list_type &aArguments,
        value_list_type &aResults)
        { return m_pGlobals->call(aPath, aArguments, aResults); }

    interpreter::error_type interpreter::call(const path &aPath, const value_list_type &aArguments)
        { return m_pGlobals->call(aPath, aArguments); }

    std::optional<reference> interpreter::read_reference(const path &aPath) const
        { return m_pGlobals->read_reference(aPath); }

    void interpreter::write(const path &aPath, const userdata &aValue)
        { m_pGlobals->write(aPath, aValue); }

    std::optional<userdata> interpreter::_read_userdata(const path &aPath) const
        { return m_pGlobals->read_userdata(aPath); }

    std::optional<userdata> interpreter::read_userdata(const path &aPath) const
        { return _read_userdata(aPath); }

    void interpreter::register_function(const path &aName, closure_type aClosure)
        { m_pGlobals->register_function(aName, std::move(aClosure)); }

    interpreter::error_type interpreter::run(const std::string &aLuaScript)
        { return m_pGlobals->run(aLuaScript); }

    interpreter::error_type interpreter::validate_syntax(const std::string &aLuaScript) const
        { return m_pGlobals->validate_syntax(aLuaScript); }
}
