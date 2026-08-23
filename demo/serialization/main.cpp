// © Joseph Cameron - All Rights Reserved

#include <jfc/lua.h>

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace jfc::lua;

namespace {
    const std::string npc(R"V0G0N(
name = "bob"
hp = 42
inventory = { "broadsword", "shield", "potion" }
position = { x = 12.5, y = 0, z = -3 }

function describe() return name .. " on " .. hp .. "hp" end

-- what a save file would want of this npc
record = {
    name = name,
    hp = hp,
    inventory = inventory,
    position = position,
    describe = describe   -- a method: not something that can be written down
}
)V0G0N");

    void heading(const char *const aTitle) { std::cout << "\n-- " << aTitle << "\n"; }

    [[nodiscard]] std::string serialize(const data_table &a)
    {
        std::ostringstream out;

        out << a;

        return out.str();
    }
}

int main()
{
    interpreter interp;

    auto env = interp.make_environment();

    if (const auto error = env->run(npc)) { std::cout << "setup failed: " << *error << "\n"; return 1; }

    heading("a method in the record is refused, and named");

    try {
        const auto strict = env->read_data_table("record");

        std::cout << "  unexpectedly succeeded\n";
    }
    catch (const jfc::lua_exception &e) { std::cout << "  " << e.what() << "\n"; }

    heading("so is a table that contains itself");

    if (const auto error = env->run("record.self = record"))
        std::cout << "  error: " << *error << "\n";

    try {
        const auto strict = env->read_data_table("record");

        std::cout << "  unexpectedly succeeded\n";
    }
    catch (const jfc::lua_exception &e) { std::cout << "  " << e.what() << "\n"; }

    heading("asking for what can be stored instead");

    const auto saved = env->read_data_table("record", unsupported::skip);

    if (!saved) { std::cout << "  nothing came back\n"; return 1; }

    std::cout << "  kept " << saved->size() << ":";

    {
        std::vector<std::string> kept;

        for (const auto &field : saved->keys())
        {
            std::stringstream name;

            std::visit([&name](auto &&aKey) { name << aKey; }, field.value());

            kept.push_back(name.str());
        }

        for (const auto &field : kept) std::cout << " " << field;
    }

    std::cout << "\n  dropped:";

    for (const auto *const absent : {"describe", "self"})
        if (!saved->contains(absent)) std::cout << " " << absent;

    std::cout << "  (a function, and a loop)\n";

    std::cout << "\n  skip is lossy on purpose: what it stepped over is gone, and writing this back\n"
                 "  writes a record without it\n";

    heading("what a save file would hold");

    const auto text = serialize(*saved);

    std::cout << "  " << text.substr(0, 76) << (text.size() > 76 ? "..." : "") << "\n"
              << "  (" << text.size() << " bytes)\n";

    const auto again = serialize(data_table::from_string(text));

    std::cout << "\n  written again after a round trip: "
              << (again == text ? "byte for byte identical" : "DIFFERENT -- " + again) << "\n"
                 "  the same state always spells itself the same way, so a save file can be diffed\n"
                 "  or checksummed\n";

    heading("and loading it again");

    auto loaded = interp.make_environment();

    if (const auto error = loaded->run("restored = " + text))
    {
        std::cout << "  could not load: " << *error << "\n";

        return 1;
    }

    std::cout << "  restored.name          " << loaded->read_string("restored.name").value_or("?") << "\n"
              << "  restored.hp            " << loaded->read_number("restored.hp").value_or(0) << "\n"
              << "  restored.position.x    " << loaded->read_number("restored.position.x").value_or(0)
              << "\n";

    if (const auto items = loaded->read_vector<std::string>("restored.inventory"))
    {
        std::cout << "  restored.inventory    ";

        for (const auto &item : *items) std::cout << " " << item;

        std::cout << "\n";
    }

    heading("a record assembled in c++ rather than read from lua");

    data_table quest;

    quest.set("title", "the long road");
    quest.set("stage", 2.0);
    quest.set("complete", false);

    data_table reward;

    reward.set(1, "lantern");
    reward.set(2, "rope");

    quest.set("reward", reward);

    std::cout << "  " << serialize(quest) << "\n";

    loaded->write("quest", quest);

    if (const auto error = loaded->run("summary = quest.title .. ', stage ' .. quest.stage"))
        std::cout << "  error: " << *error << "\n";

    std::cout << "  lua reads it as: " << loaded->read_string("summary").value_or("?") << "\n";

    return EXIT_SUCCESS;
}
