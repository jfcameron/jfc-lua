// © Joseph Cameron - All Rights Reserved

#include <jfc/lua/internal.h>

#include <jfc/lua/coroutine.h>

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

        if (!lua_checkstack(thread, static_cast<int>(aArguments.size()) + 1))
            return "the lua stack cannot grow enough to resume with this many values";

        for (const auto &argument : aArguments) detail::push_param(thread, argument);

        shared.running = true;

        const int result = lua_resume(thread, static_cast<int>(aArguments.size()));

        shared.running = false;

        if (result != LUA_OK && result != LUA_YIELD) {
            shared.state = status::failed;

            const auto *const message = lua_tostring(thread, -1);

            error_type error = std::string(message ? message
                : "lua reported an error it could not describe");

            lua_settop(thread, 0);

            return error;
        }

        const int count = lua_gettop(thread);

        for (int i = 1; i <= count; ++i)
            if (!detail::to_param(thread, i, aResults)) {
                const std::string type = lua_typename(thread, lua_type(thread, i));

                aResults.clear();

                lua_settop(thread, 0);

                shared.state = status::failed;

                return std::string(result == LUA_YIELD ? "the coroutine yielded" : "the coroutine returned")
                    + " a " + type + " in position " + std::to_string(i)
                    + ", which cannot cross into c++";
            }

        lua_settop(thread, 0);

        shared.state = result == LUA_YIELD ? status::suspended : status::finished;

        return {};
    }

    error_type coroutine::resume(const value_list_type &aArguments) {
        value_list_type discarded;

        return resume(aArguments, discarded);
    }
}
