// © Joseph Cameron - All Rights Reserved

#include <jfc/lua.h>

#include <iostream>
#include <sstream>
#include <string>

using namespace jfc::lua;

namespace {
    const std::string careless(R"V0G0N(
    -- forgot to advance the counter
    function think()
        local waited = 0

        while waited < 10 do
            -- waited = waited + 1
        end
    end

    -- meant to be bounded by the number of items nearby
    function hoard()
        loot = {}

        for i = 1, 1e9 do loot[i] = string.rep("gold", 1024) .. i end
    end
    )V0G0N");

    void heading(const char *const aTitle) { std::cout << "\n-- " << aTitle << "\n"; }
}

int main()
{
    interpreter interp(interpreter_policy{
        .MEMORY_BUDGET_IN_BYTES = 64u << 20,
        .MEMORY_GROWTH_BUDGET_IN_BYTES_PER_RUN = 8u << 20,
        .INSTRUCTION_BUDGET = 20000000,
        .ON_INSTRUCTION_BUDGET_EXHAUSTED = [](const std::size_t aExecuted)
        {
            std::cout << "    [limit] a script has run " << aExecuted
                << " instructions without finishing -- stopping it\n";

            return false;
        }});

    auto npc = interp.make_environment(environment_policy{
        .LIBRARY = standard_library::safe,
        .INSTRUCTION_BUDGET = 400000});

    auto bystander = interp.make_environment();

    if (const auto error = npc->run(careless)) { std::cout << "setup failed: " << *error << "\n"; return 1; }

    if (const auto error = bystander->run("hp = 100")) std::cout << "  error: " << *error << "\n";

    heading("a loop with no way out");

    if (const auto error = npc->call("think")) std::cout << "  stopped: " << *error << "\n";

    heading("a list that was meant to be short");

    if (const auto error = npc->call("hoard")) std::cout << "  stopped: " << *error << "\n";

    heading("neither took anything else with it");

    if (const auto error = npc->run("recovered = 'still here'"))
        std::cout << "  npc error: " << *error << "\n";

    std::cout << "  the npc still runs:      " << npc->read_string("recovered").value_or("?") << "\n"
              << "  the bystander is intact: " << bystander->read_number("hp").value_or(0) << "hp\n"
              << "  and the process is here to say so\n";

    heading("what the limits do and do not cover");

    std::cout << "  memory  bounds the lua heap, not the process. luajit compiles through its own\n"
                 "          allocator, so a trace is invisible to the ceiling\n"
                 "  time    counts vm instructions\n"
                 "  neither is per environment: they share one lua state, so the figures bound the\n"
                 "          interpreter and the per-run one says who is at fault\n";

    return EXIT_SUCCESS;
}
