// © Joseph Cameron - All Rights Reserved

#include <jfc/catch.hpp>

#include <jfc/lua.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <variant>

using namespace jfc::lua;

namespace {
    struct label final {
        std::string text;
        int *destroyed = nullptr;

        ~label() { if (destroyed) ++*destroyed; }
    };

    struct button final { std::string caption; };

    struct plain final { int value = 0; };

    void register_label(interpreter &aLua) {
        aLua.register_type<label>("label", {
            {"set_text", [](label &l, value_list_type a) -> value_list_type {
                l.text = std::get<std::string>(a.at(0));

                return {};
            }},
            {"text", [](label &l, value_list_type) -> value_list_type { return {l.text}; }},
            {"joined", [](label &l, value_list_type a) -> value_list_type {
                return {l.text + std::get<std::string>(a.at(0)), static_cast<double>(a.size())};
            }},
            {"fail", [](label &, value_list_type) -> value_list_type {
                throw std::runtime_error("the label will not");
            }},
        });
    }

    void register_button(interpreter &aLua) {
        aLua.register_type<button>("button", {
            {"press", [](button &b, value_list_type) -> value_list_type { return {b.caption}; }},
        });
    }

    void collect(interpreter &aLua) { REQUIRE_FALSE(aLua.run("collectgarbage('collect')")); }
}

TEST_CASE("**a registered type's methods are called on it with a colon**", "[userdata][methods]") {
    interpreter lua;

    register_label(lua);

    auto env = lua.make_environment();

    const auto greeting = std::make_shared<label>();

    env->write("greeting", greeting);

    SECTION("**a method works on the c++ object itself**") {
        REQUIRE_FALSE(env->run("greeting:set_text('hello')"));
        REQUIRE(greeting->text == "hello");
    }

    SECTION("**and what it returns is what the call returns**") {
        greeting->text = "well";

        REQUIRE_FALSE(env->run("got = greeting:text()"));
        REQUIRE(env->read_string("got") == "well");
    }

    SECTION("**with the arguments after the object, and every result**") {
        greeting->text = "a";

        REQUIRE_FALSE(env->run("joined, count = greeting:joined('b')"));
        REQUIRE(env->read_string("joined") == "ab");
        REQUIRE(env->read_number("count") == 1);
    }

    SECTION("**taken as a value, a method is a function, which a dot and the object also call**") {
        REQUIRE_FALSE(env->run("kind = type(greeting.set_text)  greeting.set_text(greeting, 'dot')"));
        REQUIRE(env->read_string("kind") == "function");
        REQUIRE(greeting->text == "dot");
    }

    SECTION("**many calls leave nothing behind**") {
        REQUIRE_FALSE(env->run("for i = 1, 100000 do greeting:set_text('x' .. i) end"));
        REQUIRE(greeting->text == "x100000");
    }
}

TEST_CASE("**a method called on anything but its own type says so, and runs nothing**",
    "[userdata][methods]") {
    interpreter lua;

    register_label(lua);
    register_button(lua);

    auto env = lua.make_environment(environment_policy{standard_library::unrestricted});

    const auto greeting = std::make_shared<label>();

    env->write("greeting", greeting);
    env->write("ok", std::make_shared<button>());
    env->write("thing", std::make_shared<plain>());

    const auto refused = [&](const std::string &aScript) {
        const auto error = env->run(aScript);

        REQUIRE(error);

        INFO("the error was " << *error);

        REQUIRE(error->find("colon") != std::string::npos);
        REQUIRE(error->find("set_text") != std::string::npos);
        REQUIRE(error->find("label") != std::string::npos);
        REQUIRE(greeting->text.empty());
    };

    SECTION("**called with a dot, so its first argument is not the object**") {
        refused("greeting.set_text('dot')");
    }

    SECTION("**called with nothing at all**") { refused("greeting.set_text()"); }

    SECTION("**on another registered type**") { refused("greeting.set_text(ok, 'x')"); }

    SECTION("**on an unregistered one**") { refused("greeting.set_text(thing, 'x')"); }

    SECTION("**on a table, whatever it pretends to be**") {
        refused("greeting.set_text({ text = '' }, 'x')");
    }

    SECTION("**on somebody else's userdata**") {
        refused("greeting.set_text(io.tmpfile(), 'x')");
    }

    SECTION("**and a method a type does not have is nil, not another's**") {
        REQUIRE_FALSE(env->run("missing = ok.set_text == nil  pressed = ok:press()"));
        REQUIRE(env->read_boolean("missing") == true);
        REQUIRE(env->read_string("pressed") == "");
    }
}

TEST_CASE("**a method that throws raises an error in lua, and the script carries on after**",
    "[userdata][methods]") {
    interpreter lua;

    register_label(lua);

    auto env = lua.make_environment();

    env->write("greeting", std::make_shared<label>());

    const auto error = env->run("greeting:fail()");

    REQUIRE(error);
    REQUIRE(error->find("the label will not") != std::string::npos);

    REQUIRE_FALSE(env->run("ok, why = pcall(greeting.fail, greeting)  after = greeting:text()"));
    REQUIRE(env->read_boolean("ok") == false);
    REQUIRE(env->read_string("why")->find("the label will not") != std::string::npos);
    REQUIRE(env->read_string("after") == "");
}

TEST_CASE("**a script can set fields on a registered object, as on a table**",
    "[userdata][fields]") {
    interpreter lua;

    register_label(lua);

    auto env = lua.make_environment();

    const auto first = std::make_shared<label>();
    const auto second = std::make_shared<label>();

    env->write("first", first);
    env->write("second", second);

    SECTION("**a field set is read back, of any kind of value**") {
        REQUIRE_FALSE(env->run(R"(
            first.count = 3
            first.on_submit = function(self, text) self:set_text(text) end
            first.on_submit(first, 'sent')
            count = first.count
        )"));

        REQUIRE(env->read_number("count") == 3);
        REQUIRE(first->text == "sent");
    }

    SECTION("**each object has its own**") {
        REQUIRE_FALSE(env->run("first.name = 'one'  unset = second.name == nil"));
        REQUIRE(env->read_boolean("unset") == true);
    }

    SECTION("**a field read through a path, from the host**") {
        REQUIRE_FALSE(env->run("first.name = 'one'  first.nested = { deep = 2 }"));
        REQUIRE(env->read_string("first.name") == "one");
        REQUIRE(env->read_number("first.nested.deep") == 2);
        REQUIRE_FALSE(env->read_string("first.nothing"));
    }

    SECTION("**a method's name cannot be taken by a field**") {
        const auto error = env->run("first.set_text = function() end");

        REQUIRE(error);
        REQUIRE(error->find("set_text is a method of label") != std::string::npos);

        REQUIRE_FALSE(env->run("first:set_text('still the method')"));
        REQUIRE(first->text == "still the method");
    }

    SECTION("**the fields are the object's own, not the type's: setting one gives no other it**") {
        REQUIRE_FALSE(env->run("first.set_text_later = true  other = second.set_text_later"));
        REQUIRE_FALSE(env->read_boolean("other"));
    }
}

TEST_CASE("**the same object is the same lua value**", "[userdata][identity]") {
    interpreter lua;

    register_label(lua);

    auto env = lua.make_environment();

    const auto greeting = std::make_shared<label>();

    env->write("a", greeting);
    env->write("b", greeting);
    env->write("other", std::make_shared<label>());

    SECTION("**handed over twice, it compares equal, and raw equal**") {
        REQUIRE_FALSE(env->run("same = a == b  raw = rawequal(a, b)  different = a == other"));
        REQUIRE(env->read_boolean("same") == true);
        REQUIRE(env->read_boolean("raw") == true);
        REQUIRE(env->read_boolean("different") == false);
    }

    SECTION("**so a script's table keyed by it finds it again**") {
        REQUIRE_FALSE(env->run("seen = { [a] = 'yes' }  found = seen[b]"));
        REQUIRE(env->read_string("found") == "yes");
    }

    SECTION("**and a field set through one handle is there through the other**") {
        REQUIRE_FALSE(env->run("a.mark = 'x'  through_b = b.mark"));
        REQUIRE(env->read_string("through_b") == "x");
    }

    SECTION("**returned from c++, it is the one the script has**") {
        env->register_function("fetch", [greeting](value_list_type) -> value_list_type {
            return {userdata::make(greeting)};
        });

        REQUIRE_FALSE(env->run("same = fetch() == a  kept = fetch()  kept.mark = 1  mark = a.mark"));
        REQUIRE(env->read_boolean("same") == true);
        REQUIRE(env->read_number("mark") == 1);
    }

    SECTION("**in every environment of the interpreter**") {
        auto elsewhere = lua.make_environment();

        elsewhere->write("c", greeting);

        REQUIRE_FALSE(env->run("a.mark = 'from the first'"));
        REQUIRE(elsewhere->read_string("c.mark") == "from the first");
    }

    SECTION("**an unregistered type's too**") {
        const auto thing = std::make_shared<plain>();

        env->write("p", thing);
        env->write("q", thing);

        REQUIRE_FALSE(env->run("same = p == q"));
        REQUIRE(env->read_boolean("same") == true);
    }
}

TEST_CASE("**the host holds an object's lua value, and with it the fields scripts set**",
    "[userdata][hold]") {
    interpreter lua;

    register_label(lua);

    auto env = lua.make_environment();

    int destroyed = 0;

    auto field = std::make_shared<label>();

    field->destroyed = &destroyed;

    auto held = lua.hold(field);

    env->write("field", field);

    REQUIRE_FALSE(env->run(R"(
        field.on_submit = function(self, text) self:set_text('submitted ' .. text) return 'thanks' end
        field.title = 'chat'
    )"));

    SECTION("**the host reads a handler a script set, and calls it**") {
        auto handler = held.read_reference("on_submit");

        REQUIRE(handler);

        value_list_type out;

        REQUIRE_FALSE(handler->call({userdata::make(field), std::string("hi")}, out));
        REQUIRE(field->text == "submitted hi");
        REQUIRE(std::get<std::string>(out.at(0)) == "thanks");
        REQUIRE(held.read_string("title") == "chat");
    }

    SECTION("**the host writes a field**") {
        held.write("title", "log");

        REQUIRE_FALSE(env->run("title = field.title"));
        REQUIRE(env->read_string("title") == "log");
    }

    SECTION("**but never over a method**") {
        held.write("set_text", "not a method");

        REQUIRE_FALSE(env->run("field:set_text('the method')"));
        REQUIRE(field->text == "the method");
    }

    SECTION("**after every script lets go, the fields last while the host holds it**") {
        REQUIRE_FALSE(env->run("field = nil"));

        collect(lua);

        REQUIRE(held.read_string("title") == "chat");
        REQUIRE(held.read_reference("on_submit"));

        SECTION("**and handed back, it is the value it was**") {
            env->write("again", field);

            REQUIRE_FALSE(env->run("title = again.title"));
            REQUIRE(env->read_string("title") == "chat");
        }
    }

    SECTION("**held, the object lives on without the host's pointer, until let go**") {
        REQUIRE_FALSE(env->run("field = nil"));

        field.reset();

        collect(lua);

        REQUIRE(destroyed == 0);

        held = lua.hold(std::make_shared<label>());

        collect(lua);

        REQUIRE(destroyed == 1);
    }
}

TEST_CASE("**a registered object lives while either side holds it, and its fields die with its lua "
    "value**", "[userdata][lifetime]") {
    int destroyed = 0;

    SECTION("**let go of by both, it is destroyed at collection**") {
        interpreter lua;

        register_label(lua);

        auto env = lua.make_environment();

        {
            auto greeting = std::make_shared<label>();

            greeting->destroyed = &destroyed;

            env->write("greeting", greeting);
        }

        REQUIRE_FALSE(env->run("greeting.mark = 1  greeting = nil"));

        REQUIRE(destroyed == 0);

        collect(lua);

        REQUIRE(destroyed == 1);
    }

    SECTION("**a value no longer held by anything is a new one next time, without the old fields**") {
        interpreter lua;

        register_label(lua);

        auto env = lua.make_environment();

        const auto greeting = std::make_shared<label>();

        greeting->destroyed = &destroyed;

        env->write("greeting", greeting);

        REQUIRE_FALSE(env->run("greeting.mark = 1  greeting = nil"));

        collect(lua);

        REQUIRE(destroyed == 0);

        env->write("greeting", greeting);

        REQUIRE_FALSE(env->run("gone = greeting.mark == nil  greeting:set_text('works')"));
        REQUIRE(env->read_boolean("gone") == true);
        REQUIRE(greeting->text == "works");
    }

    SECTION("**a field that holds the object itself does not keep it alive**") {
        interpreter lua;

        register_label(lua);

        auto env = lua.make_environment();

        {
            auto greeting = std::make_shared<label>();

            greeting->destroyed = &destroyed;

            env->write("greeting", greeting);
        }

        REQUIRE_FALSE(env->run("greeting.me = greeting  greeting = nil"));

        collect(lua);

        REQUIRE(destroyed == 1);
    }

    SECTION("**anything still held, by a script or by the host, goes with the interpreter**") {
        {
            interpreter lua;

            register_label(lua);

            auto env = lua.make_environment();

            auto first = std::make_shared<label>();
            auto second = std::make_shared<label>();

            first->destroyed = &destroyed;
            second->destroyed = &destroyed;

            env->write("first", first);

            [[maybe_unused]] auto held = lua.hold(second);
        }

        REQUIRE(destroyed == 2);
    }
}

TEST_CASE("**a registered object's metatable is hidden, and it names itself**", "[userdata][methods]") {
    interpreter lua;

    register_label(lua);

    auto env = lua.make_environment();

    env->write("greeting", std::make_shared<label>());

    REQUIRE_FALSE(env->run(R"(
        meta = tostring(getmetatable(greeting))
        name = tostring(greeting)
        replaced = pcall(setmetatable, greeting, {})
    )"));

    REQUIRE(env->read_string("meta") == "false");
    REQUIRE(env->read_string("name")->rfind("label: ", 0) == 0);
    REQUIRE(env->read_boolean("replaced") == false);
}

TEST_CASE("**a type with a `__call` method is called as a function**", "[userdata][methods][call]") {
    interpreter lua;

    struct spell final { std::string name; int cast = 0; };

    lua.register_type<spell>("spell", {
        {"__call", [](spell &s, value_list_type a) -> value_list_type {
            ++s.cast;

            return {s.name + " at " + (a.empty() ? std::string("nothing") : std::get<std::string>(a[0])),
                static_cast<double>(a.size())};
        }},
        {"name", [](spell &s, value_list_type) -> value_list_type { return {s.name}; }},
    });

    register_label(lua);

    auto env = lua.make_environment();

    const auto fireball = std::make_shared<spell>(spell{"fireball"});

    env->write("fireball", fireball);
    env->write("greeting", std::make_shared<label>());

    SECTION("**called, it is given the arguments after the object, and returns every result**") {
        REQUIRE_FALSE(env->run("said, count = fireball('the troll')  bare = fireball()"));
        REQUIRE(env->read_string("said") == "fireball at the troll");
        REQUIRE(env->read_number("count") == 1);
        REQUIRE(env->read_string("bare") == "fireball at nothing");
        REQUIRE(fireball->cast == 2);
    }

    SECTION("**it keeps its methods beside**") {
        REQUIRE_FALSE(env->run("name = fireball:name()"));
        REQUIRE(env->read_string("name") == "fireball");
    }

    SECTION("**`__call` is not a method: read by name it is a field, nothing unless one was set**") {
        REQUIRE_FALSE(env->run("kind = type(fireball.__call)"));
        REQUIRE(env->read_string("kind") == "nil");
    }

    SECTION("**a type without one is not callable, and says so**") {
        const auto error = env->run("greeting()");

        REQUIRE(error);
        REQUIRE(error->find("call") != std::string::npos);
    }
}

TEST_CASE("**a registered object crosses into c++ as it always did**", "[userdata][methods][params]") {
    interpreter lua;

    register_label(lua);

    auto env = lua.make_environment();

    const auto greeting = std::make_shared<label>();

    env->write("greeting", greeting);

    REQUIRE(env->read_userdata<label>("greeting") == greeting);
    REQUIRE_FALSE(env->read_userdata<button>("greeting"));

    env->register_function("echo", [](value_list_type a) -> value_list_type { return a; });

    REQUIRE_FALSE(env->run("same = echo(greeting) == greeting"));
    REQUIRE(env->read_boolean("same") == true);
}

TEST_CASE("**writing through a reference to something that is neither a table nor an object does "
    "nothing**", "[userdata][hold]") {
    interpreter lua;

    auto env = lua.make_environment();

    REQUIRE_FALSE(env->run("f = function() return 1 end  n = 5"));

    auto function = env->read_reference("f");
    auto number = env->read_reference("n");

    REQUIRE(function);
    REQUIRE(number);

    function->write("x", 1.0);
    number->write("x.y", "z");

    value_list_type out;

    REQUIRE_FALSE(function->call({}, out));
    REQUIRE(std::get<double>(out.at(0)) == 1);
    REQUIRE(env->read_number("n") == 5);
}
