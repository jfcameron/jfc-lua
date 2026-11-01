// © Joseph Cameron - MIT License

#ifndef JFC_LUA_TYPED_CALL_H
#define JFC_LUA_TYPED_CALL_H

#include <jfc/lua/data_table.h>
#include <jfc/lua/reference.h>
#include <jfc/lua/table_view.h>
#include <jfc/lua/types.h>
#include <jfc/lua/userdata.h>

#include <cstddef>
#include <exception>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace jfc::lua::detail
{
    /// \brief one call a script made to a typed function: how its template reads arguments and pushes results without
    /// lua.h. A read that fails throws an exception naming the function, the argument and what was wanted.
    class call_frame final
    {
    public:
        call_frame(lua_State *L, std::string_view aName, std::optional<std::size_t> aInstructionBudget);

        //! refuses a call given more than aCount arguments
        void expect_at_most(int aCount) const;

        //! whether the argument was left out or is nil
        [[nodiscard]] bool absent(int aIndex) const;

        [[nodiscard]] double number(int aIndex) const;

        //! a number with no fraction that fits an integer of aDigits value bits
        [[nodiscard]] double whole(int aIndex, int aDigits, bool aSigned) const;

        [[nodiscard]] bool boolean(int aIndex) const;

        //! valid for the call: the string stays on the stack underneath it
        [[nodiscard]] std::string_view text(int aIndex) const;

        [[nodiscard]] data_table table(int aIndex) const;

        //! the table in place
        [[nodiscard]] table_view view(int aIndex) const;

        //! any value but nil, kept alive
        [[nodiscard]] reference value(int aIndex) const;

        //! a function, kept alive
        [[nodiscard]] function_reference function(int aIndex) const;

        [[nodiscard]] userdata object(int aIndex) const;

        //! an object of the bound type aTypeKey
        [[nodiscard]] userdata object(int aIndex, const void *aTypeKey) const;

        //! makes room for aCount more results
        void reserve(std::size_t aCount);

        void push_nil();
        void push(double aValue);
        void push(bool aValue);
        void push(std::string_view aValue);
        void push(const data_table &aValue);
        void push(const userdata &aValue);
        void push(const reference &aValue);

        //! \return how many values were pushed
        [[nodiscard]] int push(const value_list_type &aValues);

        //! reports that the function threw something that is not a std::exception
        [[noreturn]] void throw_unknown() const;

    private:
        lua_State *m_pState;
        std::string_view m_Name;
        std::optional<std::size_t> m_InstructionBudget;

        [[noreturn]] void _refuse(int aIndex, const std::string &aWanted) const;
    };

    /// \brief what a registered typed function becomes: reads, calls, pushes, and returns how many results it pushed
    using typed_closure_type = std::function<int(call_frame &)>;

    template <typename> inline constexpr bool always_false = false;

    template <typename> struct is_optional : std::false_type {};
    template <typename value_type> struct is_optional<std::optional<value_type>> : std::true_type {};

    template <typename> struct is_shared_ptr : std::false_type {};
    template <typename value_type> struct is_shared_ptr<std::shared_ptr<value_type>> : std::true_type {};

    template <typename> struct is_tuple : std::false_type {};
    template <typename... value_types> struct is_tuple<std::tuple<value_types...>> : std::true_type {};

    template <typename value_type>
    [[nodiscard]] value_type read_argument(const call_frame &aFrame, const int aIndex) {
        if constexpr (std::is_same_v<value_type, bool>) return aFrame.boolean(aIndex);
        else if constexpr (std::is_integral_v<value_type>) {
            using limits = std::numeric_limits<value_type>;

            return static_cast<value_type>(aFrame.whole(aIndex, limits::digits, limits::is_signed));
        }
        else if constexpr (std::is_floating_point_v<value_type>) return static_cast<value_type>(aFrame.number(aIndex));
        else if constexpr (std::is_same_v<value_type, std::string_view>) return aFrame.text(aIndex);
        else if constexpr (std::is_same_v<value_type, std::string>) return std::string(aFrame.text(aIndex));
        else if constexpr (std::is_same_v<value_type, data_table>) return aFrame.table(aIndex);
        else if constexpr (std::is_same_v<value_type, table_view>) return aFrame.view(aIndex);
        else if constexpr (std::is_same_v<value_type, reference>) return aFrame.value(aIndex);
        else if constexpr (std::is_same_v<value_type, function_reference>) return aFrame.function(aIndex);
        else if constexpr (std::is_same_v<value_type, userdata>) return aFrame.object(aIndex);
        else if constexpr (is_shared_ptr<value_type>::value) {
            using object_type = typename value_type::element_type;

            return aFrame.object(aIndex, &userdata_tag<std::remove_cv_t<object_type>>::id).template get<object_type>();
        }
        else if constexpr (is_optional<value_type>::value) {
            if (aFrame.absent(aIndex)) return value_type{};

            return value_type{read_argument<typename value_type::value_type>(aFrame, aIndex)};
        }
        else static_assert(always_false<value_type>, "a typed function's arguments can be numbers, bool, std::string, "
            "std::string_view, data_table, table_view, userdata, std::shared_ptr to a bound type, reference, "
            "function_reference, or std::optional of those");
    }

    /// \return how many values were pushed
    template <typename value_type>
    [[nodiscard]] int push_result(call_frame &aFrame, const value_type &aValue) {
        if constexpr (std::is_same_v<value_type, value_list_type>) return aFrame.push(aValue);
        else if constexpr (is_tuple<value_type>::value) {
            aFrame.reserve(std::tuple_size_v<value_type>);

            int pushed = 0;

            std::apply([&](const auto &...aHeld) { ((pushed += push_result(aFrame, aHeld)), ...); }, aValue);

            return pushed;
        }
        else if constexpr (is_optional<value_type>::value) {
            if (aValue) return push_result(aFrame, *aValue);

            aFrame.push_nil();

            return 1;
        }
        else {
            if constexpr (std::is_same_v<value_type, bool>) aFrame.push(aValue);
            else if constexpr (std::is_arithmetic_v<value_type>) aFrame.push(static_cast<double>(aValue));
            else if constexpr (std::is_same_v<value_type, std::string> || std::is_same_v<value_type, std::string_view>)
                aFrame.push(std::string_view(aValue));
            else if constexpr (std::is_same_v<value_type, const char *>) {
                if (aValue) aFrame.push(std::string_view(aValue));
                else aFrame.push_nil();
            }
            else if constexpr (std::is_same_v<value_type, data_table> || std::is_same_v<value_type, userdata>
                || std::is_same_v<value_type, reference> || std::is_same_v<value_type, function_reference>)
                aFrame.push(aValue);
            else if constexpr (is_shared_ptr<value_type>::value) {
                if (aValue) aFrame.push(userdata::make(aValue));
                else aFrame.push_nil();
            }
            else static_assert(always_false<value_type>, "a typed function can return nothing, numbers, bool, "
                "std::string, std::string_view, const char *, data_table, userdata, std::shared_ptr to a bound type, "
                "reference, function_reference, value_list_type, or a std::optional or std::tuple of those");

            return 1;
        }
    }

    template <typename> struct typed_signature;

    template <typename result_type, typename... argument_types>
    struct typed_signature<std::function<result_type(argument_types...)>> final {
        static_assert(((!std::is_lvalue_reference_v<argument_types>
            || std::is_const_v<std::remove_reference_t<argument_types>>) && ...),
            "a typed function's arguments come from the script, so a non-const reference has nothing to refer to");

        template <typename function_type>
        [[nodiscard]] static int call(function_type &aFunction, call_frame &aFrame) {
            return call(aFunction, aFrame, std::index_sequence_for<argument_types...>{});
        }

    private:
        template <typename function_type, std::size_t... indices>
        [[nodiscard]] static int call(function_type &aFunction, call_frame &aFrame, std::index_sequence<indices...>) {
            aFrame.expect_at_most(static_cast<int>(sizeof...(argument_types)));

            // braces, so that arguments are read left to right and the first wrong one is the one reported
            std::tuple<std::remove_cvref_t<argument_types>...> held{
                read_argument<std::remove_cvref_t<argument_types>>(aFrame, static_cast<int>(indices) + 1)...};

            const auto run = [&]() -> decltype(auto) {
                try { return std::apply(aFunction, std::move(held)); }
                catch (const std::exception &) { throw; }
                catch (...) { aFrame.throw_unknown(); }
            };

            if constexpr (std::is_void_v<result_type>) {
                run();

                return 0;
            }
            else return push_result<std::decay_t<result_type>>(aFrame, run());
        }
    };

    template <typename function_type>
    concept has_one_signature = requires (std::decay_t<function_type> &aFunction) { std::function{aFunction}; };

    template <typename function_type>
    [[nodiscard]] typed_closure_type make_typed_closure(function_type &&aFunction) {
        if constexpr (has_one_signature<function_type>) {
            using held_type = std::decay_t<function_type>;
            using signature_type = typed_signature<decltype(std::function{std::declval<held_type &>()})>;

            return [function = held_type(std::forward<function_type>(aFunction))](call_frame &aFrame) mutable
                { return signature_type::call(function, aFrame); };
        }
        else static_assert(always_false<function_type>, "a typed function's signature says what to read from the script, "
            "so it cannot be a generic lambda or an overloaded function object");
    }
}

#endif
