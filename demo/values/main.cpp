// © Joseph Cameron - All Rights Reserved

#include <jfc/lua.h>

#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace jfc::lua;

namespace {
    /// \brief the sort of thing a script wants a handle to rather than a copy of
    struct sprite final
    {
        std::string name;
        double x = 0, y = 0;

        ~sprite() { std::cout << "    (c++ destroyed the " << name << ")\n"; }
    };

    //! a second bound type, so that asking for the wrong one is something to show rather than assert
    struct texture final { std::string path; };

    const std::string world(R"V0G0N(
world = {
    name = "the overworld",
    spawns = { "meadow", "cave", "shore" },
    difficulty = { [1] = "gentle", [2] = "fair", [3] = "unkind" },
    flags = { [true] = "daylight", [false] = "night" },
    ["a.key.with.dots"] = "quoted in a path",
    region = { north = { town = "bridgewater" } }
}
)V0G0N");

    void heading(const char *const aTitle) { std::cout << "\n-- " << aTitle << "\n"; }
}

int main()
{
    interpreter interp;

    auto env = interp.make_environment();

    if (const auto error = env->run(world)) { std::cout << "setup failed: " << *error << "\n"; return 1; }

    heading("a dot is a string key, a bracket carries the key's type");

    std::cout << "  world.name              " << env->read_string("world.name").value_or("?") << "\n"
              << "  world.spawns[2]         " << env->read_string("world.spawns[2]").value_or("?") << "\n"
              << "  world.difficulty[3]     " << env->read_string("world.difficulty[3]").value_or("?") << "\n"
              << "  world.flags[true]       " << env->read_string("world.flags[true]").value_or("?") << "\n"
              << "  world['a.key.with.dots'] "
              << env->read_string("world['a.key.with.dots']").value_or("?") << "\n"
              << "  world.region.north.town " << env->read_string("world.region.north.town").value_or("?")
              << "\n";

    heading("the same path, built from typed keys instead of parsed");

    std::cout << "  path{\"world\", \"spawns\", 2}  "
        << env->read_string(path{"world", "spawns", 2}).value_or("?") << "\n";

    for (int i = 1; i <= 3; ++i)
        std::cout << "    spawn " << i << ": "
            << env->read_string(path{"world", "spawns", i}).value_or("?") << "\n";

    heading("writing, including keys that do not exist yet");

    env->write("world.spawns[4]", "summit");
    env->write("player.stats.hp", 100.0);
    env->write("player.name", "jim");

    std::cout << "  added a fourth spawn:   " << env->read_string("world.spawns[4]").value_or("?") << "\n"
              << "  built player.stats.hp:  " << env->read_number("player.stats.hp").value_or(0) << "\n";

    heading("a list reads straight into a vector");

    if (const auto spawns = env->read_vector<std::string>("world.spawns"))
    {
        std::cout << "  world.spawns ->";

        for (const auto &spawn : *spawns) std::cout << " " << spawn;

        std::cout << "\n";
    }

    env->write("world.weather", std::vector<std::string>{"clear", "rain", "fog"});

    if (const auto error = env->run("weather_count = #world.weather"))
        std::cout << "  error: " << *error << "\n";

    std::cout << "  wrote three back, lua counts " << env->read_number("weather_count").value_or(0)
        << "\n";

    std::cout << "  world.flags is keyed by booleans, so as a vector it is "
        << (env->read_vector<std::string>("world.flags") ? "something" : "nothing") << "\n";

    heading("a table crosses the barrier as a data_table");

    if (const auto region = env->read_data_table("world.region"))
    {
        const auto north = region->get_data_table("north");

        std::cout << "  read world.region, " << region->size() << " field(s), north.town = "
            << (north ? north->get_string("town").value_or("?") : "?") << "\n";
    }

    data_table sword;

    sword.set("name", "broadsword");
    sword.set("damage", 12.0);
    sword.set("two_handed", true);
    sword.set(1, "first tag");

    env->write("player.equipped", sword);

    if (const auto error = env->run(
        "described = player.equipped.name .. ' (' .. player.equipped.damage .. ')'"))
        std::cout << "  error: " << *error << "\n";

    std::cout << "  built a table in c++, lua sees: " << env->read_string("described").value_or("?")
        << "\n";

    heading("and it can be walked without knowing its shape");

    if (const auto equipped = env->read_data_table("player.equipped"))
    {
        std::vector<std::string> lines;

        for (const auto &field : equipped->keys())
        {
            std::stringstream line;

            line << "    ";

            std::visit([&line](auto &&aKey) { line << aKey; }, field.value());

            line << " = ";

            if (const auto value = equipped->get_string(field)) line << *value;
            else if (const auto value = equipped->get_number(field)) line << *value;
            else if (const auto value = equipped->get_boolean(field))
                line << (*value ? "true" : "false");
            else line << "<a table>";

            lines.push_back(line.str());
        }

        for (const auto &line : lines) std::cout << line << "\n";
    }

    heading("c++ functions a script can call");

    env->register_function("distance", [](value_list_type args) -> value_list_type
    {
        double total = 0;

        for (const auto &arg : args) total += std::get<double>(arg);

        return {total};
    });

    if (const auto error = env->run("travelled = distance(3, 4, 5)"))
        std::cout << "  error: " << *error << "\n";

    std::cout << "  distance(3, 4, 5) = " << env->read_number("travelled").value_or(0) << "\n";

    heading("and lua functions c++ can call");

    if (const auto error = env->run(
        "function label(place, away) return place .. ' is ' .. away .. ' away' end"))
        std::cout << "  error: " << *error << "\n";

    value_list_type results;

    if (const auto error = env->call("label", {std::string("the cave"), 12.0}, results))
        std::cout << "  error: " << *error << "\n";
    else
        std::cout << "  label('the cave', 12) -> " << std::get<std::string>(results.at(0)) << "\n";

    const std::string crafted = "'); stolen = 'yes'; label('";

    if (const auto error = env->call("label", {crafted, 1.0}, results))
        std::cout << "  error: " << *error << "\n";

    std::cout << "  a crafted string passed as a value stayed one: stolen is "
        << env->read_string("stolen").value_or("<never set>") << "\n";

    heading("a c++ object a script can hold");

    auto banner = std::make_shared<sprite>();

    banner->name = "banner sprite";

    env->write("hud.banner", banner);

    if (const auto error = env->run("kind = type(hud.banner)  stash = hud.banner"))
        std::cout << "  error: " << *error << "\n";

    std::cout << "  lua calls it a " << env->read_string("kind").value_or("?")
        << ", and can move it about without being able to look inside\n";

    env->register_function("move_to", [](value_list_type args) -> value_list_type
    {
        const auto target = std::get<userdata>(args.at(0)).get<sprite>();

        if (!target) return {false};

        target->x = std::get<double>(args.at(1));
        target->y = std::get<double>(args.at(2));

        return {true};
    });

    if (const auto error = env->run("moved = move_to(stash, 10, 20)"))
        std::cout << "  error: " << *error << "\n";

    std::cout << "  the script moved it, and c++ sees the change through its own share: "
        << banner->x << ", " << banner->y << "\n";

    heading("what it will not do");

    std::cout << "  read_userdata<sprite>    " << (env->read_userdata<sprite>("hud.banner")
                     ? "the sprite" : "nothing") << "\n"
              << "  read_userdata<texture>   " << (env->read_userdata<texture>("hud.banner")
                     ? "the sprite, wrongly!" : "nothing") << "\n"
              << "  read_number              " << (env->read_number("hud.banner")
                     ? "a number, wrongly!" : "nothing") << "\n";

    try {
        const auto saved = env->read_data_table("hud");

        std::cout << "  hud serialised unexpectedly\n";
    }
    catch (const jfc::lua_exception &e) { std::cout << "  " << e.what() << "\n"; }

    heading("and when both sides let go");

    if (const auto error = env->run("hud.banner = nil  stash = nil"))
        std::cout << "  error: " << *error << "\n";

    std::cout << "  lua has dropped it; c++ still holds a share, so nothing has happened yet\n";

    banner.reset();

    std::cout << "  c++ has dropped it too -- but destruction waits for the collector\n";

    if (const auto error = interp.run("collectgarbage('collect')"))
        std::cout << "  error: " << *error << "\n";

    return EXIT_SUCCESS;
}
