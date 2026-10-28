// © Joseph Cameron - MIT License

#include <jfc/lua/internal.h>

#include <jfc/lua/coroutine.h>
#include <jfc/lua/exception.h>

#include <string>
#include <utility>

namespace jfc::lua {
    struct coroutine::shared_state final {
        status state = status::suspended;

        bool running = false;

        std::optional<std::size_t> instructionBudget;
    };

    coroutine::coroutine(reference aThread, const std::optional<std::size_t> aInstructionBudget)
    : m_Thread(std::move(aThread))
    , m_pShared(std::make_shared<shared_state>()) {
        m_pShared->instructionBudget = aInstructionBudget;
    }

    coroutine::status coroutine::state() const { return m_pShared->state; }

    error_type coroutine::resume(const value_list_type &aArguments, value_list_type &aResults) {
        aResults.clear();

        auto &shared = *m_pShared;

        if (shared.state == status::finished) return "the coroutine has finished; it cannot be resumed";
        if (shared.state == status::failed) return "the coroutine has failed; it cannot be resumed";

        if (shared.running) return "the coroutine is already running: it cannot resume itself";

        auto *const L = m_Thread.state();

        lua_State *thread = nullptr;

        {
            const stack_guard guard(L);

            lua_rawgeti(L, LUA_REGISTRYINDEX, m_Thread.registry_reference());

            thread = lua_tothread(L, -1);
        }

        if (!thread) return "the coroutine's thread is gone";

        const budget_scope budget(thread, shared.instructionBudget);

        if (budget.aborted()) return "script exceeded its instruction budget";

        const int argumentCount = static_cast<int>(aArguments.size());

        if (auto *const limits = budget.limits()) limits->unbudgeted = true;

        const bool room = lua_checkstack(thread, argumentCount + 1);

        if (auto *const limits = budget.limits()) limits->unbudgeted = false;

        if (!room) return "the lua stack cannot grow enough to resume with this many values";

        try {
            bool pushed = false;

            if (const auto error = detail::protect(L, [&](lua_State *const p) {
                if (!lua_checkstack(p, argumentCount)) return;

                for (const auto &argument : aArguments) detail::push_param(p, argument);

                lua_xmove(p, thread, argumentCount);

                pushed = true;
            })) return "could not resume the coroutine: " + *error;

            if (!pushed) return "the lua stack cannot grow enough to resume with this many values";
        }
        catch (const exception &e) { return "could not resume the coroutine: " + std::string(e.what()); }

        shared.running = true;

        const int result = lua_resume(thread, argumentCount);

        shared.running = false;

        if (result != LUA_OK && result != LUA_YIELD) {
            shared.state = status::failed;

            error_type error = "lua reported an error it could not describe";

            if (const auto unread = detail::protect(L, [&](lua_State *const p) {
                lua_xmove(thread, p, 1);

                if (const auto *const message = lua_tostring(p, -1)) error = message;
            })) error = unread;

            lua_settop(thread, 0);

            return error;
        }

        const int resultCount = lua_gettop(thread);

        error_type error;

        if (const auto unread = detail::protect(L, [&](lua_State *const p) {
            if (!lua_checkstack(p, resultCount)) {
                error = "the coroutine gave more values than the lua stack can hold";

                return;
            }

            lua_xmove(thread, p, resultCount);

            const int base = lua_gettop(p) - resultCount;

            for (int i = 1; i <= resultCount; ++i) {
                const auto unconverted = [&](const std::string &aWhy) {
                    error = std::string(result == LUA_YIELD ? "the coroutine yielded" : "the coroutine returned")
                        + " a " + lua_typename(p, lua_type(p, base + i)) + " in position " + std::to_string(i) + aWhy;
                };

                try {
                    if (!detail::to_param(p, base + i, aResults)) return unconverted(", which cannot cross into c++");
                }
                catch (const exception &e) { return unconverted(" that cannot cross into c++: " + std::string(e.what())); }
            }
        })) error = "could not read what the coroutine gave: " + *unread;

        lua_settop(thread, 0);

        if (error) {
            aResults.clear();

            shared.state = status::failed;

            return error;
        }

        shared.state = result == LUA_YIELD ? status::suspended : status::finished;

        return {};
    }

    error_type coroutine::resume(const value_list_type &aArguments) {
        value_list_type discarded;

        return resume(aArguments, discarded);
    }
}
