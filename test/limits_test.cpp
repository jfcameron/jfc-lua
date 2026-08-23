// © Joseph Cameron - All Rights Reserved

#include <jfc/catch.hpp>

#include <jfc/lua.h>

#include <string>

using namespace jfc::lua;

TEST_CASE("a memory budget bounds what a script can allocate", "[limits]") {
    interpreter interp(interpreter_policy{8u << 20});

    auto env = interp.make_environment();

    SECTION("one enormous allocation fails instead of taking the process with it") {
        const auto error = env->run("s = string.rep('x', 50000000)");
        REQUIRE(error.has_value());
        REQUIRE(error->find("memory") != std::string::npos);
    }

    SECTION("so does growth without bound") {
        REQUIRE(env->run("t = {} for i = 1, 1e9 do t[i] = i end").has_value());
    }

    SECTION("and the interpreter is still usable afterwards") {
        REQUIRE(env->run("t = {} for i = 1, 1e9 do t[i] = i end").has_value());

        REQUIRE_FALSE(env->run("recovered = math.floor(2.7)").has_value());
        REQUIRE(env->read_number("recovered") == 2);
    }

    SECTION("ordinary work is untouched by the ceiling") {
        REQUIRE_FALSE(env->run(
            "local t = {}\n"
            "for i = 1, 10000 do t[i] = ('n%d'):format(i) end\n"
            "count = #t").has_value());

        REQUIRE(env->read_number("count") == 10000);
    }
}

TEST_CASE("no memory budget means no ceiling", "[limits]") {
    interpreter interp;

    auto env = interp.make_environment();

    REQUIRE_FALSE(env->run("s = string.rep('x', 50000000)  length = #s").has_value());
    REQUIRE(env->read_number("length") == 50000000);
}

TEST_CASE("an instruction budget interrupts a runaway script", "[limits]") {
    interpreter interp(interpreter_policy{
        0,     // MEMORY_BUDGET_IN_BYTES
        0,     // MEMORY_GROWTH_BUDGET_IN_BYTES_PER_RUN
        100000 // INSTRUCTION_BUDGET
    });

    auto env = interp.make_environment();

    SECTION("the loop with no way out is stopped") {
        const auto error = env->run("while true do end");

        REQUIRE(error.has_value());
        REQUIRE(error->find("instruction budget") != std::string::npos);
    }

    SECTION("so is one that allocates nothing and calls nothing") {
        REQUIRE(env->run("local i = 0 while true do i = i + 1 end").has_value());
    }

    SECTION("a script inside the budget finishes normally") {
        REQUIRE_FALSE(env->run("local s = 0 for i = 1, 100 do s = s + i end total = s").has_value());
        REQUIRE(env->read_number("total") == 5050);
    }

    SECTION("the budget restarts for each run rather than accumulating") {
        for (int i = 0; i < 20; ++i) {
            INFO("run " << i);

            REQUIRE_FALSE(env->run("local s = 0 for i = 1, 1000 do s = s + i end").has_value());
        }
    }

    SECTION("the interpreter is usable after a script is stopped") {
        REQUIRE(env->run("while true do end").has_value());

        REQUIRE_FALSE(env->run("recovered = 1").has_value());
        REQUIRE(env->read_number("recovered") == 1);
    }

    SECTION("other environments are unaffected by one being stopped") {
        auto other = interp.make_environment();

        REQUIRE_FALSE(other->run("hp = 100").has_value());
        REQUIRE(env->run("while true do end").has_value());

        REQUIRE_FALSE(other->run("hp = hp - 1").has_value());
        REQUIRE(other->read_number("hp") == 99);
    }
}

TEST_CASE("the callback decides what happens at the budget", "[limits]") {
    SECTION("returning false stops the script, and reports how far it got") {
        std::size_t reportedAt = 0;
        int calls = 0;

        interpreter interp(interpreter_policy{
            0,                                   // MEMORY_BUDGET_IN_BYTES
            0,                                   // MEMORY_GROWTH_BUDGET_IN_BYTES_PER_RUN
            50000,                               // INSTRUCTION_BUDGET
            [&](const std::size_t aExecuted)     // ON_INSTRUCTION_BUDGET_EXHAUSTED
            {
                ++calls;
                reportedAt = aExecuted;

                return false;
            }
        });

        auto env = interp.make_environment();

        REQUIRE(env->run("while true do end").has_value());

        REQUIRE(calls == 1);
        REQUIRE(reportedAt > 50000);
    }

    SECTION("returning true grants another budget, so a caller can wait as long as it likes") {
        int calls = 0;

        interpreter interp(interpreter_policy{
            0,                              // MEMORY_BUDGET_IN_BYTES
            0,                              // MEMORY_GROWTH_BUDGET_IN_BYTES_PER_RUN
            50000,                          // INSTRUCTION_BUDGET
            [&calls](const std::size_t)     // ON_INSTRUCTION_BUDGET_EXHAUSTED
            {
                // three reprieves, then stop -- what a "still working, keep waiting?" prompt does
                return ++calls < 3;
            }
        });

        auto env = interp.make_environment();

        REQUIRE(env->run("while true do end").has_value());

        REQUIRE(calls == 3);
    }

    SECTION("it is not called at all by a script that stays inside its budget") {
        int calls = 0;

        interpreter interp(interpreter_policy{
            0,                                                       // MEMORY_BUDGET_IN_BYTES
            0,                                                       // MEMORY_GROWTH_BUDGET_IN_BYTES_PER_RUN
            1000000,                                                 // INSTRUCTION_BUDGET
            [&calls](const std::size_t) { ++calls; return false; }  // ON_INSTRUCTION_BUDGET_EXHAUSTED
        });

        auto env = interp.make_environment();

        REQUIRE_FALSE(env->run("local s = 0 for i = 1, 100 do s = s + i end").has_value());

        REQUIRE(calls == 0);
    }

    SECTION("an exception thrown out of it is treated as a decision to stop") {
        // It runs inside the vm, and a c++ exception cannot cross the longjmp lua raises errors with.
        interpreter interp(interpreter_policy{
            0,                                // MEMORY_BUDGET_IN_BYTES
            0,                                // MEMORY_GROWTH_BUDGET_IN_BYTES_PER_RUN
            50000,                            // INSTRUCTION_BUDGET
            [](const std::size_t) -> bool        // ON_INSTRUCTION_BUDGET_EXHAUSTED
            {
                throw std::runtime_error("from inside the callback");
            }
        });

        auto env = interp.make_environment();

        REQUIRE(env->run("while true do end").has_value());

        REQUIRE_FALSE(env->run("survived = 1").has_value());
        REQUIRE(env->read_number("survived") == 1);
    }
}

TEST_CASE("a nested run does not refill the caller's budget", "[limits]") {
    /// Otherwise `while true do call_into_another_environment() end` never reaches its budget: each
    /// inner run would reset the count the outer loop is being measured by.

    interpreter interp(interpreter_policy{
        0,       // MEMORY_BUDGET_IN_BYTES
        0,       // MEMORY_GROWTH_BUDGET_IN_BYTES_PER_RUN
        200000  // INSTRUCTION_BUDGET
    });

    auto outer = interp.make_environment();
    auto inner = interp.make_environment();

    // Recorded rather than asserted per call: this runs thousands of times before the budget is
    // reached, and asserting inside it would bury the suite's assertion count in identical records.
    int ran = 0, refused = 0;

    outer->register_function("tick", [inner, &ran, &refused](value_list_type) -> value_list_type {
        if (inner->run("ticks = (ticks or 0) + 1").has_value()) ++refused;
        else ++ran;

        return {};
    });

    const auto error = outer->run("while true do tick() end");

    REQUIRE(error.has_value());
    REQUIRE(error->find("instruction budget") != std::string::npos);

    // The inner environment really was being entered, so a reset of the outer count had every
    // opportunity to happen and did not.
    REQUIRE(ran > 1);
    REQUIRE(inner->read_number("ticks").value_or(0) > 1);

    // And it stopped *promptly*. This is the guard on the abort being absorbed rather than obeyed:
    // before nested runs were refused once the budget was reached, this loop passed two million
    // iterations on puc lua without ever ending, because every abort was raised inside the nested
    // run's own pcall and reported to the c++ caller instead of to the script that had overrun.
    //
    // Whether any call is actually refused depends on where the next check happens to land -- on
    // luajit the first abort reached the outer script directly and none were -- so the bound is the
    // assertion rather than the refusal count.
    INFO("served " << ran << ", refused " << refused);

    REQUIRE(ran + refused < 1000000);
}

TEST_CASE("a caller that says stop is asked once", "[limits]") {
    /// Once the callback has declined, the check keeps raising rather than starting the count again,
    /// so the decision is not re-put on every subsequent check. Without that, a script calling into
    /// another environment gets its abort caught by the nested run's pcall, carries on, overruns
    /// again, and asks again -- which for a "this is taking a while, keep waiting?" prompt means the
    /// user answers no and is asked repeatedly anyway.

    int calls = 0;

    interpreter interp(interpreter_policy{
        0,                                                       // MEMORY_BUDGET_IN_BYTES
        0,                                                       // MEMORY_GROWTH_BUDGET_IN_BYTES_PER_RUN
        100000,                                                  // INSTRUCTION_BUDGET
        [&calls](const std::size_t) { ++calls; return false; }  // ON_INSTRUCTION_BUDGET_EXHAUSTED
    });

    auto outer = interp.make_environment();
    auto inner = interp.make_environment();

    outer->register_function("tick", [inner](value_list_type) -> value_list_type {
        (void)inner->run("ticks = (ticks or 0) + 1");

        return {};
    });

    REQUIRE(outer->run("while true do tick() end").has_value());

    REQUIRE(calls == 1);
}

TEST_CASE("the two budgets are independent", "[limits]") {
    SECTION("a memory ceiling does not bound time") {
        interpreter interp(interpreter_policy{
            8u << 20,  // MEMORY_BUDGET_IN_BYTES
            0,         // MEMORY_GROWTH_BUDGET_IN_BYTES_PER_RUN
            200000    // INSTRUCTION_BUDGET
        });

        auto env = interp.make_environment();

        // allocates nothing, so only the instruction budget can stop it
        const auto error = env->run("local i = 0 while true do i = i + 1 end");

        REQUIRE(error.has_value());
        REQUIRE(error->find("instruction budget") != std::string::npos);
    }

    SECTION("an instruction budget does not bound memory") {
        // one c call, so no vm instructions run inside it and the hook cannot fire
        interpreter interp(interpreter_policy{
            8u << 20,   // MEMORY_BUDGET_IN_BYTES
            0,          // MEMORY_GROWTH_BUDGET_IN_BYTES_PER_RUN
            100000000  // INSTRUCTION_BUDGET
        });

        auto env = interp.make_environment();

        const auto error = env->run("s = string.rep('x', 50000000)");

        REQUIRE(error.has_value());
        REQUIRE(error->find("memory") != std::string::npos);
    }
}

TEST_CASE("a per-run growth budget localises a runaway allocation", "[limits]") {
    /// The cheap half of localisation. It bounds how much one call to run may *add* to the heap,
    /// rather than tallying what each environment owns -- which is why it costs a comparison instead
    /// of an owner tag on every allocated block.

    interpreter interp(interpreter_policy{
        0,         // MEMORY_BUDGET_IN_BYTES
        4u << 20  // MEMORY_GROWTH_BUDGET_IN_BYTES_PER_RUN
    });

    auto greedy = interp.make_environment();
    auto bystander = interp.make_environment();

    SECTION("the run that overreaches is the one that fails") {
        const auto error = greedy->run("t = {} for i = 1, 1e9 do t[i] = i end");

        REQUIRE(error.has_value());
        REQUIRE(error->find("memory") != std::string::npos);
    }

    SECTION("and every other environment still has its headroom") {
        REQUIRE(greedy->run("t = {} for i = 1, 1e9 do t[i] = i end").has_value());

        REQUIRE_FALSE(bystander->run("t = {} for i = 1, 20000 do t[i] = i end  n = #t").has_value());
        REQUIRE(bystander->read_number("n") == 20000);
    }

    SECTION("the budget is per run, so the same script can be run again") {
        for (int i = 0; i < 5; ++i) {
            INFO("run " << i);

            REQUIRE_FALSE(greedy->run("local t = {} for i = 1, 20000 do t[i] = i end").has_value());
        }
    }

    SECTION("growth accumulated across runs does not count against any one of them") {
        for (int i = 0; i < 20; ++i) {
            INFO("run " << i);

            REQUIRE_FALSE(greedy->run(
                "kept = kept or {}\n"
                "local t = {}\n"
                "for j = 1, 40000 do t[j] = j end\n"
                "kept[#kept + 1] = t").has_value());
        }

        REQUIRE_FALSE(greedy->run("held = #kept").has_value());
        REQUIRE(greedy->read_number("held") == 20);
    }

    SECTION("a run that frees as much as it takes is not spending anything") {
        REQUIRE_FALSE(greedy->run(
            "for i = 1, 200 do local t = {} for j = 1, 2000 do t[j] = j end end\n"
            "done = true").has_value());

        REQUIRE(greedy->read_boolean("done") == true);
    }

    SECTION("it is unrelated to the interpreter-wide ceiling") {
        REQUIRE(greedy->run("t = {} for i = 1, 1e9 do t[i] = i end").has_value());
    }
}

TEST_CASE("an environment can carry its own instruction budget", "[limits]") {
    SECTION("a tight environment is stopped where a generous one is not") {
        interpreter interp;

        auto npc = interp.make_environment(environment_policy{
            standard_library::safe,  // LIBRARY
            50000                   // INSTRUCTION_BUDGET
        });
        auto loader = interp.make_environment(environment_policy{
            standard_library::safe,  // LIBRARY
            100000000               // INSTRUCTION_BUDGET
        });

        REQUIRE(npc->run("local i = 0 while i < 1e9 do i = i + 1 end").has_value());

        REQUIRE_FALSE(loader->run("local i = 0 while i < 200000 do i = i + 1 end  done = true")
            .has_value());
        REQUIRE(loader->read_boolean("done") == true);
    }

    SECTION("an environment with no figure of its own uses the interpreter's") {
        interpreter interp(interpreter_policy{
            0,      // MEMORY_BUDGET_IN_BYTES
            0,      // MEMORY_GROWTH_BUDGET_IN_BYTES_PER_RUN
            50000  // INSTRUCTION_BUDGET
        });

        auto inherited = interp.make_environment();

        REQUIRE(inherited->run("while true do end").has_value());
    }

    SECTION("and can override the interpreter's in either direction") {
        interpreter interp(interpreter_policy{
            0,      // MEMORY_BUDGET_IN_BYTES
            0,      // MEMORY_GROWTH_BUDGET_IN_BYTES_PER_RUN
            50000  // INSTRUCTION_BUDGET
        });

        auto allowed = interp.make_environment(environment_policy{
            standard_library::safe,  // LIBRARY
            0                       // INSTRUCTION_BUDGET
            });   // 0 means unbounded here

            REQUIRE_FALSE(allowed->run("local i = 0 while i < 500000 do i = i + 1 end  done = true")
                .has_value());
            REQUIRE(allowed->read_boolean("done") == true);
            }

            SECTION("an interpreter with no budget still honours an environment that asks for one")
            {
            // The hook has to appear when the environment is made rather than when the interpreter was.
            interpreter interp;

            auto unbounded = interp.make_environment();
            auto bounded = interp.make_environment(environment_policy{
            standard_library::safe,  // LIBRARY
            50000                   // INSTRUCTION_BUDGET
        });

        REQUIRE(bounded->run("while true do end").has_value());

        REQUIRE_FALSE(unbounded->run("local i = 0 while i < 500000 do i = i + 1 end  done = true")
            .has_value());
        REQUIRE(unbounded->read_boolean("done") == true);
    }

    SECTION("the library set and the budget are set together") {
        interpreter interp;

        auto env = interp.make_environment(environment_policy{
            standard_library::unrestricted,  // LIBRARY
            50000                           // INSTRUCTION_BUDGET
        });

        REQUIRE_FALSE(env->run("probe = tostring(io)").has_value());
        REQUIRE(env->read_string("probe") != "nil");

        REQUIRE(env->run("while true do end").has_value());
    }
}

TEST_CASE("the outermost run's instruction budget governs a nested one", "[limits]") {
    // The script being measured is the one that has been running, not the one it called into. A tight
    // npc calling a generous loader must not inherit the loader's allowance for its own loop.
    interpreter interp;

    auto npc = interp.make_environment(environment_policy{
        standard_library::safe,  // LIBRARY
        100000                  // INSTRUCTION_BUDGET
    });
    auto loader = interp.make_environment(environment_policy{
        standard_library::safe,  // LIBRARY
        100000000               // INSTRUCTION_BUDGET
    });

    int served = 0;

    npc->register_function("load_something", [loader, &served](value_list_type) -> value_list_type {
        if (!loader->run("work = (work or 0) + 1").has_value()) ++served;

        return {};
    });

    REQUIRE(npc->run("while true do load_something() end").has_value());

    REQUIRE(served > 1);
}

#ifdef JFC_LUA_LUAJIT
TEST_CASE("a budgeted environment does not cost the rest of the state its jit", "[limits]") {
    interpreter interp(interpreter_policy{
        0,        // MEMORY_BUDGET_IN_BYTES
        0,        // MEMORY_GROWTH_BUDGET_IN_BYTES_PER_RUN
        100000    // INSTRUCTION_BUDGET
    });

    auto budgeted = interp.make_environment();
    auto observer = interp.make_environment(environment_policy{standard_library::unrestricted});

    REQUIRE_FALSE(observer->run("before = jit.status()").has_value());
    REQUIRE(observer->read_boolean("before") == true);

    // a script is measured, and stopped
    REQUIRE(budgeted->run("while true do end").has_value());

    // and the compiler is still there for everyone else
    REQUIRE_FALSE(observer->run("after = jit.status()").has_value());
    REQUIRE(observer->read_boolean("after") == true);
}
#endif
