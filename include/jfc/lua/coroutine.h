// © Joseph Cameron - All Rights Reserved

#ifndef JFC_LUA_COROUTINE_H
#define JFC_LUA_COROUTINE_H

#include <jfc/lua/reference.h>
#include <jfc/lua/types.h>

#include <cstddef>
#include <memory>
#include <optional>

namespace jfc::lua {
    /// \brief a lua function that can be stopped and resumed later
    class coroutine final {
    public:
        enum class status {
            suspended,  //!< not yet started, or yielded: resume carries on
            finished,   //!< returned: what it returned was the last resume's results
            failed      //!< raised an error, or ran out of budget: it can never be resumed
        };

        /// \brief run it until it next yields or returns
        [[nodiscard]] error_type resume(const value_list_type &aArguments,
            value_list_type &aResults);

        //! run it until it next yields or returns
        [[nodiscard]] error_type resume(const value_list_type &aArguments = {});

        [[nodiscard]] status state() const;

        //! whether it can be resumed: not finished and not failed
        [[nodiscard]] bool suspended() const { return state() == status::suspended; }

    private:
        friend class environment;

        struct shared_state;

        coroutine(reference aThread, std::optional<std::size_t> aInstructionBudget);

        reference m_Thread;

        std::shared_ptr<shared_state> m_pShared;
    };
}

#endif
