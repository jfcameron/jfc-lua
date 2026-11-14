// © Joseph Cameron - MIT License

#include <jfc/lua/internal.h>

#include <jfc/lua/exception.h>
#include <jfc/lua/environment.h>
#include <jfc/lua/interpreter.h>
#include <jfc/lua/data_table.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <memory>
#include <set>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
#include <utility>

using jfc::lua::interpreter_limits;

namespace {
    //! how many tables deep from an environment's globals `registered` looks for functions written in lua
    constexpr int DEFINED_DEPTH = 4;

    [[nodiscard]] bool _identifier(const std::string &aKey) {
        const auto letter = [](const unsigned char aEach) { return std::isalpha(aEach) || aEach == '_'; };

        return !aKey.empty() && letter(static_cast<unsigned char>(aKey[0])) && std::all_of(aKey.begin(), aKey.end(),
            [&](const char aEach) { return letter(static_cast<unsigned char>(aEach))
                || std::isdigit(static_cast<unsigned char>(aEach)); });
    }

    //! a script's name as `run` was given it; empty for a script run without one
    [[nodiscard]] std::string _source_of(const char *const aSource) {
        return aSource && (aSource[0] == '@' || aSource[0] == '=') ? std::string(aSource + 1) : std::string();
    }

    //! every function written in lua in the table at aTable, and in its tables to aDepth more, each table once
    void _walk(lua_State *const L, const int aTable, const std::string &aPrefix, const int aDepth,
        std::set<const void *> &aVisited, std::map<std::string, jfc::lua::definition> &aOut) {
        if (!jfc::lua::backend::check_stack(L, 4)) return;

        lua_pushnil(L);

        while (lua_next(L, aTable)) {
            if (lua_type(L, -2) == LUA_TSTRING) {
                std::size_t length = 0;

                const char *const pKey = lua_tolstring(L, -2, &length);
                const std::string key(pKey, length);

                if (_identifier(key)) {
                    const auto path = aPrefix.empty() ? key : aPrefix + "." + key;

                    if (lua_isfunction(L, -1) && !lua_iscfunction(L, -1)) {
                        std::string source;
                        int line = 0;

                        if (jfc::lua::backend::defined_at(L, source, line))
                            aOut[path] = {_source_of(source.c_str()), line};
                    }
                    else if (lua_istable(L, -1) && aDepth > 0 && aVisited.insert(lua_topointer(L, -1)).second)
                        _walk(L, lua_gettop(L), path, aDepth - 1, aVisited, aOut);
                }
            }

            lua_pop(L, 1);
        }
    }

#if defined(JFC_LUA_LUAU)
    //! the table on top replaced by a frozen proxy of it, its metatable frozen too: Luau refuses a write to either,
    //! rawset's too, and what the host adds to the table is seen through it
    void _make_readonly(lua_State *const L) {
        lua_newtable(L);

        lua_newtable(L);

        lua_pushvalue(L, -3);
        lua_setfield(L, -2, "__index");

        lua_pushboolean(L, 0);
        lua_setfield(L, -2, "__metatable");

        lua_setreadonly(L, -1, true);
        lua_setmetatable(L, -2);

        lua_setreadonly(L, -1, true);
        lua_remove(L, -2);
    }
#else
    int _reject_standard_library_write(lua_State *const L) {
        return JFC_LUA_RAISE(L, "the standard library is shared by every environment and cannot be "
            "modified from within one");
    }

    //! the table on top replaced by a proxy of it, which refuses a write
    void _make_readonly(lua_State *const L) {
        lua_newtable(L);

        lua_newtable(L);

        lua_pushvalue(L, -3);
        lua_setfield(L, -2, "__index");

        jfc::lua::backend::push_function(L, _reject_standard_library_write, "jfc.lua.readonly");
        lua_setfield(L, -2, "__newindex");

        lua_pushboolean(L, 0);
        lua_setfield(L, -2, "__metatable");

        lua_setmetatable(L, -2);

        lua_remove(L, -2);
    }

    //! rawset, refusing a proxy: its own fields would be every environment's
    int _guarded_rawset(lua_State *const L) {
        luaL_checktype(L, 1, LUA_TTABLE);
        luaL_checkany(L, 2);
        luaL_checkany(L, 3);

        if (lua_getmetatable(L, 1)) {
            lua_getfield(L, -1, "__newindex");

            const bool shared = lua_tocfunction(L, -1) == _reject_standard_library_write;

            lua_pop(L, 2);

            if (shared) return _reject_standard_library_write(L);
        }

        lua_settop(L, 3);
        lua_rawset(L, 1);

        return 1;
    }
#endif

    //! under Lua 5.1, vm instructions between looks at the clock
    [[maybe_unused]] constexpr int INSTRUCTION_CHECK_INTERVAL = 1000;

    //! under Luau, interrupts between looks at the clock
    [[maybe_unused]] constexpr unsigned INTERRUPT_CHECK_INTERVAL = 64;

    //! the allocator a Luau state is made with, there being no default to take
    [[maybe_unused]] void *_plain_alloc(void *, void *aBlock, std::size_t, std::size_t aNewSize) {
        if (aNewSize == 0) {
            std::free(aBlock);

            return nullptr;
        }

        return std::realloc(aBlock, aNewSize);
    }

    void *_capped_alloc(void *aUserData, void *aBlock, std::size_t aOldSize, std::size_t aNewSize) {
        auto *const limits = static_cast<interpreter_limits *>(aUserData);
        const std::size_t held = aBlock ? aOldSize : 0;

        if (aNewSize == 0) {
            limits->used -= held;
            return limits->innerAlloc(limits->innerData, aBlock, aOldSize, 0);
        }

        const std::size_t prospective = limits->used - held + aNewSize;

        if (!limits->unbudgeted) {
            if (limits->cap && prospective > limits->cap) return nullptr;

            if (limits->growthCap && limits->depth > 0 && prospective > limits->usedAtRunStart
                && prospective - limits->usedAtRunStart > limits->growthCap) return nullptr;
        }

        void *const allocated = limits->innerAlloc(limits->innerData, aBlock, aOldSize, aNewSize);

        if (allocated) limits->used = prospective;

        return allocated;
    }

    //! stops the run where it has taken longer than its budget, unless whoever set it lets it carry on
    void _check_time_budget(lua_State *const L) {
        void *ud = nullptr;

        lua_getallocf(L, &ud);

        auto *const limits = static_cast<interpreter_limits *>(ud);

        if (!limits || limits->timeBudget.count() <= 0) return;

        if (limits->aborted) luaL_error(L, "script exceeded its time budget");

        const auto taken = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - limits->runStarted);

        if (taken <= limits->timeBudget) return;

        bool carryOn = false;

        if (limits->onExhausted) {
            // a c++ exception thrown here would unwind through the vm and leave it in pieces, so it's treated as "stop"
            try { carryOn = limits->onExhausted(taken); }
            catch (...) { carryOn = false; }
        }

        if (carryOn) {
            limits->runStarted = std::chrono::steady_clock::now();

            return;
        }

        limits->aborted = true;

        luaL_error(L, "script exceeded its time budget");
    }

#if defined(JFC_LUA_LUAU)
    void _interrupt(lua_State *const L, const int aCollecting) {
        if (aCollecting >= 0) return;

        void *ud = nullptr;

        lua_getallocf(L, &ud);

        auto *const limits = static_cast<interpreter_limits *>(ud);

        if (limits && ++limits->interrupts % INTERRUPT_CHECK_INTERVAL == 0) _check_time_budget(L);
    }
#else
    void _instruction_hook(lua_State *const L, lua_Debug *) { _check_time_budget(L); }
#endif

    void _ensure_budget_check(lua_State *const L, interpreter_limits *const aLimits) {
        if (!aLimits || aLimits->hookInstalled) return;

#if defined(JFC_LUA_LUAU)
        lua_callbacks(L)->interrupt = _interrupt;
#else
        lua_sethook(L, _instruction_hook, LUA_MASKCOUNT, INSTRUCTION_CHECK_INTERVAL);
#endif

        aLimits->hookInstalled = true;
    }

    //! told of a lua error nothing was protecting against
    void _panicked(lua_State *const L) {
        void *ud = nullptr;

        lua_getallocf(L, &ud);

        const auto *const pLimits = static_cast<const interpreter_limits *>(ud);

        if (pLimits && pLimits->onPanic) {
            const char *const message = lua_tostring(L, -1);

            try { pLimits->onPanic(message ? message : "no description"); }
            catch (...) {}
        }
    }

    //! base values that cannot reach outside the vm, of either dialect: one a dialect lacks is passed over
    constexpr const char *SAFE_VALUES[] = {
        "_VERSION", "assert", "error", "getmetatable", "ipairs", "next", "pairs", "pcall", "print",
        "rawequal", "rawget", "rawlen", "rawset", "select", "setmetatable", "tonumber", "tostring", "type",
        "typeof", "unpack", "xpcall"
    };

    //! libraries with no route to the host, taken whole: LuaJIT's bit, and Luau's bit32, utf8, buffer and vector
    constexpr const char *SAFE_LIBRARIES[] = {
        "coroutine", "math", "table", "bit", "bit32", "utf8", "buffer", "vector"
    };

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

#if defined(JFC_LUA_LUAU)
        lua_setreadonly(L, -1, true);
#else
        jfc::lua::backend::push_function(L, _guarded_rawset, "rawset");
        lua_setfield(L, -2, "rawset");
#endif

        return jfc::lua::backend::ref(L);
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

#if defined(JFC_LUA_LUAU)
        lua_setreadonly(L, -1, true);
#endif

        return jfc::lua::backend::ref(L);
    }
}

namespace jfc::lua {
    interpreter::interpreter(interpreter_policy aPolicy)
    : m_pLimits(std::make_shared<interpreter_limits>()) {
        m_pLimits->cap = aPolicy.MEMORY_BUDGET_IN_BYTES;
        m_pLimits->growthCap = aPolicy.MEMORY_GROWTH_BUDGET_IN_BYTES_PER_RUN;
        m_pLimits->timeBudget = aPolicy.TIME_BUDGET;
        m_pLimits->onExhausted = aPolicy.ON_TIME_BUDGET_EXHAUSTED;
        m_pLimits->onPanic = aPolicy.ON_PANIC;

#if defined(JFC_LUA_LUAU)
        m_pLimits->innerAlloc = _plain_alloc;

        auto *const state = lua_newstate(_capped_alloc, m_pLimits.get());

        if (!state) throw exception("could not create a lua state");
#else
        auto *const state = luaL_newstate();

        if (!state) throw exception("could not create a lua state");

        m_pLimits->innerAlloc = lua_getallocf(state, &m_pLimits->innerData);
        m_pLimits->used = static_cast<std::size_t>(lua_gc(state, LUA_GCCOUNT, 0)) * 1024
            + static_cast<std::size_t>(lua_gc(state, LUA_GCCOUNTB, 0));

        lua_setallocf(state, _capped_alloc, m_pLimits.get());
#endif

        m_pState = std::shared_ptr<lua_State>(state, [limits = m_pLimits](lua_State *pState) {
#if !defined(JFC_LUA_LUAU)
            lua_setallocf(pState, limits->innerAlloc, limits->innerData);
#endif
            lua_close(pState);

            // it points at the control block holding this deleter, which holds limits: a cycle unless broken
            limits->state.reset();
        });

        m_pLimits->state = m_pState;

        auto *L = m_pState.get();

#if defined(JFC_LUA_LUAU)
        lua_callbacks(L)->panic = [](lua_State *pState, int) { _panicked(pState); };
#else
        lua_atpanic(L, [](lua_State *pState) {
            _panicked(pState);

            return 0;
        });
#endif

        const stack_guard guard(L);

        if (const auto error = detail::protect(L, [&](lua_State *) { _set_up(L); }))
            throw exception("could not set up a lua state: " + *error);
    }

    void interpreter::_set_up(lua_State *const L) {
        luaL_openlibs(L);

#if defined(JFC_LUA_LUAU)
        lua_getglobal(L, "math");
        lua_getfield(L, -1, "randomseed");
        lua_pushinteger(L, 0);
        lua_call(L, 1, 0);
        lua_pop(L, 1);
#endif

        lua_getglobal(L, "jit");

        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, "off");

            if (lua_isfunction(L, -1)) m_pLimits->jitOffReference = backend::ref(L);
            else lua_pop(L, 1);
        }

        lua_pop(L, 1);

        if (m_pLimits->timeBudget.count()) _ensure_budget_check(L, m_pLimits.get());

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
            new environment(m_pState, backend::ref(L), {}, {}));
    }

    environment_shared_ptr_type interpreter::make_environment(environment_policy aPolicy) {
        auto *L = m_pState.get();

        const stack_guard guard(L);

        if (aPolicy.TIME_BUDGET.value_or(std::chrono::nanoseconds(0)).count())
            _ensure_budget_check(L, m_pLimits.get());

        int root = LUA_NOREF;

        if (const auto error = detail::protect(L, [&](lua_State *) {
            lua_newtable(L);

            lua_newtable(L);
            lua_rawgeti(L, LUA_REGISTRYINDEX, aPolicy.LIBRARY == standard_library::safe
                ? m_SafeLibraryReference
                : m_StandardLibraryReference);
            lua_setfield(L, -2, "__index");
            lua_pushboolean(L, 0);
            lua_setfield(L, -2, "__metatable");
            lua_setmetatable(L, -2);

            lua_pushvalue(L, -1);
            lua_setfield(L, -2, "_G");

#if defined(JFC_LUA_LUAU)
            lua_setsafeenv(L, -1, true);
#endif

            root = backend::ref(L);
        })) throw exception("could not make an environment: " + *error);

        auto made = environment_shared_ptr_type(new environment(m_pState, root, aPolicy.TIME_BUDGET,
            aPolicy.NAME));

        if (!aPolicy.NAME.empty()) m_pLimits->latest[aPolicy.NAME] = made;

        return made;
    }

    catalogue interpreter::registered() const {
        auto *const L = m_pState.get();

        const detail::type_namer nameOf = [L](const void *const aTypeKey) {
            return detail::type_name(L, aTypeKey).value_or("userdata");
        };

        catalogue out;

        for (const auto &[name, functions] : m_pLimits->registered) {
            auto &listed = out.environments[name];

            for (const auto &[path, made] : functions) {
                auto each = made.first ? made.first(nameOf) : signature{};

                each.yields = made.second;
                listed.emplace(path, std::move(each));
            }
        }

        for (const auto &[type, methods] : m_pLimits->methods) {
            auto &listed = out.types[type];

            for (const auto &[name, describe] : methods)
                listed.emplace(name, describe ? describe(nameOf) : signature{});
        }

        for (const auto &[name, latest] : m_pLimits->latest) {
            const auto pEnvironment = latest.lock();

            if (!pEnvironment) continue;

            const stack_guard guard(L);

            auto &listed = out.defined[name];

            std::set<const void *> visited;

            if (const auto error = detail::protect(L, [&](lua_State *) {
                lua_rawgeti(L, LUA_REGISTRYINDEX, pEnvironment->registry_reference());

                visited.insert(lua_topointer(L, -1));

                _walk(L, lua_gettop(L), {}, DEFINED_DEPTH, visited, listed);
            })) throw exception("could not list what the " + name + " environment defines: " + *error);
        }

        return out;
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

    void interpreter::set_call_timer(call_timer_type aTimer)
        { m_pLimits->callTimer = std::move(aTimer); }

    interpreter::error_type interpreter::run(const std::string &aLuaScript, const std::string_view aName)
        { return m_pGlobals->run(aLuaScript, aName); }

    interpreter::error_type interpreter::run(const script &aScript)
        { return m_pGlobals->run(aScript); }

    void interpreter::collect_garbage()
        { lua_gc(m_pState.get(), LUA_GCCOLLECT, 0); }

    interpreter::error_type interpreter::validate_syntax(const std::string &aLuaScript, const std::string_view aName) const
        { return m_pGlobals->validate_syntax(aLuaScript, aName); }
}
