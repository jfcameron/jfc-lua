// © Joseph Cameron - MIT License

#include <jfc/catch.hpp>
#include <lua.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>

namespace {
    using lua_state = std::unique_ptr<lua_State, decltype(&lua_close)>;

    lua_state make_state() {
        lua_state state{luaL_newstate(), &lua_close};
        REQUIRE(state);
        return state;
    }

    void write(lua_State *const apState, const double aKey, const lua_Integer aValue) {
        lua_pushnumber(apState, aKey);
        lua_pushinteger(apState, aValue);
        lua_settable(apState, 1);
    }

    void check(lua_State *const apState, const double aKey, const lua_Integer aValue) {
        CAPTURE(aKey);
        lua_pushnumber(apState, aKey);
        lua_gettable(apState, 1);
        REQUIRE(lua_type(apState, -1) == LUA_TNUMBER);
        REQUIRE(lua_tointeger(apState, -1) == aValue);
        lua_pop(apState, 1);
        REQUIRE(lua_gettop(apState) == 1);
    }
} // namespace

TEST_CASE("numeric table probes preserve keys beyond the integer range", "[numeric-keys]") {
    const auto state = make_state();
    auto *const pState = state.get();
    const auto low = static_cast<double>(std::numeric_limits<int>::min());
    const auto high = static_cast<double>(std::numeric_limits<int>::max());
    const auto infinity = std::numeric_limits<double>::infinity();
    const std::array keys{0.,
                          1.,
                          -1.,
                          low,
                          high,
                          low - 1.,
                          high + 1.,
                          low - .5,
                          high + .5,
                          std::nextafter(low, -infinity),
                          std::nextafter(high, infinity),
                          4294967295.,
                          4294967296.,
                          -4294967296.,
                          9007199254740991.,
                          9007199254740992.,
                          .5,
                          -.5,
                          std::numeric_limits<double>::min(),
                          -std::numeric_limits<double>::min(),
                          std::numeric_limits<double>::denorm_min(),
                          -std::numeric_limits<double>::denorm_min(),
                          std::numeric_limits<double>::max(),
                          std::numeric_limits<double>::lowest(),
                          infinity,
                          -infinity};
    lua_newtable(pState);
    for (std::size_t i = 0; i < keys.size(); ++i)
        write(pState, keys[i], static_cast<lua_Integer>(i + 1));
    for (std::size_t i = 0; i < keys.size(); ++i)
        check(pState, keys[i], static_cast<lua_Integer>(i + 1));
    std::size_t count = 0;
    lua_pushnil(pState);
    while (lua_next(pState, 1)) {
        ++count;
        lua_pop(pState, 1);
    }
    REQUIRE(count == keys.size());
    REQUIRE(lua_gettop(pState) == 1);
}

TEST_CASE("array growth and rehash keep fractional and large numeric hash keys", "[numeric-keys]") {
    const auto state = make_state();
    auto *const pState = state.get();
    const auto infinity = std::numeric_limits<double>::infinity();
    const std::array keys{4294967295., -4294967296., .25, -.75, infinity, -infinity};
    lua_newtable(pState);
    for (std::size_t i = 0; i < keys.size(); ++i)
        write(pState, keys[i], static_cast<lua_Integer>(2000 + i));
    for (int i = 1; i <= 1024; ++i) {
        lua_pushinteger(pState, i);
        lua_rawseti(pState, 1, i);
    }
    for (std::size_t i = 0; i < keys.size(); ++i)
        check(pState, keys[i], static_cast<lua_Integer>(2000 + i));
    for (int i = 1; i <= 1024; ++i)
        check(pState, i, i);
    REQUIRE(lua_objlen(pState, 1) == 1024);
    for (const auto key : keys) {
        lua_pushnumber(pState, key);
        lua_pushnil(pState);
        lua_settable(pState, 1);
        lua_pushnumber(pState, key);
        lua_gettable(pState, 1);
        REQUIRE(lua_isnil(pState, -1));
        lua_pop(pState, 1);
    }
    REQUIRE(lua_objlen(pState, 1) == 1024);
    std::size_t count = 0;
    lua_pushnil(pState);
    while (lua_next(pState, 1)) {
        ++count;
        lua_pop(pState, 1);
    }
    REQUIRE(count == 1024);
}

TEST_CASE("numeric probes retain zero aliasing and reject NaN writes", "[numeric-keys]") {
    const auto state = make_state();
    auto *const pState = state.get();
    lua_newtable(pState);
    write(pState, 0., 7);
    check(pState, -0., 7);
    write(pState, -0., 9);
    check(pState, 0., 9);
    lua_pushnumber(pState, std::numeric_limits<double>::quiet_NaN());
    lua_gettable(pState, 1);
    REQUIRE(lua_isnil(pState, -1));
    lua_pop(pState, 1);
    lua_pushcfunction(pState, [](lua_State *const apState) -> int {
        lua_newtable(apState);
        lua_pushnumber(apState, std::numeric_limits<double>::quiet_NaN());
        lua_pushboolean(apState, true);
        lua_settable(apState, -3);
        return 0;
    });
    REQUIRE(lua_pcall(pState, 0, 0, 0) == LUA_ERRRUN);
    REQUIRE(lua_type(pState, -1) == LUA_TSTRING);
    lua_pop(pState, 1);
    check(pState, 0., 9);
}
