// © Joseph Cameron - MIT License

#include <jfc/lua.h>

#include <string_view>

int main() {
    jfc::lua::interpreter lua;

    auto env = lua.make_environment();

    env->register_function("length", [](const std::string_view aText) { return aText.size(); });

    if (env->run("out = length('four')")) return 1;

    return env->read_number("out") == 4.0 ? 0 : 1;
}
