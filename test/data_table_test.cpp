// © Joseph Cameron - All Rights Reserved

#include <jfc/catch.hpp>

#include <jfc/lua.h>

#include <sstream>
#include <string>

using namespace jfc::lua;

namespace {
    struct round_trip final {
        interpreter interp;

        [[nodiscard]] std::optional<data_table> of(const std::string &aExpression) {
            if (interp.run("source = " + aExpression).has_value()) return {};

            auto source = interp.read_data_table("source");

            if (!source.has_value()) return {};

            interp.write("round_tripped", *source);

            return source;
        }

        [[nodiscard]] std::string describe(const std::string &aPath) {
            if (interp.run("described = tostring(" + aPath + ")").has_value()) return "<error>";

            return interp.read_string("described").value_or("<none>");
        }

        [[nodiscard]] bool agrees(const std::string &aSuffix) {
            return describe("source" + aSuffix) == describe("round_tripped" + aSuffix);
        }
    };

    [[nodiscard]] std::string serialize(const data_table &a) {
        std::ostringstream out;
        out << a;

        return out.str();
    }
}

TEST_CASE("a table is read out of lua", "[table]") {
    round_trip t;

    SECTION("an empty table") {
        REQUIRE(t.of("{ }").has_value());
    }

    SECTION("a sequence") {
        REQUIRE(t.of("{ 10, 20, 30 }").has_value());
        REQUIRE(t.agrees("[1]"));
        REQUIRE(t.agrees("[2]"));
        REQUIRE(t.agrees("[3]"));
    }

    SECTION("string keys") {
        REQUIRE(t.of("{ alpha = 1, beta = 'two', gamma = true }").has_value());
        REQUIRE(t.agrees(".alpha"));
        REQUIRE(t.agrees(".beta"));
        REQUIRE(t.agrees(".gamma"));
    }

    SECTION("boolean keys, which lua allows and most wrappers forget") {
        REQUIRE(t.of("{ [true] = 'yes', [false] = 'no' }").has_value());
        REQUIRE(t.agrees("[true]"));
        REQUIRE(t.agrees("[false]"));
    }

    SECTION("mixed key types in one table") {
        REQUIRE(t.of("{ 1, 2, named = 'x', [true] = 'y' }").has_value());
        REQUIRE(t.agrees("[1]"));
        REQUIRE(t.agrees("[2]"));
        REQUIRE(t.agrees(".named"));
        REQUIRE(t.agrees("[true]"));
    }

    SECTION("a nested table") {
        REQUIRE(t.of("{ outer = { inner = { leaf = 42 } } }").has_value());
        REQUIRE(t.agrees(".outer.inner.leaf"));
    }

    SECTION("a table nested under a number key") {
        REQUIRE(t.of("{ { 'a' }, { 'b' } }").has_value());
        REQUIRE(t.agrees("[1][1]"));
        REQUIRE(t.agrees("[2][1]"));
    }

    SECTION("non integral and non positive number keys") {
        REQUIRE(t.of("{ [0] = 'zero', [-1] = 'minus', [1.5] = 'half' }").has_value());
        REQUIRE(t.agrees("[0]"));
        REQUIRE(t.agrees("[-1]"));
        REQUIRE(t.agrees("[1.5]"));
    }

    SECTION("a sparse sequence keeps its keys") {
        REQUIRE(t.of("{ [1] = 'one', [3] = 'three', [7] = 'seven' }").has_value());

        REQUIRE(t.agrees("[1]"));
        REQUIRE(t.agrees("[3]"));
        REQUIRE(t.agrees("[7]"));

        REQUIRE(t.describe("round_tripped[2]") == "nil");
        REQUIRE(t.describe("round_tripped[4]") == "nil");
    }

    SECTION("a value that is not a table is refused") {
        REQUIRE_FALSE(t.interp.run("not_a_table = 7").has_value());
        REQUIRE_FALSE(t.interp.read_data_table("not_a_table").has_value());
    }
}

TEST_CASE("a table serializes to a lua table literal", "[table]") {
    round_trip t;

    SECTION("the output is something lua will accept back") {
        REQUIRE(t.of("{ alpha = 1, list = { 2, 3 }, [true] = 'yes' }").has_value());

        const auto text = serialize(*t.of("{ alpha = 1, list = { 2, 3 }, [true] = 'yes' }"));

        REQUIRE_FALSE(text.empty());
        REQUIRE(text.front() == '{');
        REQUIRE(text.back() == '}');

        REQUIRE_FALSE(t.interp.run("reparsed = " + text).has_value());
        REQUIRE(t.describe("type(reparsed)") == "table");
    }

    SECTION("the text carries every key back to the same value") {
        const auto source = t.of("{ [1] = 'one', [3] = 'three', [true] = 'yes', [false] = 'no', "
            "alpha = 1, beta = 'two' }");

        REQUIRE(source.has_value());

        REQUIRE_FALSE(t.interp.run("reparsed = " + serialize(*source)).has_value());

        for (const auto *const key : {"[1]", "[3]", "[true]", "[false]", ".alpha", ".beta"}) {
            INFO("key " << key);

            REQUIRE(t.describe(std::string("source") + key) ==
                t.describe(std::string("reparsed") + key));
        }
    }

    SECTION("the separators fall between entries and nowhere else") {
        const auto malformed = [](const std::string &aText) {
            return aText.find(",}") != std::string::npos    
                || aText.find("{,") != std::string::npos   
                || aText.find(",,") != std::string::npos; 
        };

        for (const auto *const source : {
            "{}",
            "{ alpha = 1 }",
            "{ [true] = 'yes' }",
            "{ [true] = 'yes', [false] = 'no' }",
            "{ 1, 2, 3 }",
            "{ [1] = 'one', [true] = 'yes' }",
            "{ [1] = 'one', alpha = 2 }",
            "{ [true] = 'yes', alpha = 2 }",
            "{ [1] = 'one', [true] = 'yes', alpha = 2 }",
            "{ [1] = 'one', [true] = 'yes', [false] = 'no', alpha = 2, beta = 3 }",
            "{ nested = { [true] = 'yes', alpha = 1 } }"}) {
            const auto parsed = t.of(source);

            REQUIRE(parsed.has_value());

            const auto text = serialize(*parsed);

            INFO(source << " serialized to " << text);

            REQUIRE_FALSE(malformed(text));

            REQUIRE_FALSE(t.interp.run("reparsed = " + text).has_value());
            REQUIRE(t.describe("type(reparsed)") == "table");
        }
    }

    SECTION("a sparse table's text keeps its keys explicit") {
        const auto source = t.of("{ [1] = 'one', [3] = 'three' }");

        REQUIRE(source.has_value());

        const auto text = serialize(*source);

        REQUIRE_FALSE(t.interp.run("reparsed = " + text).has_value());
        REQUIRE(t.describe("reparsed[1]") == "one");
        REQUIRE(t.describe("reparsed[3]") == "three");
        REQUIRE(t.describe("reparsed[2]") == "nil");
    }

    SECTION("an empty table serializes to an empty literal") {
        const auto source = t.of("{ }");

        REQUIRE(source.has_value());
        REQUIRE(serialize(*source) == "{}");
    }

    SECTION("a default constructed table serializes too") {
        REQUIRE(serialize(data_table()) == "{}");
    }

    SECTION("nesting survives the text form") {
        const auto source = t.of("{ outer = { inner = 5 } }");

        REQUIRE(source.has_value());

        REQUIRE_FALSE(t.interp.run("reparsed = " + serialize(*source)).has_value());
        REQUIRE(t.describe("reparsed.outer.inner") == "5");
    }
}

TEST_CASE("a table crosses back into lua intact", "[table]") {
    round_trip t;

    SECTION("written under a new name it reads the same") {
        REQUIRE(t.of("{ a = 1, b = 'two', c = { d = true } }").has_value());

        REQUIRE(t.agrees(".a"));
        REQUIRE(t.agrees(".b"));
        REQUIRE(t.agrees(".c.d"));
    }

    SECTION("twice through is the same as once") {
        REQUIRE(t.of("{ 1, 2, named = 'x', nested = { deep = 'y' } }").has_value());

        auto second = t.interp.read_data_table("round_tripped");

        REQUIRE(second.has_value());

        t.interp.write("twice", *second);

        REQUIRE(t.describe("twice[1]") == t.describe("source[1]"));
        REQUIRE(t.describe("twice.named") == t.describe("source.named"));
        REQUIRE(t.describe("twice.nested.deep") == t.describe("source.nested.deep"));
    }

    SECTION("writing into a nested path") {
        REQUIRE_FALSE(t.interp.run("holder = { }").has_value());

        const auto source = t.of("{ value = 9 }");

        REQUIRE(source.has_value());

        t.interp.write("holder.placed", *source);

        REQUIRE(t.describe("holder.placed.value") == "9");
    }
}
