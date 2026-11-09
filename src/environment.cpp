// © Joseph Cameron - MIT License

#include <jfc/lua/internal.h>

#include <jfc/lua/exception.h>
#include <jfc/lua/environment.h>
#include <jfc/lua/key.h>
#include <jfc/lua/path.h>
#include <jfc/lua/reference.h>
#include <jfc/lua/userdata.h>
#include <jfc/lua/data_table.h>

#include <cstring>
#include <functional>
#include <new>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

using jfc::lua::interpreter_limits;

bool jfc::lua::detail::to_param(lua_State *const L, const int aIndex,
    jfc::lua::value_list_type &aOut) {
    switch (lua_type(L, aIndex)) {
        case LUA_TNUMBER:  aOut.push_back(lua_tonumber(L, aIndex)); return true;
        case LUA_TBOOLEAN: aOut.push_back(static_cast<bool>(lua_toboolean(L, aIndex))); return true;
        case LUA_TNIL:     aOut.push_back(nullptr); return true;

        case LUA_TSTRING: {
            size_t length;
            const char *const text = lua_tolstring(L, aIndex, &length);

            aOut.push_back(std::string(text, length));

            return true;
        }

        case LUA_TTABLE: aOut.push_back(jfc::lua::data_table(L, aIndex)); return true;

        case LUA_TUSERDATA: {
            const auto *const bound = jfc::lua::detail::to_userdata(L, aIndex);

            if (!bound) return false;

            aOut.push_back(*bound);

            return true;
        }

        default: return false;
    }
}

void jfc::lua::detail::push_param(lua_State *const L,
    const jfc::lua::value_list_type::value_type &aValue) {
    std::visit([L](auto &&aHeld) {
        using value_type = std::decay_t<decltype(aHeld)>;

        if constexpr (std::is_same_v<value_type, double>) lua_pushnumber(L, aHeld);
        else if constexpr (std::is_same_v<value_type, bool>) lua_pushboolean(L, aHeld);
        else if constexpr (std::is_same_v<value_type, std::string>) lua_pushlstring(L, aHeld.data(), aHeld.size());
        else if constexpr (std::is_same_v<value_type, jfc::lua::data_table>) aHeld.push_to_lua_state(L);
        else if constexpr (std::is_same_v<value_type, jfc::lua::userdata>)
            jfc::lua::detail::push_userdata(L, aHeld);
        else lua_pushnil(L);
    }, aValue);
}

void jfc::lua::detail::push_key(lua_State *const L, const jfc::lua::key &aKey) {
    std::visit([L](auto &&aHeld) {
        using value_type = std::decay_t<decltype(aHeld)>;

        if constexpr (std::is_same_v<value_type, double>) lua_pushnumber(L, aHeld);
        else if constexpr (std::is_same_v<value_type, bool>) lua_pushboolean(L, aHeld);
        else lua_pushlstring(L, aHeld.data(), aHeld.size());
    }, aKey.value());
}

static bool _read_value(lua_State *L, const int aRoot, const jfc::lua::path &aPath) {
    lua_rawgeti(L, LUA_REGISTRYINDEX, aRoot);

    for (std::size_t i = 0; i < aPath.size(); ++i) {
        if (!lua_istable(L, -1) && !jfc::lua::detail::indexable_userdata(L, -1)) {
            lua_pop(L, 1);

            return false;
        }

        jfc::lua::detail::push_key(L, aPath[i]);

        lua_gettable(L, -2);

        lua_remove(L, -2);
    }

    return true;
}

static void _write_value(lua_State *L, const int aRoot, const jfc::lua::path &aPath,
    std::function<void()> &&aPushValueFunctor) {
    if (aPath.empty()) throw jfc::lua::exception("an empty path names nothing to write to");

    lua_rawgeti(L, LUA_REGISTRYINDEX, aRoot);

    const int container = lua_gettop(L);

    if (!lua_istable(L, container)) {
        if (aPath.empty()) return;

        jfc::lua::detail::push_key(L, aPath[0]);

        if (!jfc::lua::detail::push_field_table(L, container, lua_gettop(L))) return;

        lua_replace(L, container);
    }

    for (std::size_t i = 0; i + 1 < aPath.size(); ++i) {
        jfc::lua::detail::push_key(L, aPath[i]);
        lua_rawget(L, container);

        if (!lua_istable(L, -1)) {
            lua_pop(L, 1);

            jfc::lua::detail::push_key(L, aPath[i]);
            lua_gettable(L, container);

            const bool inheritedATable = lua_istable(L, -1);

            lua_newtable(L);

            if (inheritedATable) {
                lua_newtable(L);

                lua_pushvalue(L, -3);
                lua_setfield(L, -2, "__index");

                lua_setmetatable(L, -2);
            }

            jfc::lua::detail::push_key(L, aPath[i]);
            lua_pushvalue(L, -2);
            lua_settable(L, container);

            lua_remove(L, -2);
        }

        lua_replace(L, container);
    }

    aPushValueFunctor();

    jfc::lua::detail::push_key(L, aPath[aPath.size() - 1]);
    lua_pushvalue(L, -2);

    lua_settable(L, container);
}

namespace {
    void _disable_jit_for_chunk(lua_State *const L, interpreter_limits *const aLimits) {
        if (!aLimits || aLimits->jitOffReference == -2 /* LUA_NOREF */) return;

        lua_rawgeti(L, LUA_REGISTRYINDEX, aLimits->jitOffReference);

        lua_pushvalue(L, -2);
        lua_pushboolean(L, 1);

        if (lua_pcall(L, 2, 0, 0) != LUA_OK) lua_pop(L, 1);
    }

    [[nodiscard]] std::optional<std::string> _to_string(lua_State *const L, const int aIndex) {
        const auto *const text = lua_tostring(L, aIndex);

        if (!text) return {};

        return std::string(text);
    }
}

namespace jfc::lua {
    environment::environment(std::shared_ptr<lua_State> aState, const int aRootReference,
        const std::optional<std::size_t> aInstructionBudget, std::string aName)
    : reference(std::move(aState), aRootReference)
    , m_InstructionBudget(aInstructionBudget)
    , m_Name(std::move(aName))
    {}

    namespace {
        template <typename read_type>
        [[nodiscard]] bool _protected_read(lua_State *const L, const int aRoot, const path &aPath, read_type &&aRead) {
            return !detail::protect(L, [&](lua_State *) { if (_read_value(L, aRoot, aPath)) aRead(); });
        }

        void _protected_write(lua_State *const L, const int aRoot, const path &aPath, std::function<void()> &&aPush) {
            if (const auto error = detail::protect(L, [&](lua_State *) { _write_value(L, aRoot, aPath, std::move(aPush)); })) {
                std::ostringstream where;

                where << aPath;

                throw exception("could not write " + where.str() + ": " + *error);
            }
        }
    }

    std::optional<double> reference::read_number(const path &aPath) const {
        auto *L = state();

        const stack_guard guard(L);

        std::optional<double> val;

        if (!_protected_read(L, registry_reference(), aPath, [&] {
            if (lua_type(L, -1) == LUA_TNUMBER) val = lua_tonumber(L, -1);
        })) return {};

        return val;
    }

    std::optional<bool> reference::read_boolean(const path &aPath) const {
        auto *L = state();

        const stack_guard guard(L);

        std::optional<bool> val;

        if (!_protected_read(L, registry_reference(), aPath, [&] {
            if (lua_isboolean(L, -1)) val = lua_toboolean(L, -1);
        })) return {};

        return val;
    }

    std::optional<std::string> reference::read_string(const path &aPath) const {
        auto *L = state();

        const stack_guard guard(L);

        std::optional<std::string> val;

        if (!_protected_read(L, registry_reference(), aPath, [&] {
            if (lua_type(L, -1) != LUA_TSTRING) return;

            size_t len;
            const char *str = lua_tolstring(L, -1, &len);

            val = std::string(str, len);
        })) return {};

        return val;
    }

    std::optional<data_table> reference::read_data_table(const path &aPath,
        const unsupported aPolicy) const {
        auto *L = state();

        const stack_guard guard(L);

        std::optional<data_table> val;

        if (!_protected_read(L, registry_reference(), aPath, [&] {
            if (lua_istable(L, -1)) val = data_table(L, -1, aPolicy);
        })) return {};

        return val;
    }

    void reference::write(const path &aPath, const bool aValue) {
        auto *L = state();

        const stack_guard guard(L);

        _protected_write(L, registry_reference(), aPath, [L, aValue]() { lua_pushboolean(L, aValue); });
    }

    void reference::write(const path &aPath, const double aValue) {
        auto *L = state();

        const stack_guard guard(L);

        _protected_write(L, registry_reference(), aPath, [L, aValue]() { lua_pushnumber(L, aValue); });
    }

    void reference::write(const path &aPath, const std::string &aValue) {
        auto *L = state();

        const stack_guard guard(L);

        _protected_write(L, registry_reference(), aPath, [L, &aValue]() { lua_pushlstring(L, aValue.data(), aValue.size()); });
    }

    void reference::write(const path &aPath, const std::string::value_type *aValue) {
        auto *L = state();

        const stack_guard guard(L);

        _protected_write(L, registry_reference(), aPath, [L, &aValue]() { lua_pushstring(L, aValue); });
    }

    void reference::write(const path &aPath, const data_table &aValue) {
        auto *L = state();

        const stack_guard guard(L);

        _protected_write(L, registry_reference(), aPath, [L, &aValue]() { aValue.push_to_lua_state(L); });
    }

    template <typename value_type>
    [[nodiscard]] std::optional<std::vector<value_type>> _to_vector(const data_table &aTable) {
        std::vector<value_type> out;

        out.reserve(aTable.size());

        for (std::size_t i = 1; i <= aTable.size(); ++i) {
            std::optional<value_type> element;

            if constexpr (std::is_same_v<value_type, double>) element = aTable.get_number(i);
            else if constexpr (std::is_same_v<value_type, bool>) element = aTable.get_boolean(i);
            else element = aTable.get_string(i);

            if (!element) return {};

            out.push_back(*element);
        }

        return out;
    }

    template <typename container_type>
    void _write_as_array(reference &aReference, const path &aPath, const container_type &aValues) {
        data_table out;

        std::size_t index = 1;

        for (const auto &value : aValues) out.set(index++, value);

        aReference.write(aPath, out);
    }

    template <typename value_type>
    std::optional<std::vector<value_type>> reference::read_vector(const path &aPath) const {
        std::optional<data_table> found;

        try { found = read_data_table(aPath); }
        catch (const exception &) { return {}; }

        if (!found) return {};

        return _to_vector<value_type>(*found);
    }

    template std::optional<std::vector<double>>
        reference::read_vector<double>(const path &) const;
    template std::optional<std::vector<bool>>
        reference::read_vector<bool>(const path &) const;
    template std::optional<std::vector<std::string>>
        reference::read_vector<std::string>(const path &) const;

    void reference::write(const path &aPath, const std::vector<double> &aValue)
        { _write_as_array(*this, aPath, aValue); }

    void reference::write(const path &aPath, const std::vector<bool> &aValue) {
        data_table out;

        std::size_t index = 1;

        for (const bool value : aValue) out.set(index++, value);

        write(aPath, out);
    }

    void reference::write(const path &aPath, const std::vector<std::string> &aValue) { _write_as_array(*this, aPath, aValue); }

    namespace {
        /// \param aWhat makes the name an error calls this by, which is too slow to make for a call that works
        /// \param aPush pushes the arguments, given the stack's top, and returns how many it pushed
        /// \param aRead takes the results, given where they begin
        template <typename what_type, typename push_type, typename read_type>
        [[nodiscard]] error_type _invoke(lua_State *const L, const what_type &aWhat, const push_type &aPush,
            const read_type &aRead)
        {
            if (lua_type(L, -1) != LUA_TFUNCTION)
                return aWhat() + " holds a " + lua_typename(L, lua_type(L, -1)) + ", not a function";

            const int base = lua_gettop(L);

            int pushed = 0;

            try { pushed = aPush(base); }
            catch (const exception &e) { return e.what(); }

            if (lua_pcall(L, pushed, LUA_MULTRET, 0) != LUA_OK)
                return _to_string(L, -1).value_or("lua reported an error it could not describe");

            return aRead(base);
        }

        template <typename what_type, typename find_type, typename push_type, typename read_type>
        [[nodiscard]] error_type _protected_invoke(lua_State *const L, const what_type &aWhat, find_type &&aFind,
            const push_type &aPush, const read_type &aRead) {
            error_type error;

            if (const auto raised = detail::protect(L, [&](lua_State *) {
                if (const auto missing = aFind()) error = *missing;
                else error = _invoke(L, aWhat, aPush, aRead);
            })) return raised;

            return error;
        }

        [[nodiscard]] auto _push_list(lua_State *const L, const value_list_type &aArguments) {
            return [L, &aArguments](int) {
                if (!lua_checkstack(L, static_cast<int>(aArguments.size()) + 2))
                    throw exception("the lua stack cannot grow enough to make this call");

                for (const auto &argument : aArguments) jfc::lua::detail::push_param(L, argument);

                return static_cast<int>(aArguments.size());
            };
        }

        template <typename what_type>
        [[nodiscard]] auto _read_list(lua_State *const L, const what_type &aWhat, value_list_type &aResults) {
            return [L, &aWhat, &aResults](const int aBase) -> error_type {
                for (int i = aBase; i <= lua_gettop(L); ++i) {
                    const auto unconverted = [&](const std::string &aWhy) -> error_type {
                        return aWhat() + " returned a " + lua_typename(L, lua_type(L, i))
                            + " in position " + std::to_string(i - aBase + 1) + aWhy;
                    };

                    try {
                        if (!jfc::lua::detail::to_param(L, i, aResults)) return unconverted(", which cannot cross into c++");
                    }
                    catch (const exception &e) { return unconverted(" that cannot cross into c++: " + std::string(e.what())); }
                }

                return {};
            };
        }

        [[nodiscard]] auto _push_work(lua_State *const L, const detail::stack_work &aWork) {
            return [L, &aWork](const int aBase) {
                detail::call_frame frame(L, {}, aBase, "argument");

                aWork.run(aWork.pContext, frame);

                return lua_gettop(L) - aBase;
            };
        }

        template <typename what_type>
        [[nodiscard]] auto _read_work(lua_State *const L, const what_type &aWhat, const detail::stack_work &aWork) {
            return [L, &aWhat, &aWork](const int aBase) -> error_type {
                try {
                    detail::call_frame frame(L, {}, aBase - 1, "result");

                    aWork.run(aWork.pContext, frame);

                    return {};
                }
                catch (const exception &e) { return aWhat() + ": " + e.what(); }
            };
        }

        [[nodiscard]] std::string _name_of(const path &aPath) {
            std::ostringstream where;

            where << aPath;

            return where.str();
        }
    }

    environment::error_type environment::call(const path &aPath, const value_list_type &aArguments,
        value_list_type &aResults) {
        auto *L = state();

        const stack_guard guard(L);
        const root_scope root(L, registry_reference());
        const budget_scope budget(L, m_InstructionBudget);

        if (budget.aborted()) return "script exceeded its instruction budget";

        aResults.clear();

        const auto what = [&aPath] { return _name_of(aPath); };

        auto error = _protected_invoke(L, what, [&]() -> error_type {
            if (!_read_value(L, registry_reference(), aPath)) return what() + " does not resolve";

            return {};
        }, _push_list(L, aArguments), _read_list(L, what, aResults));

        if (error) aResults.clear();

        return error;
    }

    error_type environment::_call(const path &aPath, const detail::stack_work &aPush, const detail::stack_work &aRead) {
        auto *L = state();

        const stack_guard guard(L);
        const root_scope root(L, registry_reference());
        const budget_scope budget(L, m_InstructionBudget);

        if (budget.aborted()) return "script exceeded its instruction budget";

        const auto what = [&aPath] { return _name_of(aPath); };

        return _protected_invoke(L, what, [&]() -> error_type {
            if (!_read_value(L, registry_reference(), aPath)) return what() + " does not resolve";

            return {};
        }, _push_work(L, aPush), _read_work(L, what, aRead));
    }

    error_type reference::call(const value_list_type &aArguments, value_list_type &aResults)
        { return _call(aArguments, aResults, {}); }

    error_type reference::_call(const value_list_type &aArguments, value_list_type &aResults,
        const std::optional<std::size_t> aInstructionBudget) {
        auto *L = state();

        const stack_guard guard(L);

        const budget_scope budget(L, aInstructionBudget);

        if (budget.aborted()) return "script exceeded its instruction budget";

        aResults.clear();

        const auto what = [] { return std::string("the reference"); };

        auto error = _protected_invoke(L, what, [&]() -> error_type {
            lua_rawgeti(L, LUA_REGISTRYINDEX, registry_reference());

            return {};
        }, _push_list(L, aArguments), _read_list(L, what, aResults));

        if (error) aResults.clear();

        return error;
    }

    error_type reference::_call(const detail::stack_work &aPush, const detail::stack_work &aRead,
        const std::optional<std::size_t> aInstructionBudget) {
        auto *L = state();

        const stack_guard guard(L);

        const budget_scope budget(L, aInstructionBudget);

        if (budget.aborted()) return "script exceeded its instruction budget";

        const auto what = [] { return std::string("the reference"); };

        return _protected_invoke(L, what, [&]() -> error_type {
            lua_rawgeti(L, LUA_REGISTRYINDEX, registry_reference());

            return {};
        }, _push_work(L, aPush), _read_work(L, what, aRead));
    }

    error_type reference::call(const value_list_type &aArguments) {
        value_list_type discarded;

        return call(aArguments, discarded);
    }

    environment::error_type environment::call(const path &aPath, const value_list_type &aArguments) {
        value_list_type discarded;

        return call(aPath, aArguments, discarded);
    }

    std::optional<reference> reference::read_reference(const path &aPath) const {
        auto *L = state();

        const stack_guard guard(L);

        std::optional<reference> val;

        if (!_protected_read(L, registry_reference(), aPath, [&] {
            if (lua_isnil(L, -1)) return;

            lua_pushvalue(L, -1);

            val = reference(m_pState, luaL_ref(L, LUA_REGISTRYINDEX));
        })) return {};

        return val;
    }

    void reference::write(const path &aPath, const userdata &aValue) {
        auto *L = state();

        const stack_guard guard(L);

        _protected_write(L, registry_reference(), aPath, [L, &aValue]() { detail::push_userdata(L, aValue); });
    }

    std::optional<userdata> reference::_read_userdata(const path &aPath) const {
        auto *L = state();

        const stack_guard guard(L);

        std::optional<userdata> val;

        if (!_protected_read(L, registry_reference(), aPath, [&] {
            if (const auto *const held = detail::to_userdata(L, -1)) val = *held;
        })) return {};

        return val;
    }

    std::optional<userdata> reference::read_userdata(const path &aPath) const
        { return _read_userdata(aPath); }

    namespace {
        struct registered_closure final {
            detail::typed_closure_type closure;
            std::string name;
        };

        int _closure_trampoline(lua_State *pState) {
            auto *const pImpl = static_cast<registered_closure *>(lua_touserdata(pState, lua_upvalueindex(1)));

            return detail::guarded_call(pState, pImpl->name, [pImpl, pState] {
                detail::call_frame frame(pState, pImpl->name, 0, "argument");

                return pImpl->closure(frame);
            });
        }

        int _yielding_closure_trampoline(lua_State *pState) {
            const bool mainThread = lua_pushthread(pState) == 1;

            lua_pop(pState, 1);

            if (mainThread) return luaL_error(pState, "%s", "this function waits, and only a "
                "coroutine can wait: call it from one, not from a plain call or run");

            return lua_yield(pState, _closure_trampoline(pState));
        }
    }

    void environment::register_function(const path &aName, closure_type aClosure) {
        _register(aName, detail::make_typed_closure(std::move(aClosure)), detail::signature_maker_of<closure_type>(),
            false);
    }

    void environment::register_yielding_function(const path &aName, closure_type aClosure) {
        _register(aName, detail::make_typed_closure(std::move(aClosure)), detail::signature_maker_of<closure_type>(),
            true);
    }

    void environment::_register(const path &aName, detail::typed_closure_type aClosure,
        const detail::signature_maker aDescribe, const bool aYields) {
        auto *L = state();

        const stack_guard guard(L);
        const root_scope root(L, registry_reference());

        const auto trampoline = aYields ? _yielding_closure_trampoline : _closure_trampoline;

        std::ostringstream where;

        where << aName;

        const auto name = where.str();

        _protected_write(L, registry_reference(), aName, [&]() {
            auto *const pStorage = static_cast<registered_closure *>(lua_newuserdata(L, sizeof(registered_closure)));

            new (pStorage) registered_closure{aClosure, name};

            if (luaL_newmetatable(L, "jfc.lua.closure")) {
                lua_pushcfunction(L, [](lua_State *pState) {
                    static_cast<registered_closure *>(lua_touserdata(pState, 1))->~registered_closure();
                    return 0;
                });

                lua_setfield(L, -2, "__gc");
            }

            lua_setmetatable(L, -2);

            lua_pushcclosure(L, trampoline, 1);
        });

        if (m_Name.empty()) return;

        void *ud = nullptr;

        lua_getallocf(L, &ud);

        static_cast<interpreter_limits *>(ud)->registered[m_Name][name] = {aDescribe, aYields};
    }

    std::optional<coroutine> environment::make_coroutine(const path &aFunction) {
        auto *L = state();

        const stack_guard guard(L);

        const root_scope root(L, registry_reference());

        std::optional<reference> thread;

        if (const auto error = detail::protect(L, [&](lua_State *) {
            if (!_read_value(L, registry_reference(), aFunction) || !lua_isfunction(L, -1)) return;

            lua_State *const pThread = lua_newthread(L);

            lua_pushvalue(L, -2);
            lua_xmove(L, pThread, 1);

            thread = reference(m_pState, luaL_ref(L, LUA_REGISTRYINDEX));
        })) {
            std::ostringstream where;

            where << aFunction;

            throw exception("could not make a coroutine of " + where.str() + ": " + *error);
        }

        if (!thread) return {};

        return coroutine(std::move(*thread), m_InstructionBudget);
    }

    namespace {
        /// \brief loads aLuaScript as luaL_loadstring does, calling it aName if it has one
        [[nodiscard]] int _load(lua_State *const L, const std::string &aLuaScript, const std::string_view aName) {
            if (aName.empty()) return luaL_loadstring(L, aLuaScript.c_str());

            const std::string chunkName = "@" + std::string(aName);

            return luaL_loadbuffer(L, aLuaScript.c_str(), std::strlen(aLuaScript.c_str()), chunkName.c_str());
        }
    }

    environment::error_type environment::run(const std::string &aLuaScript, const std::string_view aName) {
        auto *L = state();

        const stack_guard guard(L);
        const root_scope root(L, registry_reference());
        const budget_scope budget(L, m_InstructionBudget);

        if (budget.aborted()) return "script exceeded its instruction budget";

        if (_load(L, aLuaScript, aName) != LUA_OK)
            return _to_string(L, -1).value_or("lua reported an error it could not describe");

        if (budget.instruction_budget()) _disable_jit_for_chunk(L, budget.limits());

        if (lua_pcall(L, 0, 0, 0) != LUA_OK)
            return _to_string(L, -1).value_or("lua reported an error it could not describe");

        return {};
    }

    environment::error_type environment::validate_syntax(const std::string &aLuaScript, const std::string_view aName) const {
        auto *L = state();

        const stack_guard guard(L);
        const root_scope root(L, registry_reference());

        if (_load(L, aLuaScript, aName) == LUA_OK) return {};

        return _to_string(L, -1).value_or("lua reported an error it could not describe");
    }
}
