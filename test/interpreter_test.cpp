// © Joseph Cameron - All Rights Reserved

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

