// © Joseph Cameron - MIT License

#include <jfc/catch.hpp>

#include <jfc/lua.h>

#include <limits>
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

TEST_CASE("a number survives being written down and read back", "[table]") {
    const double numbers[]{
        4503599627370496.0,    
        12345678.0,
        0.123456789,
        1.0 / 3.0,
        -98765.4321,
        1e-7,
        1.5,                    
        100.0,
        0.0,
    };

    for (const double each : numbers) {
        INFO(each);

        jfc::lua::data_table source;

        source.set("value", each);
        source.set(each, std::string("keyed by it too"));

        const auto read = jfc::lua::data_table::from_string(source.to_string());

        REQUIRE(read.get_number("value") == each);
        REQUIRE(read.get_string(each) == "keyed by it too");
    }

    SECTION("and a plain number is written plainly, for whoever opens the file") {
        jfc::lua::data_table table;

        table.set("size", 1.5);
        table.set("full", 100.0);

        const auto text = table.to_string();

        REQUIRE(text.find("1.5") != std::string::npos);
        REQUIRE(text.find("100") != std::string::npos);

        REQUIRE(text.find("1.5000") == std::string::npos);
        REQUIRE(text.find("100.000") == std::string::npos);
    }

    SECTION("a whole number is never written in scientific notation") {
        const double whole[]{
            7250984733720.0,        
            63150973064418.0,      
            1e12, 1e13, 1e14, 1e15,
            9007199254740992.0,   
            -7250984733720.0,
            120.0, 1000000.0, 0.0, -0.0,
        };

        for (const double each : whole) {
            INFO(each);

            jfc::lua::data_table table;

            table.set("id", each);

            const auto text = table.to_string();

            INFO(text);

            REQUIRE(text.find('e') == std::string::npos);
            REQUIRE(text.find('.') == std::string::npos);

            REQUIRE(jfc::lua::data_table::from_string(text).get_number("id") == each);
        }
    }

    SECTION("negative zero is written as zero") {
        jfc::lua::data_table table;

        table.set("none", -0.0);

        REQUIRE(table.to_string().find("-0") == std::string::npos);
    }
}

TEST_CASE("a NaN key is refused, and asked for, is nothing", "[data_table]") {
    const auto nan = std::numeric_limits<double>::quiet_NaN();

    data_table t;

    t.set(5, 1.0);
    t.set("x", 2.0);

    REQUIRE_THROWS_AS(t.set(nan, 1.0), exception);
    REQUIRE_FALSE(t.get_number(nan).has_value());
    REQUIRE_FALSE(t.contains(nan));

    t.erase(nan);

    REQUIRE(t.size() == 2);
    REQUIRE(t.get_number(5) == 1);
}

TEST_CASE("a string keeps its NULs as a key and as a value, into lua and out", "[data_table]") {
    const std::string withNul("a\0b", 3);

    interpreter interp;

    auto env = interp.make_environment();

    data_table t;

    t.set(withNul, withNul);

    env->write("t", t);

    REQUIRE_FALSE(env->run("for k, v in pairs(t) do keyLength, valueLength = #k, #v end").has_value());
    REQUIRE(env->read_number("keyLength") == 3);
    REQUIRE(env->read_number("valueLength") == 3);

    REQUIRE_FALSE(env->run("back = { ['x\\0y'] = 'p\\0q' }").has_value());

    const auto back = env->read_data_table("back");

    REQUIRE(back);
    REQUIRE(back->get_string(std::string("x\0y", 3)) == std::string("p\0q", 3));
}

TEST_CASE("a table as deep as reading allows writes back", "[data_table]") {
    interpreter interp;

    auto env = interp.make_environment();

    REQUIRE_FALSE(env->run(
        "deep = {} local at = deep\n"
        "for i = 1, 127 do at.inner = { n = i } at = at.inner end").has_value());

    const auto deep = env->read_data_table("deep");

    REQUIRE(deep);

    env->write("copy", *deep);

    REQUIRE(env->read_data_table("copy")->to_string() == deep->to_string());
}
