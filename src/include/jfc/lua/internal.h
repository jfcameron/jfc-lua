// © Joseph Cameron - All Rights Reserved

#ifndef JFC_LUA_INTERNAL_H
#define JFC_LUA_INTERNAL_H

#include <jfc/lua/reference.h>
#include <jfc/lua/userdata.h>
#include <jfc/lua/types.h>

#include <lua.hpp>

#ifndef LUA_OK
    #define LUA_OK 0
#endif

#include <cstddef>
#include <functional>
#include <optional>

namespace jfc::lua {
    /// \brief what the allocator and the instruction hook share
    struct interpreter_limits final {
        //! bytes currently held by this state
        std::size_t used{0};

        //! ceiling on `used`, or zero for unbounded
        std::size_t cap{0};

        //! instructions one outermost run may execute, or zero for unbounded
        std::size_t instructionBudget{0};

        //! instructions the current outermost run has executed
        std::size_t instructionsSeen{0};

        //! bytes one outermost run may add to the heap, or zero for unbounded
        std::size_t growthCap{0};

        //! what `used` stood at when the outermost run began
        std::size_t usedAtRunStart{0};

        //! how many runs are on the stack, so only the outermost resets the count
        int depth{0};

        //! registry reference to luajit's jit.off, or LUA_NOREF where there is no jit
        int jitOffReference{-2};   // LUA_NOREF

        //! whether the instruction hook has been installed yet
        bool hookInstalled{false};

        //! whether this run has already been told to stop
        bool aborted{false};

        //! what to do when the budget is reached; empty means stop
        std::function<bool(const std::size_t)> onExhausted;
    };
}

namespace jfc::lua::detail {
    /// \brief the value at aIndex, appended to aOut; false for a value that cannot cross into c++
    bool to_param(lua_State *L, int aIndex, value_list_type &aOut);

    //! push a value that crossed from c++
    void push_param(lua_State *L, const value_list_type::value_type &aValue);

    /// \brief push the lua value an object is: the one it already is, if anything still holds that
    /// one, or a new one. \see interpreter::register_type
    void push_userdata(lua_State *L, const userdata &aValue);

    /// \brief the object a value at aIndex boxes, or null if it is not a box this library made
    [[nodiscard]] const userdata *to_userdata(lua_State *L, int aIndex);

    /// \brief whether the value at aIndex is a box whose type has methods and fields to index
    [[nodiscard]] bool indexable_userdata(lua_State *L, int aIndex);

    /// \brief push the table a box's own fields are kept in, to write the key at aKey into
    [[nodiscard]] bool push_field_table(lua_State *L, int aBox, int aKey);
}

namespace {
    /// \brief restores the lua stack to the depth it was at on construction
    class stack_guard final {
        lua_State *m_pState;
        int m_Depth;

    public:
        stack_guard(const stack_guard &) = delete;
        stack_guard &operator=(const stack_guard &) = delete;

        stack_guard(lua_State *const aState)
        : m_pState(aState)
        , m_Depth(lua_gettop(aState))
        {}

        ~stack_guard() { lua_settop(m_pState, m_Depth); }
    };

    /// \brief makes an environment's table the globals for as long as it lives
    class root_scope final {
        lua_State *m_pState;
        int m_SavedIndex;

    public:
        root_scope(const root_scope &) = delete;
        root_scope &operator=(const root_scope &) = delete;

        root_scope(lua_State *const aState, const int aRootReference)
        : m_pState(aState) {
            lua_pushvalue(aState, LUA_GLOBALSINDEX);

            m_SavedIndex = lua_gettop(aState);

            lua_rawgeti(aState, LUA_REGISTRYINDEX, aRootReference);
            lua_replace(aState, LUA_GLOBALSINDEX);
        }

        ~root_scope() {
            lua_pushvalue(m_pState, m_SavedIndex);
            lua_replace(m_pState, LUA_GLOBALSINDEX);
        }
    };


    /// \brief one outermost run's share of the instruction and memory budgets
    class budget_scope final {
        jfc::lua::interpreter_limits *m_pLimits;
        std::size_t m_OutermostBudget;
        bool m_Outermost;

    public:
        budget_scope(const budget_scope &) = delete;
        budget_scope &operator=(const budget_scope &) = delete;

        budget_scope(lua_State *const aState, const std::optional<std::size_t> aBudgetOverride)
        : m_pLimits(nullptr)
        , m_OutermostBudget(0)
        , m_Outermost(false) {
            void *ud = nullptr;

            lua_getallocf(aState, &ud);

            m_pLimits = static_cast<jfc::lua::interpreter_limits *>(ud);

            if (!m_pLimits) return;

            m_Outermost = m_pLimits->depth++ == 0;

            if (!m_Outermost) return;

            m_pLimits->instructionsSeen = 0;
            m_pLimits->aborted = false;
            m_pLimits->usedAtRunStart = m_pLimits->used;

            m_OutermostBudget = m_pLimits->instructionBudget;

            if (aBudgetOverride) m_pLimits->instructionBudget = *aBudgetOverride;
        }

        ~budget_scope() {
            if (!m_pLimits) return;

            --m_pLimits->depth;

            if (m_Outermost) m_pLimits->instructionBudget = m_OutermostBudget;
        }

        [[nodiscard]] bool aborted() const { return m_pLimits && m_pLimits->aborted; }

        [[nodiscard]] std::size_t instruction_budget() const
            { return m_pLimits ? m_pLimits->instructionBudget : 0; }

        [[nodiscard]] jfc::lua::interpreter_limits *limits() const { return m_pLimits; }
    };

}

#endif
