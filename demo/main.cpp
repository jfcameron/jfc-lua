// © Joseph Cameron - All Rights Reserved

#include <jfc/lua.h>

#include <cstdlib>
#include <sstream>
#include <stdexcept>

static const std::string init((R"V0G0N(
junk = {}
debug = {
    a = {
        b = "wowee", --This gets overwritten by a table in main
        goof = "hello",
        troop = 564,
    }
}
very = {
    [-1] = "negative1_val",
    [0] = "zero_value",
    "one_value",
    "anon_one",
    [3.1415] = "pi",
    "anon_two",
    "anon_three",
    123,
    ["asdf"] = "asdf value",
    { "nothing" },
    cool = {
        "one",
        "two",
        "three!"
    },
    [true] = "lol",
    [0.1256] = "whoa",
    "月",
    "火",
    "水",
    "木",
    "金",
    "土",
    "日"
}

)V0G0N"));

static const std::string script((R"V0G0N(
debug.print("-------------------")
debug.print("table: ", very )
)V0G0N"));

int main(int argc, char *argv[])
{
    using namespace jfc::lua;

    interpreter interp;

    if (interp.run(init)) std::cout << "error in init\n";

    interp.write_value("written", "This is not a test");
    interp.write_value("debug.written", 456.);
    interp.write_value("debug.a.b.c.d.written", 6545.);
    interp.write_value("debug.a.b.c.d.stringy", "Whats going on");
    interp.write_value("global_number", 123.);
    interp.write_value("nested.number", 1.);
    interp.write_value("nes.ted.number", 2.);
    interp.write_value("debug.a.b.mybool", true);
    interp.write_value("debug.a.b.mynumber", 678.);
    
    interp.register_function("debug.print", [](params_type args) -> params_type
    {
        std::stringstream stream;

        for (const auto &arg : args) std::visit([&stream](auto &&arg)
        {
            stream << arg;
        }, arg);

        stream << "\n";

        std::cout << stream.str();

        return {};
    });

    interp.register_function("math.add", [](params_type args) -> params_type
    {
        double out(0);

        for (const auto &arg : args) std::visit([&out](auto &&arg)
        {
            using arg_type = std::decay_t<decltype(arg)>;

            if constexpr (std::is_same_v<arg_type, double>) out += arg;
            else throw std::runtime_error("unsupported arg type");
        }, arg);

        return {out};
    });

    if (auto error = interp.run(script))
        std::cout << "interp error: " << *error << "\n";

    auto val = interp.read_table("very");

    interpreter second_interpreter;

    second_interpreter.register_function("debug.print", [](params_type args) -> params_type
    {
        std::stringstream stream;

        for (const auto &arg : args) std::visit([&stream](auto &&arg)
        {
            stream << arg;
        }, arg);

        stream << "\n";

        std::cout << stream.str();

        return {};
    });

    second_interpreter.write_value("inbox.hello_from_first_interpreter", *val);

    static const std::string second_script((R"V0G0N(
        debug.print("---- INTERP 2 ----")
        debug.print("copy: ", inbox.hello_from_first_interpreter)

    )V0G0N"));

    if (auto error = second_interpreter.run(second_script))
        std::cout << "interp error: " << *error << "\n";

    return EXIT_SUCCESS;
}

