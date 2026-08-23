// © Joseph Cameron - All Rights Reserved

#include <jfc/catch.hpp>

#include <jfc/lua.h>

#include <memory>
#include <string>

using namespace jfc::lua;

namespace {
    struct body final {
        int id = 0;
        int *destroyed = nullptr;

        body(const int aId, int *const aDestroyed) : id(aId), destroyed(aDestroyed) {}

        ~body() { if (destroyed) ++*destroyed; }
    };

    struct texture final { std::string name; };

    struct base { virtual ~base() = default; };
    struct derived final : base {};
}

TEST_CASE("a bound object comes back as what it was bound as", "[userdata]") {
    int destroyed = 0;

    const auto held = userdata::make(std::make_shared<body>(7, &destroyed));

    SECTION("and carries its value") {
        const auto out = held.get<body>();

        REQUIRE(out);
        REQUIRE(out->id == 7);
    }

    SECTION("it knows what it holds without being asked to produce it") {
        REQUIRE(held.holds<body>());
        REQUIRE_FALSE(held.holds<texture>());
    }

    SECTION("cv qualifiers do not make it a different binding") {
        REQUIRE(held.holds<const body>());

        const std::shared_ptr<const body> out = held.get<const body>();

        REQUIRE(out);
        REQUIRE(out->id == 7);
    }
}

TEST_CASE("asking for the wrong type is answered, not reinterpreted", "[userdata]") {
    int destroyed = 0;

    const auto held = userdata::make(std::make_shared<body>(1, &destroyed));

    REQUIRE_FALSE(held.get<texture>());
    REQUIRE_FALSE(held.get<int>());
    REQUIRE_FALSE(held.get<std::string>());

    SECTION("including a type it really is, if that is not how it was bound") {
        const std::shared_ptr<base> asBase = std::make_shared<derived>();

        const auto bound = userdata::make(asBase);

        REQUIRE(bound.holds<base>());
        REQUIRE_FALSE(bound.holds<derived>());
        REQUIRE(bound.get<base>());
        REQUIRE_FALSE(bound.get<derived>());
    }

    SECTION("and an empty one holds nothing at all") {
        const userdata nothing;

        REQUIRE(nothing.empty());
        REQUIRE_FALSE(nothing.holds<body>());
        REQUIRE_FALSE(nothing.get<body>());
    }
}

TEST_CASE("ownership is shared, so neither side can dangle the other", "[userdata]") {
    int destroyed = 0;

    SECTION("the binding keeps the object alive after c++ lets go") {
        auto original = std::make_shared<body>(1, &destroyed);

        const auto held = userdata::make(original);

        REQUIRE(held.use_count() == 2);

        original.reset();

        REQUIRE(destroyed == 0);
        REQUIRE(held.use_count() == 1);
        REQUIRE(held.get<body>()->id == 1);
    }

    SECTION("and c++ keeps it alive after the binding goes") {
        auto original = std::make_shared<body>(2, &destroyed);

        {
            const auto held = userdata::make(original);

            REQUIRE(held.use_count() == 2);
        }

        REQUIRE(destroyed == 0);
        REQUIRE(original->id == 2);
    }

    SECTION("it is destroyed once, when the last owner lets go") {
        {
            auto original = std::make_shared<body>(3, &destroyed);

            const auto held = userdata::make(original);

            original.reset();

            REQUIRE(destroyed == 0);
        }

        REQUIRE(destroyed == 1);
    }

    SECTION("the right destructor runs, though it is held type erased") {
        {
            const auto held = userdata::make(std::make_shared<body>(4, &destroyed));

            REQUIRE(destroyed == 0);
        }

        REQUIRE(destroyed == 1);
    }
}

TEST_CASE("copies share the object", "[userdata]") {
    int destroyed = 0;

    const auto first = userdata::make(std::make_shared<body>(5, &destroyed));

    SECTION("a copy refers to the same thing") {
        const auto second = first;

        REQUIRE(second == first);
        REQUIRE(second.get<body>() == first.get<body>());
        REQUIRE(first.use_count() == 2);
    }

    SECTION("and letting one go does not destroy it") {
        {
            const auto second = first;
        }

        REQUIRE(destroyed == 0);
        REQUIRE(first.get<body>()->id == 5);
    }

    SECTION("two bindings of different objects are not equal") {
        const auto other = userdata::make(std::make_shared<body>(6, &destroyed));

        REQUIRE(other != first);
    }

    SECTION("two bindings of the same object are") {
        const auto shared = std::make_shared<body>(7, &destroyed);

        REQUIRE(userdata::make(shared) == userdata::make(shared));
    }
}

TEST_CASE("a type's identity is stable and unique", "[userdata]") {
    int destroyed = 0;

    const auto one = userdata::make(std::make_shared<body>(1, &destroyed));
    const auto two = userdata::make(std::make_shared<body>(2, &destroyed));
    const auto other = userdata::make(std::make_shared<texture>());

    REQUIRE(one.type_key() == two.type_key());
    REQUIRE(one.type_key() != other.type_key());
    REQUIRE(one.type_key() != nullptr);

    SECTION("an empty binding has none") {
        REQUIRE(userdata().type_key() == nullptr);
    }
}
