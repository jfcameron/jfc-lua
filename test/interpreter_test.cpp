// © Joseph Cameron - MIT License

#include <jfc/catch.hpp>
#include <jfc/types.h>

#include <jfc/lua.h>

#include <string>
#include <vector>

using namespace jfc::lua;

/*static const std::string script((R"V0G0N(
    message = { 1, 2, 3 }
)V0G0N"));*/

TEST_CASE( "jfc::lua::interpreter_test", "[jfc::lua::interpreter]" )
{
    SECTION("The interpreter runs a well-formed script")
    {
        interpreter interp;

        auto error = interp.run("message = { 1, 2, 3 }");

        REQUIRE(!error.has_value());
    }

    SECTION("The interpreter fails to run an ill-formed script")
    {
        interpreter interp;

        auto error = interp.run("message = { 1 2, 3 }");

        REQUIRE(error.has_value());
    }
}

TEST_CASE("integer boundary constants and negative table keys remain valid", "[jfc::lua::interpreter]")
{
    interpreter interp;
    const auto error = interp.run(R"lua(
        local minimum = -2147483648
        local values = {[minimum] = 'minimum', [1] = 'first'}
        assert(values[minimum] == 'minimum')
        assert(values[minimum + 1] == nil)
        assert(values[0] == nil and values[1] == 'first')
    )lua");
    REQUIRE_FALSE(error.has_value());
}

#if defined(JFC_LUA_LUAJIT) || defined(JFC_LUA_LUAU)
TEST_CASE("random numbers start the same in every interpreter until a script seeds them", "[jfc::lua::interpreter]") {
    const auto draws = [](const std::string &aFirst) {
        interpreter lua;

        auto env = lua.make_environment();

        REQUIRE_FALSE(env->run(aFirst + " a, b, c = math.random(1, 1000000), math.random(1, 1000000), math.random()"));

        return std::vector<double>{*env->read_number("a"), *env->read_number("b"), *env->read_number("c")};
    };

    REQUIRE(draws("") == draws(""));
    REQUIRE(draws("math.randomseed(7)") == draws("math.randomseed(7)"));
    REQUIRE(draws("math.randomseed(7)") != draws(""));
}
#endif
