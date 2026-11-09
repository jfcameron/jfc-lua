// © Joseph Cameron - MIT License

#ifndef JFC_LUA_TYPED_CALL_H
#define JFC_LUA_TYPED_CALL_H

#include <jfc/lua/data_table.h>
#include <jfc/lua/reference.h>
#include <jfc/lua/signature.h>
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
        //! aFirstArgument is how many values on the stack come before the first one read. aNoun is what those values are
        //! called in an error
        call_frame(lua_State *L, std::string_view aName, int aFirstArgument, const char *aNoun);

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

        //! every argument from aIndex on
        [[nodiscard]] value_list_type arguments(int aIndex) const;

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
        int m_FirstArgument;
        const char *m_Noun;

        //! the name and the position, for the start of an error
        [[nodiscard]] std::string _where(int aIndex) const;

        [[noreturn]] void _refuse(int aIndex, const std::string &aWanted) const;
    };

    /// \brief what a registered typed function becomes: reads, calls, pushes, and returns how many results it pushed
    using typed_closure_type = std::function<int(call_frame &)>;

    /// \brief what a registered method becomes: the same, given the object it was called on
    using typed_method_type = std::function<int(const userdata &, call_frame &)>;

    template <typename> inline constexpr bool always_false = false;

    template <typename> struct is_optional : std::false_type {};
    template <typename value_type> struct is_optional<std::optional<value_type>> : std::true_type {};

    template <typename> struct is_shared_ptr : std::false_type {};
    template <typename value_type> struct is_shared_ptr<std::shared_ptr<value_type>> : std::true_type {};

    template <typename> struct is_tuple : std::false_type {};
    template <typename... value_types> struct is_tuple<std::tuple<value_types...>> : std::true_type {};

    /// \brief a bound type's name, by its key, for a signature to say it
    using type_namer = std::function<std::string(const void *aTypeKey)>;

    /// \brief a registered function's signature, made only when it is asked for
    using signature_maker = signature (*)(const type_namer &aNameOf);

    template <typename value_type>
    [[nodiscard]] signature::value described(const type_namer &aNameOf) {
        if constexpr (is_optional<value_type>::value) {
            auto out = described<typename value_type::value_type>(aNameOf);

            out.optional = true;

            return out;
        }
        else if constexpr (std::is_same_v<value_type, bool>) return {"boolean"};
        else if constexpr (std::is_integral_v<value_type>) return {"integer"};
        else if constexpr (std::is_arithmetic_v<value_type>) return {"number"};
        else if constexpr (std::is_same_v<value_type, std::string> || std::is_same_v<value_type, std::string_view>
            || std::is_same_v<value_type, const char *>) return {"string"};
        else if constexpr (std::is_same_v<value_type, data_table> || std::is_same_v<value_type, table_view>)
            return {"table"};
        else if constexpr (std::is_same_v<value_type, function_reference>) return {"function"};
        else if constexpr (std::is_same_v<value_type, reference>) return {"value"};
        else if constexpr (std::is_same_v<value_type, userdata>) return {"userdata"};
        else if constexpr (std::is_same_v<value_type, std::nullptr_t>) return {"nil"};
        else if constexpr (is_shared_ptr<value_type>::value)
            return {aNameOf(&userdata_tag<std::remove_cv_t<typename value_type::element_type>>::id)};
        else static_assert(always_false<value_type>, "a signature says only what a typed function can take or give");
    }

    template <typename value_type>
    void describe_argument(signature &aOut, const type_namer &aNameOf) {
        if constexpr (std::is_same_v<value_type, value_list_type>) aOut.takes_rest = true;
        else aOut.arguments.push_back(described<value_type>(aNameOf));
    }

    template <typename value_type>
    void describe_result(signature &aOut, const type_namer &aNameOf) {
        if constexpr (std::is_same_v<value_type, value_list_type>) aOut.gives_rest = true;
        else aOut.results.push_back(described<value_type>(aNameOf));
    }

    template <typename result_type>
    void describe_results(signature &aOut, const type_namer &aNameOf) {
        using value_type = std::decay_t<result_type>;

        if constexpr (is_tuple<value_type>::value) {
            [&]<typename... element_types>(std::type_identity<std::tuple<element_types...>>) {
                (describe_result<std::decay_t<element_types>>(aOut, aNameOf), ...);
            }(std::type_identity<value_type>{});
        }
        else if constexpr (!std::is_void_v<value_type>) describe_result<value_type>(aOut, aNameOf);
    }

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
        else if constexpr (std::is_same_v<value_type, value_list_type>) return aFrame.arguments(aIndex);
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
            "function_reference, std::optional of those, or a value_list_type for every argument left");
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
            if constexpr (std::is_same_v<value_type, std::nullptr_t>) aFrame.push_nil();
            else if constexpr (std::is_same_v<value_type, bool>) aFrame.push(aValue);
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
            else static_assert(always_false<value_type>, "what goes to lua can be numbers, bool, nullptr, std::string, "
                "std::string_view, const char *, data_table, userdata, std::shared_ptr to a bound type, reference, "
                "function_reference, or a std::optional or std::tuple of those");

            return 1;
        }
    }

    /// \brief reads argument_types from the script, calls with them, and pushes what comes back
    template <typename result_type, typename... argument_types>
    struct typed_call {
        static_assert(((!std::is_lvalue_reference_v<argument_types>
            || std::is_const_v<std::remove_reference_t<argument_types>>) && ...),
            "a typed function's arguments come from the script, so a non-const reference has nothing to refer to");

    private:
        static constexpr std::size_t COUNT = sizeof...(argument_types);

        static constexpr std::size_t VALUE_LISTS
            = (std::size_t{0} + ... + std::is_same_v<std::remove_cvref_t<argument_types>, value_list_type>);

        //! a value list takes what is left, which leaves nothing to refuse
        static constexpr bool TAKES_THE_REST = VALUE_LISTS == 1 && std::is_same_v<std::remove_cvref_t<
            std::tuple_element_t<(COUNT > 0 ? COUNT - 1 : 0), std::tuple<argument_types..., void>>>, value_list_type>;

        static_assert(VALUE_LISTS == 0 || TAKES_THE_REST, "a value_list_type takes every argument left, so it comes last");

        template <typename call_type, std::size_t... indices>
        [[nodiscard]] static int run(call_type &&aCall, call_frame &aFrame, std::index_sequence<indices...>) {
            if constexpr (!TAKES_THE_REST) aFrame.expect_at_most(static_cast<int>(COUNT));

            // braces, so that arguments are read left to right and the first wrong one is the one reported
            std::tuple<std::remove_cvref_t<argument_types>...> held{
                read_argument<std::remove_cvref_t<argument_types>>(aFrame, static_cast<int>(indices) + 1)...};

            const auto guarded = [&]() -> decltype(auto) {
                try { return std::apply(aCall, std::move(held)); }
                catch (const std::exception &) { throw; }
                catch (...) { aFrame.throw_unknown(); }
            };

            if constexpr (std::is_void_v<result_type>) {
                guarded();

                return 0;
            }
            else return push_result<std::decay_t<result_type>>(aFrame, guarded());
        }

    public:
        template <typename call_type>
        [[nodiscard]] static int call(call_type &&aCall, call_frame &aFrame) {
            return run(std::forward<call_type>(aCall), aFrame, std::index_sequence_for<argument_types...>{});
        }

        //! what a call takes and gives, as its signature says
        [[nodiscard]] static signature describe(const type_namer &aNameOf) {
            signature out;

            (describe_argument<std::remove_cvref_t<argument_types>>(out, aNameOf), ...);

            describe_results<result_type>(out, aNameOf);

            return out;
        }
    };

    template <typename> struct typed_signature;

    template <typename result_type, typename... argument_types>
    struct typed_signature<std::function<result_type(argument_types...)>> final {
        using call_type = typed_call<result_type, argument_types...>;
    };

    template <typename object_type, typename> struct typed_method_signature;

    template <typename object_type, typename result_type, typename self_type, typename... argument_types>
    struct typed_method_signature<object_type, std::function<result_type(self_type, argument_types...)>> final {
        static_assert(std::is_reference_v<self_type> && std::is_same_v<std::remove_cvref_t<self_type>, object_type>,
            "a typed method's first parameter is a reference to the object it is called on");

        using call_type = typed_call<result_type, argument_types...>;
    };

    template <typename function_type>
    concept has_one_signature = requires (std::decay_t<function_type> &aFunction) { std::function{aFunction}; };

    template <typename function_type>
    using signature_of = decltype(std::function{std::declval<std::decay_t<function_type> &>()});

    template <typename function_type>
    [[nodiscard]] typed_closure_type make_typed_closure(function_type &&aFunction) {
        if constexpr (has_one_signature<function_type>) {
            using call_type = typename typed_signature<signature_of<function_type>>::call_type;

            return [function = std::decay_t<function_type>(std::forward<function_type>(aFunction))](
                call_frame &aFrame) mutable { return call_type::call(function, aFrame); };
        }
        else static_assert(always_false<function_type>, "a typed function's signature says what to read from the script, "
            "so it cannot be a generic lambda or an overloaded function object");
    }

    template <typename object_type, typename function_type>
    [[nodiscard]] typed_method_type make_typed_method(function_type &&aFunction) {
        if constexpr (has_one_signature<function_type>) {
            using call_type = typename typed_method_signature<object_type, signature_of<function_type>>::call_type;

            return [function = std::decay_t<function_type>(std::forward<function_type>(aFunction))](
                const userdata &aSelf, call_frame &aFrame) mutable {
                    const auto object = aSelf.get<object_type>();

                    return call_type::call([&](auto &&...aArguments) -> decltype(auto) {
                        return function(*object, std::forward<decltype(aArguments)>(aArguments)...);
                    }, aFrame);
                };
        }
        else static_assert(always_false<function_type>, "a typed method's signature says what to read from the script, "
            "so it cannot be a generic lambda or an overloaded function object");
    }

    //! how to say a typed function's signature; nothing for one make_typed_closure refuses
    template <typename function_type>
    [[nodiscard]] constexpr signature_maker signature_maker_of() {
        if constexpr (has_one_signature<function_type>)
            return &typed_signature<signature_of<function_type>>::call_type::describe;
        else return nullptr;
    }

    //! how to say a typed method's signature, after the object it is called on
    template <typename object_type, typename function_type>
    [[nodiscard]] constexpr signature_maker method_signature_maker_of() {
        if constexpr (has_one_signature<function_type>)
            return &typed_method_signature<object_type, signature_of<function_type>>::call_type::describe;
        else return nullptr;
    }

    template <typename> inline constexpr bool borrows_the_stack = false;
    template <> inline constexpr bool borrows_the_stack<std::string_view> = true;
    template <> inline constexpr bool borrows_the_stack<table_view> = true;
    template <typename value_type> inline constexpr bool borrows_the_stack<std::optional<value_type>>
        = borrows_the_stack<value_type>;
    template <typename... value_types> inline constexpr bool borrows_the_stack<std::tuple<value_types...>>
        = (borrows_the_stack<value_types> || ...);

    template <typename tuple_type, std::size_t... indices>
    [[nodiscard]] tuple_type read_tuple(const call_frame &aFrame, std::index_sequence<indices...>) {
        // braces, so that results are read left to right and the first wrong one is the one reported
        return tuple_type{read_argument<std::tuple_element_t<indices, tuple_type>>(aFrame, static_cast<int>(indices) + 1)...};
    }

    template <typename result_type>
    [[nodiscard]] result_type read_results(const call_frame &aFrame) {
        static_assert(!borrows_the_stack<result_type>,
            "a typed call's results leave the stack when it ends, so ask for a std::string or a data_table");

        if constexpr (is_tuple<result_type>::value)
            return read_tuple<result_type>(aFrame, std::make_index_sequence<std::tuple_size_v<result_type>>{});
        else return read_argument<result_type>(aFrame, 1);
    }

    template <typename work_type>
    [[nodiscard]] stack_work make_stack_work(work_type &aWork) {
        return {&aWork, [](void *const pContext, call_frame &aFrame) { (*static_cast<work_type *>(pContext))(aFrame); }};
    }

    /// \brief calls aCall with what pushes aArguments and what reads a result_type
    template <typename result_type, typename call_type, typename... argument_types>
    [[nodiscard]] call_outcome<result_type> typed_outcome(call_type &&aCall, const argument_types &...aArguments) {
        static_assert((!std::is_same_v<std::remove_cvref_t<argument_types>, value_list_type> && ...),
            "a typed call does not take a value_list_type: use the call that takes the list and its results");

        auto push = [&](call_frame &aFrame) {
            aFrame.reserve(sizeof...(argument_types));

            (static_cast<void>(push_result<std::decay_t<const argument_types>>(aFrame, aArguments)), ...);
        };

        if constexpr (std::is_void_v<result_type>) {
            auto read = [](call_frame &) {};

            return aCall(make_stack_work(push), make_stack_work(read));
        }
        else {
            std::optional<result_type> results;

            auto read = [&](call_frame &aFrame) { results.emplace(read_results<result_type>(aFrame)); };

            if (auto error = aCall(make_stack_work(push), make_stack_work(read)))
                return call_result<result_type>::failed(std::move(*error));

            return call_result<result_type>(std::move(*results));
        }
    }

    /// \see jfc::lua::method
    template <typename function_type>
    struct named_method final {
        std::string name;
        function_type function;
    };

    /// \brief a method for interpreter::register_type, made: what runs, and how to say its signature
    struct made_method final {
        std::string name;
        typed_method_type method;
        signature_maker describe;
    };
}

namespace jfc::lua
{
    /// \brief a method for interpreter::register_type, which says what it takes in its own signature
    ///
    /// Its first parameter is a reference to the object. What follows reads as a typed function's arguments do, numbered
    /// from the first after the object. \see environment::register_function
    template <typename function_type>
    [[nodiscard]] detail::named_method<std::decay_t<function_type>> method(std::string aName, function_type &&aFunction) {
        return {std::move(aName), std::forward<function_type>(aFunction)};
    }

    template <typename result_type, typename... argument_types>
    call_outcome<result_type> reference::call(const argument_types &...aArguments) {
        return detail::typed_outcome<result_type>([this](const detail::stack_work &aPush, const detail::stack_work &aRead)
            { return _call(aPush, aRead, {}); }, aArguments...);
    }

    template <typename result_type, typename... argument_types>
    call_outcome<result_type> function_reference::call(const argument_types &...aArguments) {
        return detail::typed_outcome<result_type>([this](const detail::stack_work &aPush, const detail::stack_work &aRead)
            { return _call(aPush, aRead, m_InstructionBudget); }, aArguments...);
    }
}

#endif
