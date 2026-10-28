// © Joseph Cameron - MIT License

#include <jfc/catch.hpp>

#include <jfc/lua.h>

#include <cctype>
#include <clocale>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace jfc::lua;

TEST_CASE("a table survives text and back", "[text]") {
    SECTION("every value kind") {
        data_table original;

        original.set("name", "bob");
        original.set("hp", 42.0);
        original.set("alive", true);
        original.set("dead", false);
        original.set("fraction", 1.5);
        original.set("negative", -3.0);

        const auto returned = data_table::from_string(original.to_string());

        REQUIRE(returned.size() == original.size());
        REQUIRE(returned.get_string("name") == "bob");
        REQUIRE(returned.get_number("hp") == 42);
        REQUIRE(returned.get_boolean("alive") == true);
        REQUIRE(returned.get_boolean("dead") == false);
        REQUIRE(returned.get_number("fraction") == 1.5);
        REQUIRE(returned.get_number("negative") == -3);
    }

    SECTION("every key kind") {
        data_table original;

        original.set(1, "one");
        original.set(2, "two");
        original.set(7, "seven");
        original.set(-1, "minus");
        original.set(0.5, "half");
        original.set(true, "yes");
        original.set(false, "no");
        original.set("named", "by name");

        const auto returned = data_table::from_string(original.to_string());

        REQUIRE(returned.size() == original.size());

        for (const auto &k : original.keys()) {
            REQUIRE(returned.contains(k));
            REQUIRE(returned.get_string(k) == original.get_string(k));
        }
    }

    SECTION("nested tables, to depth") {
        data_table inner;
        inner.set("leaf", 1.0);

        data_table middle;
        middle.set("inner", inner);

        data_table outer;
        outer.set("middle", middle);
        outer.set("beside", "x");

        const auto returned = data_table::from_string(outer.to_string());

        REQUIRE(returned.get_data_table("middle"));
        REQUIRE(returned.get_data_table("middle")->get_data_table("inner"));
        REQUIRE(returned.get_data_table("middle")->get_data_table("inner")->get_number("leaf") == 1);
    }

    SECTION("an empty table, and one holding an empty table") {
        REQUIRE(data_table::from_string(data_table().to_string()).empty());

        data_table holder;
        holder.set("nothing", data_table());

        const auto returned = data_table::from_string(holder.to_string());

        REQUIRE(returned.get_data_table("nothing"));
        REQUIRE(returned.get_data_table("nothing")->empty());
    }
}

TEST_CASE("strings that would break the text survive it", "[text]") {
    const std::vector<std::string> awkward{
        "plain",
        "a,b", "a=b", "a{b", "a}b", "a[b", "a]b",
        "say \"hi\"", "back\\slash", "line1\nline2", "tab\there", "carriage\rreturn",
        "", " ", "end", "nil", "function",          
        "1", "1.5", "true"                         
    };

    for (const auto &text : awkward) {
        INFO("string: [" << text << "]");

        data_table original;

        original.set(text, text);   

        const auto written = original.to_string();

        INFO("wrote: " << written);

        const auto returned = data_table::from_string(written);

        REQUIRE(returned.size() == 1);
        REQUIRE(returned.contains(text));
        REQUIRE(returned.get_string(text) == text);
    }
}

TEST_CASE("text outside ascii survives the round trip", "[text][unicode]") {
    data_table localisation;

    localisation.set("monday",    "月曜日");
    localisation.set("tuesday",   "火曜日");
    localisation.set("wednesday", "水曜日");
    localisation.set("thursday",  "木曜日");
    localisation.set("friday",    "金曜日");
    localisation.set("saturday",  "土曜日");
    localisation.set("sunday",    "日曜日");

    SECTION("through to_string and from_string") {
        const auto returned = data_table::from_string(localisation.to_string());

        REQUIRE(returned.size() == 7);

        for (const auto &k : localisation.keys())
            REQUIRE(returned.get_string(k) == localisation.get_string(k));

        REQUIRE(returned.get_string("monday") == "月曜日");
    }

    SECTION("and through lua, which is the other way a save is loaded") {
        interpreter interp;

        auto env = interp.make_environment();

        REQUIRE_FALSE(env->run("weekdays = " + localisation.to_string()).has_value());

        const auto viaLua = env->read_data_table("weekdays");

        REQUIRE(viaLua.has_value());
        REQUIRE(viaLua->get_string("sunday") == "日曜日");

        REQUIRE_FALSE(env->run("n = #weekdays.monday").has_value());
        REQUIRE(env->read_number("n") == 9);
    }

    SECTION("and stays legible in the text, rather than becoming escapes") {
        const auto written = localisation.to_string();

        INFO("wrote: " << written);

        REQUIRE(written.find("月曜日") != std::string::npos);
        REQUIRE(written.find("日曜日") != std::string::npos);
        REQUIRE(written.find("\\2") == std::string::npos);   
    }

    SECTION("as keys as well as values") {
        data_table reversed;

        reversed.set("月曜日", "monday");
        reversed.set("火曜日", "tuesday");

        const auto returned = data_table::from_string(reversed.to_string());

        REQUIRE(returned.size() == 2);
        REQUIRE(returned.get_string("月曜日") == "monday");
        REQUIRE(returned.get_string("火曜日") == "tuesday");
    }

    SECTION("mixed with ascii, and beyond the basic plane") {
        data_table mixed;

        mixed.set("label", "金 Friday 土 Saturday 日");
        mixed.set("emoji", "\U0001F3AE");            

        const auto returned = data_table::from_string(mixed.to_string());

        REQUIRE(returned.get_string("label") == "金 Friday 土 Saturday 日");
        REQUIRE(returned.get_string("emoji") == "\U0001F3AE");
    }
}

TEST_CASE("a decimal escape is padded, so a digit after it stays a digit", "[text][unicode]") {
    const std::string awkward = std::string("a\x01") + "5b";

    data_table original;

    original.set("k", awkward);

    const auto written = original.to_string();

    INFO("wrote: " << written);

    REQUIRE(data_table::from_string(written).get_string("k") == awkward);

    interpreter interp;

    auto env = interp.make_environment();

    REQUIRE_FALSE(env->run("t = " + written).has_value());
    REQUIRE_FALSE(env->run("n = #t.k").has_value());

    REQUIRE(env->read_number("n") == 4);
}

TEST_CASE("what to_string writes is still valid lua", "[text]") {
    interpreter interp;

    auto env = interp.make_environment();

    data_table original;

    original.set("name", "bob");
    original.set("odd key, with punctuation", "and \"quotes\"");
    original.set("end", "a reserved word, so not a bare key");
    original.set("function", "nor this one");
    original.set(1, "first");
    original.set(true, "flagged");

    REQUIRE_FALSE(env->run("restored = " + original.to_string()).has_value());

    const auto viaLua = env->read_data_table("restored");

    REQUIRE(viaLua.has_value());

    const auto viaParser = data_table::from_string(original.to_string());

    REQUIRE(viaLua->size() == viaParser.size());

    for (const auto &k : original.keys())
        REQUIRE(viaLua->get_string(k) == viaParser.get_string(k));
}

TEST_CASE("from_string refuses what it cannot read, rather than guessing", "[text]") {
    const std::vector<std::string> malformed{
        "",                    
        "{",                   
        "}",                   
        "{a=}",                
        "{=1}",                
        "{a=1",                
        "{a=nil}",             
        "{a=\"unterminated}",  
        "{a=1} trailing",      
        "{a=\\}",              
        "{[}=1}"               
    };

    for (const auto &text : malformed) {
        INFO("text: [" << text << "]");

        REQUIRE_THROWS_AS(data_table::from_string(text), jfc::lua::exception);
    }

    REQUIRE(data_table::from_string("{a=1,}").get_number("a") == 1);

    REQUIRE(data_table::from_string("{1,2,3}").get_number(3) == 3);
}

TEST_CASE("a tampered save is a parse error, not a script", "[text]") {
    const std::string tampered =
        "{name=\"bob\",hp=42} ; stolen = 'yes' ; damage = (function() while true do end end)()";

    SECTION("from_string refuses it") {
        REQUIRE_THROWS_AS(data_table::from_string(tampered), jfc::lua::exception);
    }

    SECTION("where running it executes the payload") {
        interpreter interp(interpreter_policy{.MEMORY_BUDGET_IN_BYTES = 0,
            .MEMORY_GROWTH_BUDGET_IN_BYTES_PER_RUN = 0, .INSTRUCTION_BUDGET = 200000});

        auto env = interp.make_environment();

        REQUIRE(env->run("restored = " + tampered).has_value());
        REQUIRE(env->read_string("stolen") == "yes");
    }
}

TEST_CASE("serialisation is deterministic", "[text][order]") {
    SECTION("keys come back ascending, whatever order they went in") {
        data_table t;

        for (const auto *const name : {"zulu", "mike", "alpha", "yankee", "bravo"}) t.set(name, 1.0);

        t.set(2, "two");
        t.set(1, "one");
        t.set(true, "yes");

        std::vector<std::string> strings;

        for (const auto &k : t.keys()) if (k.is_string()) strings.push_back(std::get<std::string>(k.value()));

        REQUIRE(strings == std::vector<std::string>{"alpha", "bravo", "mike", "yankee", "zulu"});

        const auto keys = t.keys();

        REQUIRE(keys.front().is_number());
        REQUIRE(std::get<double>(keys.front().value()) == 1);
        REQUIRE(keys.at(2).is_boolean());
        REQUIRE(keys.at(3).is_string());
    }

    SECTION("the text is exactly predictable, so it can be asserted whole") {
        data_table t;

        t.set("gamma", 3.0);
        t.set("alpha", 1.0);
        t.set("beta", 2.0);

        REQUIRE(t.to_string() == "{alpha=1,beta=2,gamma=3}");
    }

    SECTION("and identical content gives identical bytes") {
        data_table first;
        data_table second;

        for (const auto *const name : {"delta", "echo", "charlie"}) first.set(name, name);

        for (const auto *const name : {"charlie", "echo", "delta"}) second.set(name, name);

        REQUIRE(first.to_string() == second.to_string());
        REQUIRE(data_table::from_string(first.to_string()).to_string() == first.to_string());
    }

    SECTION("nested tables are determined too") {
        data_table inner;

        inner.set("second", 2.0);
        inner.set("first", 1.0);

        data_table outer;

        outer.set("zzz", 1.0);
        outer.set("inner", inner);
        outer.set("aaa", 2.0);

        REQUIRE(outer.to_string() == "{aaa=2,inner={first=1,second=2},zzz=1}");
    }
}

TEST_CASE("**a table reindented by hand still reads**", "[data_table][text]") {
    const std::string pretty = R"(
{
    ["format"] = 1,
    ["kits"] = {
        ["room"] = ">= 2",
    },
    ["deep"] = { ["list"] = { [1] = true, [2] = false, }, },
}
)";

    const auto read = data_table::from_string(pretty);

    REQUIRE(read.get_number("format") == 1);
    REQUIRE(read.get_data_table("kits")->get_string("room") == ">= 2");
    REQUIRE(read.get_data_table("deep")->get_data_table("list")->get_boolean(1.0) == true);

    SECTION("**and what it says is what it said before it was tidied**") {
        REQUIRE(data_table::from_string(read.to_string()).to_string() == read.to_string());
    }

    SECTION("**spaces inside a string are the string's, and are left alone**") {
        const auto spaced = data_table::from_string(R"({ ["words"] = "  two  spaces  ", })");

        REQUIRE(spaced.get_string("words") == "  two  spaces  ");
    }

    SECTION("**tabs and newlines are blanks like any other**") {
        REQUIRE(data_table::from_string("{\n\t[\"a\"]\t=\t1,\r\n}").get_number("a") == 1);
    }

    SECTION("**and what is not whitespace is still refused**") {
        for (const std::string bad : {"{ [\"a\"] = 1, } trailing", "{ [\"a\"] = os.time(), }",
                 "{ [\"a\"] 1, }", "{ [\"a\"] = , }"}) {
            INFO(bad);

            REQUIRE_THROWS(data_table::from_string(bad));
        }

        REQUIRE(data_table::from_string("{ -- a comment\n [\"a\"] = 1, }").get_number("a") == 1);
    }
}

TEST_CASE("a list reads as lua writes one: numbered from one, among named fields", "[data_table][text]") {
    const auto read = data_table::from_string(
        "{ 0.5, \"two\", true, { x = 1 }, name = \"named\", false, -3 }");

    REQUIRE(read.size() == 7);

    REQUIRE(read.get_number(1) == 0.5);
    REQUIRE(read.get_string(2) == "two");
    REQUIRE(read.get_boolean(3) == true);
    REQUIRE(read.get_data_table(4)->get_number("x") == 1.0);
    REQUIRE(read.get_boolean(5) == false);
    REQUIRE(read.get_number(6) == -3.0);
    REQUIRE(read.get_string("name") == "named");

    SECTION("a list of lists") {
        const auto frames = data_table::from_string("{ { 0, 0 }, { 8, 8 }, }");

        REQUIRE(frames.size() == 2);
        REQUIRE(frames.get_data_table(2)->get_number(1) == 8.0);
    }

    SECTION("a listed value wins over the explicit key it lands on, written before it or after") {
        REQUIRE(data_table::from_string("{ [1] = \"explicit\", \"listed\" }").get_string(1) == "listed");
        REQUIRE(data_table::from_string("{ \"listed\", [1] = \"explicit\" }").get_string(1) == "listed");
        REQUIRE(data_table::from_string("{ \"listed\", [2] = \"explicit\" }").get_string(2) == "explicit");
    }

    SECTION("and a list written back reads back the same") {
        const auto again = data_table::from_string(read.to_string());

        REQUIRE(again.size() == 7);
        REQUIRE(again.get_number(6) == -3.0);
    }

    SECTION("a name with no value is still not a value") {
        REQUIRE_THROWS(data_table::from_string("{ name }"));
    }
}

TEST_CASE("a data file written by hand reads as lua would: comments, and a leading return", "[data_table][text]") {
    const auto read = data_table::from_string(R"(
        -- a character: what it is drawn with, and how it moves
        return {
            sheet = "villager", -- sheets/villager/
            -- a comment between fields
            speeds = { 1.8, -- walking
                4.2 },
            says = "hello -- not a comment, a string",
        }
        -- and after it
    )");

    REQUIRE(read.get_string("sheet") == "villager");
    REQUIRE(read.get_data_table("speeds")->get_number(2) == 4.2);
    REQUIRE(read.get_string("says") == "hello -- not a comment, a string");

    SECTION("return is a word, not a prefix") {
        REQUIRE_THROWS(data_table::from_string("returned { a = 1 }"));
    }

    SECTION("and nothing but data is read after it") {
        REQUIRE_THROWS(data_table::from_string("return os.exit()"));
        REQUIRE_THROWS(data_table::from_string("return { a = 1 } print(1)"));
    }
}

TEST_CASE("NaN and the infinities are written as lua writes them, and read back", "[text]") {
    const auto inf = std::numeric_limits<double>::infinity();

    data_table t;

    t.set("none", std::numeric_limits<double>::quiet_NaN());
    t.set("up", inf);
    t.set("down", -inf);
    t.set(inf, std::string("far"));

    const auto text = t.to_string();

    INFO(text);

    REQUIRE(text.find("0/0") != std::string::npos);
    REQUIRE(text.find("inf") == std::string::npos);

    const auto back = data_table::from_string(text);

    REQUIRE(std::isnan(*back.get_number("none")));
    REQUIRE(back.get_number("up") == inf);
    REQUIRE(back.get_number("down") == -inf);
    REQUIRE(back.get_string(inf) == "far");

    SECTION("and run as lua") {
        interpreter interp;

        auto env = interp.make_environment();

        REQUIRE_FALSE(env->run("t = " + text).has_value());

        REQUIRE(std::isnan(*env->read_number("t.none")));
        REQUIRE(env->read_number("t.down") == -inf);
        REQUIRE(env->read_string(path{"t", inf}) == "far");
    }
}

TEST_CASE("nan and inf, as older versions wrote them, still read", "[text]") {
    const auto back = data_table::from_string("{a=nan,b=inf,c=-inf}");

    REQUIRE(std::isnan(*back.get_number("a")));
    REQUIRE(back.get_number("b") == std::numeric_limits<double>::infinity());
    REQUIRE(back.get_number("c") == -std::numeric_limits<double>::infinity());
}

TEST_CASE("only the divisions to_string writes are read", "[text]") {
    REQUIRE_THROWS_AS(data_table::from_string("{a=2/0}"), exception);
    REQUIRE_THROWS_AS(data_table::from_string("{a=1/2}"), exception);
}

TEST_CASE("a NaN key in text is refused", "[text]") {
    REQUIRE_THROWS_AS(data_table::from_string("{[0/0]=1}"), exception);
    REQUIRE_THROWS_AS(data_table::from_string("{[nan]=1}"), exception);
}

TEST_CASE("text nested deeper than a table may be is refused, not followed off the stack", "[text]") {
    const auto nested = [](const std::size_t aDepth) {
        return std::string(aDepth, '{') + std::string(aDepth, '}');
    };

    REQUIRE_NOTHROW(data_table::from_string(nested(data_table::MAXIMUM_DEPTH)));
    REQUIRE_THROWS_WITH(data_table::from_string(nested(data_table::MAXIMUM_DEPTH + 1)), Catch::Matchers::Contains("nesting"));
    REQUIRE_THROWS_AS(data_table::from_string(nested(1000000)), exception);
}

namespace {
    class scoped_locale final {
        std::locale m_Previous;
        bool m_Found = false;

    public:
        scoped_locale(const scoped_locale &) = delete;
        scoped_locale &operator=(const scoped_locale &) = delete;

        scoped_locale(const std::initializer_list<const char *> aNames) {
            for (const auto *const name : aNames) {
                try { m_Previous = std::locale::global(std::locale(name)); }
                catch (const std::runtime_error &) { continue; }

                m_Found = true;

                return;
            }
        }

        ~scoped_locale() { if (m_Found) std::locale::global(m_Previous); }

        [[nodiscard]] bool found() const { return m_Found; }
    };
}

TEST_CASE("numbers read and write the same whatever the locale", "[text][locale]") {
    const scoped_locale comma({"de_DE.UTF-8", "de_DE.utf8", "fr_FR.UTF-8", "fr_FR.utf8"});

    if (!comma.found() || std::string(std::localeconv()->decimal_point) != ",") {
        WARN("no locale with a comma for its decimal point is installed: nothing to check");

        return;
    }

    SECTION("written with points") {
        data_table t;

        t.set("half", 0.5);
        t.set("tenth", 0.1);
        t.set("big", 1234567.0);

        REQUIRE(t.to_string() == "{big=1234567,half=0.5,tenth=0.1}");
    }

    SECTION("read with points") {
        const auto t = data_table::from_string("{half=0.5,[0.25]=\"quarter\"}");

        REQUIRE(t.get_number("half") == 0.5);
        REQUIRE(t.get_string(0.25) == "quarter");
    }

    SECTION("a comma between numbers is not taken for a decimal point") {
        const auto t = data_table::from_string("{1,2}");

        REQUIRE(t.size() == 2);
        REQUIRE(t.get_number(1) == 1.0);
        REQUIRE(t.get_number(2) == 2.0);
    }

    SECTION("paths too") {
        REQUIRE(path("t[0.5]") == path{"t", 0.5});

        std::ostringstream written;

        written << path{"t", 0.5};

        REQUIRE(written.str() == "t[0.5]");
    }
}

TEST_CASE("what is a name or a space does not follow the locale", "[text][locale]") {
    const scoped_locale latin({"de_DE", "de_DE.ISO-8859-1", "de_DE.iso88591", "fr_FR", "fr_FR.ISO-8859-1"});

    const std::string e = "\xC3\xAA";

    if (!latin.found() || !std::isalpha(static_cast<unsigned char>(e[0])) || !std::isalpha(static_cast<unsigned char>(e[1]))) {
        WARN("no latin-1 locale is installed: nothing to check");

        return;
    }

    SECTION("a key of utf-8 is quoted, as lua wants it") {
        data_table t;

        t.set(e, 1.0);

        REQUIRE(t.to_string() == "{[\"" + e + "\"]=1}");
    }

    SECTION("and read only quoted") {
        REQUIRE_THROWS_AS(data_table::from_string("{" + e + "=1}"), exception);
    }
}

TEST_CASE("numbers read in every spelling lua and older versions wrote", "[text]") {
    const auto t = data_table::from_string("{0x10,-0Xff,.5,5.,1e3,1E-3,-0,-nan(ind),infinity,+2}");

    REQUIRE(t.get_number(1) == 16.0);
    REQUIRE(t.get_number(2) == -255.0);
    REQUIRE(t.get_number(3) == 0.5);
    REQUIRE(t.get_number(4) == 5.0);
    REQUIRE(t.get_number(5) == 1000.0);
    REQUIRE(t.get_number(6) == 0.001);
    REQUIRE(std::signbit(*t.get_number(7)));
    REQUIRE(std::isnan(*t.get_number(8)));
    REQUIRE(t.get_number(9) == std::numeric_limits<double>::infinity());
    REQUIRE(t.get_number(10) == 2.0);

    REQUIRE_THROWS_AS(data_table::from_string("{1e}"), exception);
    REQUIRE_THROWS_AS(data_table::from_string("{.}"), exception);
    REQUIRE_THROWS_AS(data_table::from_string("{0x}"), exception);
}

TEST_CASE("lua's own numbers are spelt with a point whatever the locale", "[locale]") {
    const scoped_locale comma({"de_DE.UTF-8", "de_DE.utf8", "fr_FR.UTF-8", "fr_FR.utf8"});

    if (!comma.found() || std::string(std::localeconv()->decimal_point) != ",") {
        WARN("no locale with a comma for its decimal point is installed: nothing to check");

        return;
    }

    interpreter lua;

    auto env = lua.make_environment();

    REQUIRE_FALSE(env->run(R"(
        written = tostring(0.5)
        joined = "x" .. 1.5
        read = tonumber("0.25")
        read_with_comma = tonumber("0,25")
        number = 0.75
    )").has_value());

    REQUIRE(env->read_string("written") == "0.5");
    REQUIRE(env->read_string("joined") == "x1.5");
    REQUIRE(env->read_number("read") == 0.25);
    REQUIRE_FALSE(env->read_number("read_with_comma").has_value());
    REQUIRE(env->read_number("number") == 0.75);
}
