// © Joseph Cameron - MIT License

#include <jfc/catch.hpp>
#include <jfc/types.h>

#include <jfc/lua.h>

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

