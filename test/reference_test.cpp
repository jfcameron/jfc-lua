// © Joseph Cameron - All Rights Reserved

#include <jfc/catch.hpp>

#include <jfc/lua.h>

#include <string>

using namespace jfc::lua;

TEST_CASE("a reference can be taken to any kind of value", "[reference]") {
    interpreter interp;

    auto env = interp.make_environment();

    REQUIRE_FALSE(env->run(
        "npc = { hp = 50 }\n"
        "function handler() return 1 end\n"
        "count = 7\n"
        "label = 'text'\n"
        "flag = true").has_value());

    SECTION("a table") {
        const auto held = env->read_reference("npc");

        REQUIRE(held.has_value());
        REQUIRE(held->is_table());
        REQUIRE_FALSE(held->is_function());
        REQUIRE(held->type_name() == "table");
    }

    SECTION("a function, which is the half that data_table can never carry") {
        const auto held = env->read_reference("handler");

        REQUIRE(held.has_value());
        REQUIRE(held->is_function());
        REQUIRE(held->type_name() == "function");
    }

    SECTION("and the plain kinds too") {
        REQUIRE(env->read_reference("count")->type_name() == "number");
        REQUIRE(env->read_reference("label")->type_name() == "string");
        REQUIRE(env->read_reference("flag")->type_name() == "boolean");
    }

    SECTION("through a path, not only at the top") {
        REQUIRE_FALSE(env->run("world = { region = { town = {} } }").has_value());

        REQUIRE(env->read_reference("world.region.town")->is_table());
    }

    SECTION("nothing comes back for a path that resolves to nothing") {
        REQUIRE_FALSE(env->read_reference("no.such.path").has_value());
        REQUIRE_FALSE(env->read_reference("missing").has_value());
    }
}

TEST_CASE("references compare by identity", "[reference]") {
    interpreter interp;

    auto env = interp.make_environment();

    REQUIRE_FALSE(env->run(
        "a = { x = 1 }\n"
        "b = { x = 1 }\n"
        "alias = a").has_value());

    auto first = env->read_reference("a");
    const auto second = env->read_reference("alias");
    const auto other = env->read_reference("b");

    SECTION("two references to one value are equal") {
        REQUIRE(*first == *second);
    }

    SECTION("two identical but separate values are not") {
        REQUIRE(*first != *other);
    }

    SECTION("a copy refers to the same value") {
        const auto copy = *first;

        REQUIRE(copy == *first);
    }

    SECTION("and a script cannot make two things claim to be one") {
        REQUIRE_FALSE(env->run(
            "meta = { __eq = function() return true end }\n"
            "p = setmetatable({}, meta)\n"
            "q = setmetatable({}, meta)\n"
            "lua_says = (p == q)").has_value());

        REQUIRE(env->read_boolean("lua_says") == true);
        REQUIRE(*env->read_reference("p") != *env->read_reference("q"));
    }
}

TEST_CASE("a reference is interpreter scoped, like a bound object", "[reference]") {
    interpreter interp;

    auto one = interp.make_environment();
    auto two = interp.make_environment();

    REQUIRE_FALSE(one->run("thing = { x = 1 }").has_value());

    SECTION("the other environment cannot see it by name") {
        REQUIRE_FALSE(two->read_reference("thing").has_value());
    }

    SECTION("but a reference taken in one is valid in the other") {
        const auto held = one->read_reference("thing");

        REQUIRE(held.has_value());
        REQUIRE(held->is_table());

        REQUIRE(*held == *one->read_reference("thing"));
    }
}

TEST_CASE("a reference keeps what it refers to, and its interpreter, alive", "[reference]") {
    SECTION("it outlives the environment it was taken from") {
        interpreter interp;

        auto held = interp.make_environment()->read_reference("_G");

        REQUIRE(held.has_value());
        REQUIRE(held->is_table());
    }

    SECTION("and the interpreter itself") {
        std::optional<reference> escaped;
        {
            interpreter interp;

            auto env = interp.make_environment();

            REQUIRE_FALSE(env->run("survivor = { x = 1 }").has_value());

            escaped = env->read_reference("survivor");
        }

        REQUIRE(escaped.has_value());
        REQUIRE(escaped->is_table());
        REQUIRE(escaped->type_name() == "table");
    }

    SECTION("the value it holds is not collected while it holds it") {
        interpreter interp;

        auto env = interp.make_environment();

        REQUIRE_FALSE(env->run("doomed = { marker = 'here' }").has_value());

        const auto held = env->read_reference("doomed");

        REQUIRE_FALSE(env->run("doomed = nil").has_value());
        REQUIRE_FALSE(interp.run("collectgarbage('collect')").has_value());

        REQUIRE(held->is_table());
    }
}

TEST_CASE("a reference to a function can be called", "[reference][call]") {
    interpreter interp;

    auto env = interp.make_environment();

    SECTION("the referent is invoked, arguments in and results out") {
        REQUIRE_FALSE(env->run("function add(a, b) return a + b, 'sum' end"));

        auto add = env->read_reference("add");

        REQUIRE(add);
        REQUIRE(add->is_function());

        value_list_type results;

        REQUIRE_FALSE(add->call({3.0, 4.0}, results));
        REQUIRE(results.size() == 2);
        REQUIRE(std::get<double>(results.at(0)) == 7);
        REQUIRE(std::get<std::string>(results.at(1)) == "sum");
    }

    SECTION("the results overload may be omitted") {
        REQUIRE_FALSE(env->run("count = 0 function bump() count = count + 1 end"));

        auto bump = env->read_reference("bump");

        REQUIRE(bump);
        REQUIRE_FALSE(bump->call());
        REQUIRE_FALSE(bump->call());

        REQUIRE(env->read_number("count") == 2);
    }

    SECTION("it is the function itself, so it reaches the environment it came from") {
        REQUIRE_FALSE(env->run("hp = 10 function hurt(n) hp = hp - n end"));

        auto hurt = env->read_reference("hurt");

        REQUIRE(hurt);
        REQUIRE_FALSE(hurt->call({3.0}));

        REQUIRE(env->read_number("hp") == 7);
    }

    SECTION("a handle survives the name being reassigned, which a path would not") {
        REQUIRE_FALSE(env->run("function greet() return 'first' end"));

        auto first = env->read_reference("greet");

        REQUIRE(first);
        REQUIRE_FALSE(env->run("function greet() return 'second' end"));

        value_list_type byHandle;
        value_list_type byPath;

        REQUIRE_FALSE(first->call({}, byHandle));
        REQUIRE_FALSE(env->call("greet", {}, byPath));

        REQUIRE(std::get<std::string>(byHandle.at(0)) == "first");
        REQUIRE(std::get<std::string>(byPath.at(0)) == "second");
    }
}

TEST_CASE("calling a reference reports what went wrong", "[reference][call]") {
    interpreter interp;

    auto env = interp.make_environment();

    SECTION("a reference that is not a function names what it is") {
        REQUIRE_FALSE(env->run("t = {} n = 5"));

        auto table = env->read_reference("t");
        auto number = env->read_reference("n");

        REQUIRE(table);
        REQUIRE(number);

        const auto tableError = table->call();
        const auto numberError = number->call();

        REQUIRE(tableError);
        REQUIRE(numberError);
        REQUIRE(*tableError == "the reference holds a table, not a function");
        REQUIRE(*numberError == "the reference holds a number, not a function");
    }

    SECTION("an error raised inside the function comes back rather than escaping") {
        REQUIRE_FALSE(env->run("function boom() error('from the script') end"));

        auto boom = env->read_reference("boom");

        REQUIRE(boom);

        const auto error = boom->call();

        REQUIRE(error);
        REQUIRE(error->find("from the script") != std::string::npos);
    }

    SECTION("a result that cannot cross is reported, and the results are left empty") {
        REQUIRE_FALSE(env->run("function gives_a_function() return 1, print end"));

        auto gives = env->read_reference("gives_a_function");

        REQUIRE(gives);

        value_list_type results;

        const auto error = gives->call({}, results);

        REQUIRE(error);
        REQUIRE(error->find("function") != std::string::npos);
        REQUIRE(results.empty());
    }

    SECTION("each call gets its own budget rather than sharing one") {
        int aborts = 0;

        interpreter budgeted(interpreter_policy{
            .MEMORY_BUDGET_IN_BYTES = 0,
            .MEMORY_GROWTH_BUDGET_IN_BYTES_PER_RUN = 0,
            .INSTRUCTION_BUDGET = 200000,
            .ON_INSTRUCTION_BUDGET_EXHAUSTED = [&](const std::size_t)
            {
                ++aborts;

                return false;
            }
        });

        auto env2 = budgeted.make_environment();

        REQUIRE_FALSE(env2->run("function work() local n = 0 for i = 1, 2000 do n = n + i end end"));

        auto work = env2->read_reference("work");

        REQUIRE(work);

        for (int i = 0; i < 200; ++i) REQUIRE_FALSE(work->call());

        REQUIRE(aborts == 0);
    }

    SECTION("the interpreter's instruction budget still applies") {
        bool reacted = false;

        interpreter budgeted(interpreter_policy{
            .MEMORY_BUDGET_IN_BYTES = 0,
            .MEMORY_GROWTH_BUDGET_IN_BYTES_PER_RUN = 0,
            .INSTRUCTION_BUDGET = 100000,
            .ON_INSTRUCTION_BUDGET_EXHAUSTED = [&](const std::size_t)
            {
                reacted = true;
                return false;
            }
        });

        auto env2 = budgeted.make_environment();

        REQUIRE_FALSE(env2->run("function spin() while true do end end"));

        auto spin = env2->read_reference("spin");

        REQUIRE(spin);

        const auto error = spin->call();

        REQUIRE(error);
        REQUIRE(reacted);
    }
}
