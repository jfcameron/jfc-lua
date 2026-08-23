// © Joseph Cameron - All Rights Reserved

#include <jfc/lua.h>

#include <iostream>
#include <sstream>
#include <type_traits>
#include <string>

using namespace jfc::lua;

namespace {
    const std::string behaviour(R"V0G0N(
hp = 100
name = "unnamed"

function describe() return "is on " .. hp .. "hp." end

function hurt(amount)
    hp = hp - amount

    if hp <= 0 then say("collapses.")
    else say("takes " .. amount .. ", down to " .. hp .. ".")
    end
end
)V0G0N");

    void heading(const char *const aTitle) { std::cout << "\n-- " << aTitle << "\n"; }

    [[nodiscard]] environment_shared_ptr_type make_npc(interpreter &aInterpreter,
        const std::string &aName)
    {
        auto npc = aInterpreter.make_environment();

        npc->register_function("say", [aName](value_list_type args) -> value_list_type
        {
            std::stringstream out;

            out << aName << " ";

            for (const auto &arg : args) std::visit([&out](auto &&value)
            {
                using value_type = std::decay_t<decltype(value)>;

                if constexpr (std::is_same_v<value_type, userdata>) out << "<a bound object>";
                else out << value;
            }, arg);

            std::cout << "  " << out.str() << "\n";

            return {};
        });

        if (const auto error = npc->run(behaviour))
            std::cout << "  npc error: " << *error << "\n";

        npc->write("name", aName);

        return npc;
    }
}

int main() {
    interpreter interp;

    auto bob = make_npc(interp, "bob");
    auto alice = make_npc(interp, "alice");

    heading("the same script but separate states for each instance");

    bob->write("hp", 60.0);

    for (const auto &npc : {bob, alice})
    {
        value_list_type described;

        if (const auto error = npc->call("describe", {}, described))
            std::cout << "  npc error: " << *error << "\n";
        else
            std::cout << "  " << npc->read_string("name").value_or("?") << " "
                << std::get<std::string>(described.at(0)) << "\n";
    }

    if (const auto error = bob->call("hurt", {10.0})) std::cout << "  npc error: " << *error << "\n";
    if (const auto error = alice->call("hurt", {25.0})) std::cout << "  npc error: " << *error << "\n";

    std::cout << "\n  read back from c++: bob " << bob->read_number("hp").value_or(0)
        << "hp, alice " << alice->read_number("hp").value_or(0) << "hp\n";

    heading("nothing leaks between instances or host");

    if (const auto error = alice->run("secret = 'alice only'"))
        std::cout << "  npc error: " << *error << "\n";

    if (const auto error = interp.run("host_only = 'the interpreter'"))
        std::cout << "  error: " << *error << "\n";

    std::cout << "  bob sees his own name:      " << bob->read_string("name").value_or("<no>") << "\n"
              << "  bob sees alice's secret:    " << bob->read_string("secret").value_or("<no>") << "\n"
              << "  bob sees the host's global: " << bob->read_string("host_only").value_or("<no>")
              << "\n";

    heading("what a script may reach is chosen per environment");

    auto untrusted = interp.make_environment();
    auto trusted = interp.make_environment(environment_policy{standard_library::unrestricted});

    const auto reaches = [](const environment_shared_ptr_type &aEnvironment, const char *const aName)
    {
        const auto error = aEnvironment->run(std::string("probe = tostring(") + aName + ")");

        return error ? std::string("<error>") : aEnvironment->read_string("probe").value_or("?");
    };

    for (const auto *const name : {"math.floor", "io", "os.execute", "require", "debug"})
        std::cout << "    " << (std::string(name) + std::string(12 - std::string(name).size(), ' '))
            << "  safe: " << (reaches(untrusted, name) == "nil" ? "no " : "yes")
            << "   unrestricted: " << (reaches(trusted, name) == "nil" ? "no" : "yes") << "\n";

    std::cout << "\n  the safe set is an allow list, so anything luajit adds later is hidden until it is named\n";

    heading("a whole second interpreter");

    interpreter second;

    if (const auto error = second.run("greeting = 'from the other interpreter'"))
        std::cout << "  error: " << *error << "\n";

    data_table message;

    message.set("text", second.read_string("greeting").value_or("?"));
    message.set("hops", 1.0);

    bob->write("inbox", message);

    if (const auto error = bob->run("say('was told: ' .. inbox.text)"))
        std::cout << "  npc error: " << *error << "\n";

    return EXIT_SUCCESS;
}
