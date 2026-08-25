// © Joseph Cameron - All Rights Reserved

/// \file the parts of the implementation that more than one translation unit needs
#ifndef JFC_LUA_INTERNAL_H
#define JFC_LUA_INTERNAL_H

#include <jfc/lua/types.h>

#include <lua.hpp>

#ifndef LUA_OK
    #define LUA_OK 0
#endif

#include <cstddef>
#include <functional>

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
}

#endif
