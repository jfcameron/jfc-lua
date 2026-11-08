# jfc-lua
A library for embedding Lua in a program. It runs scripts in sandboxed environments and passes values, functions and C++ objects between C++ and Lua. It can be built with either LuaJIT or the reference implementation of Lua 5.1.

API documentation: https://jfcameron.github.io/jfc-lua/

## Getting Started
Install cmake 3.21 or greater, and a c++20 compatible compiler
```
# initialize the tree
git submodule update --init --recursive

# generate the script to build the library and its tests on linux using gcc
cmake --preset linux-gcc

# build the library and tests
cmake --build --preset linux-gcc

# run the tests
ctest --preset linux-gcc
```
The `linux-gcc-demo` preset builds the demo programs instead, and `linux-gcc-lua51` builds against the reference implementation rather than LuaJIT. See `CMakePresets.json` for the full list of supported platforms and compilers. See `.github/workflows/ci.yml` to see exactly how CI runners build it on different platforms.

To use it from another project, add it with `add_subdirectory` and link `jfclua`. The Lua implementation is chosen with `JFC_LUA_BACKEND`, either `luajit` (the default) or `lua51` (Lua 5.1.5, the default for Emscripten, iOS, tvOS, watchOS and visionOS, where `luajit` is refused); generation reports which it is.

The reference backend uses the LuaDist `5.1.5` submodule's `src/` library sources and stock `luaconf.h.orig`, copied to the build directory as `luaconf.h`. It is built as C++, with jfc-lua's numeric conversion and platform settings, independently of LuaDist's build configuration and optional relative-path loader.

## Overview
### Entry point
An interpreter owns a Lua state and the standard library loaded into it. It can run scripts and register functions itself, but those see the interpreter's own globals and the whole standard library, so scripts are usually run in environments instead.

### Environments
An environment is an independent set of global variables, made by an interpreter, and the scripts that run against it. A script in one environment cannot see or change the globals of another, while all of them share the one Lua state, so an environment is cheap to make: one per character, per mod, or per anything else that should not interfere with the rest. The standard library is shared between environments and is read only from inside them, so one cannot replace `string.format` for everyone else.

By default an environment can only reach the parts of the standard library that compute: string, table, math, coroutine, the clock parts of os, and the base functions that cannot reach outside the VM. It can be given the unrestricted standard library instead, but that includes `debug`, which can reach every other environment in the same interpreter and run commands on the system.

Functions registered with an environment are C++ lambdas, and are visible to that environment alone. A C++ exception thrown inside one becomes a Lua error in the script that called it, since a C++ exception cannot unwind through Lua. A registered function lives in the Lua state, so one that holds an environment or reference by `std::shared_ptr` keeps the whole interpreter alive: hold a `std::weak_ptr` instead.

### Typed functions
A registered function can say what it takes in its own signature, and the library reads and checks the arguments for it:

```cpp
env->register_function("gas.set", [&](const int aCell, const std::string_view aMixture,
    const std::optional<double> aPressure) {
    world.set_gas(aCell, aMixture, aPressure.value_or(1.0));
});
```

A call that does not fit is a Lua error naming the function and the argument, such as `gas.set: argument 2 wants a string, got a number`. A number given for an integer must be whole and fit its type, a `std::optional` may be left out or nil, and no more arguments may be given than the function takes. A `std::string_view` needs no copy and is valid for the call. A function can return a `std::tuple` for several results, and an empty `std::optional` is nil.

A `table_view` reads a script's table where it is, for the length of the call, instead of copying it into a data table:

```cpp
env->register_function("firework", [&](const table_view &aOptions) {
    launch(aOptions.get_number("height").value_or(10), aOptions.get_table("colour")->get_number("red").value_or(1));
});
```

A table it reads may hold anything, since only what is asked for is looked at, and no metamethods run. A nested view gives its place on the Lua stack back when it is destroyed. Use `to_data_table` to keep the contents.

A `function_reference` is a function the script passed, to call during the call or to keep and call later. It runs in the environment that made it, and under the instruction budget of the environment it was given to. A `reference` takes any value but nil in the same way, so a table can be kept without a copy, and read later as it is by then. Either can be returned. Keeping one inside something Lua owns, such as a registered closure's captures, keeps the interpreter's state alive for good.

A trailing `value_list_type` takes every argument left, for a function that is only partly fixed. On its own it is a function whose arguments are decided at run time, which remains an ordinary way to write one, though typed functions are faster.

### Script names
`run` and `validate_syntax` take a name for the script, usually its file, which errors use for it and for every function it defines: `rules.lua:3: the bucket leaks`. Without one, Lua quotes the start of the script instead.

### Paths
Values are read and written by path, in the same form Lua uses: `"player.hitbox.bounds.x"`, `"names[1]"`. A path can also be written out as its keys, `{"names", 1}`, for one assembled at runtime.

### Values
Reads return an optional, which is empty if nothing is at the path or what is there is a different type. Arguments to and results from calls are lists of numbers, booleans, strings, nil, data tables and userdata. Anything else, a function for example, is held from C++ through a reference, a handle that keeps the Lua value alive and can be called, read through and written through. Methods that can fail, such as running a script or calling a function, return an optional error message rather than throwing.

### Data tables

A data table is a Lua table restricted to what can be saved: numbers, booleans, strings, and other data tables. Reading one refuses functions, userdata, tables that contain themselves, and nesting deeper than 128 levels, or steps over them if asked to. A data table can be written out as a Lua table literal, and read back from that text without running it as Lua, so a save file is data and cannot run code when it's loaded. `to_string()` writes it on one line; `to_string(4)` writes a field a line, indented four spaces a level, for a file people read and diff. `get` gives whatever a key holds, as a `data_table::value_type`, and `set` takes one back, so a value is copied without asking for each type in turn.

### Reading a format
A `table_reader` reads a data table as a file format says it is. It is made with the fields the format has, and refuses a table holding any other, so a misspelt field is an error rather than a value quietly left at its default. `get` reads a field as the kind it names and refuses any other, saying where it is and what was wanted, as a typed call does:

```cpp
const auto table = data_table::from_string(text);
const table_reader read(table, "sky.lua", {"version", "label", "colour", "phases"});

static_cast<void>(read.get<int>("version", {.least = 1, .most = 1}));

const auto label = read.get<std::optional<std::string>>("label");
const auto colour = read.get<std::array<float, 3>>("colour", {.least = 0, .most = 1});

for (const auto &phase : read.tables("phases", {"hour", "sun"})) hours.push_back(phase.get<double>("hour"));
```

A `std::optional` may be absent, as it may be nil in a typed function. A `double` or `float` takes a finite number, a whole number type a number that is whole and fits it, a `std::vector` a list, and a `std::array` a list of exactly its size. A table in the table is read by a reader of its own, from `table` or, for a list of them, `tables`, and its refusals say its path: `sky.lua: phases[2].hour wants a number, got a string`. `refused` makes a refusal of the format's own, after where the reader is.

### Userdata
A C++ object is given to Lua as a `std::shared_ptr`, and both sides own it: the object lives until neither C++ nor any script holds it, so neither can leave the other with a dangling pointer. The same object is always the same Lua value, so a script can compare objects and use them as keys. Registering a type gives its objects methods, called as `object:name(arguments)`, and scripts can also set fields of their own on an object. Reading an object back as the wrong type gives nothing rather than a bad cast.

Methods can say what they take the same way, each named with `method`. The first parameter is the object, and arguments are numbered from the one after it. Methods taking value lists can sit among them, so a type can move to this one method at a time:

```cpp
lua.register_type<label>("label",
    method("set_text", [](label &aLabel, const std::string_view aText) { aLabel.text = aText; }),
    method("text", [](const label &aLabel) { return aLabel.text; }));
```

### Coroutines
Any Lua function can be made into a coroutine, which C++ resumes, and which runs until it yields or returns. A function can be registered as yielding, so that a script calling it is suspended until C++ resumes it again: a `wait(seconds)` that a script calls in the middle of a loop, for example, without the script having to be written as a state machine.

### Calling Lua from C++
`call` takes its arguments directly and names what it wants back, instead of going through value lists:

```cpp
if (const auto damage = hook.call<double>(1.5, "fire")) apply(*damage);
else log(damage.error());
```

A result of the wrong type is an error, such as `on.hit: result 1 wants a number, got a string`, and a `std::tuple` asks for several. With no type named it returns the `error_type` alone, as a call of a value list does. Results are owned, so a `std::string_view` or a `table_view` cannot be asked for.

A `path` built once is faster than a string parsed for every call, and a `reference` taken with `read_reference` is faster again, since it skips finding the function: about half the time of a call by name. Use one for anything called every frame.

### Limits
An interpreter can be given a memory budget for the whole state, a memory budget for how much one run may add to it, and an instruction budget for how much one run may execute. An environment can have an instruction budget of its own. When a script reaches its instruction budget it stops with an error, as it does when allocation goes over a memory budget, unless the program has given a callback that chooses to let it carry on. This stops an infinite loop or a runaway allocation in a script from taking the program down with it. Under LuaJIT, code run under an instruction budget is kept off the JIT, since compiled code is not counted. A call timer can also be set to time every call scripts make into registered functions, to see which of them a script spends its time in.

## Installing
`cmake --install` installs the library, its Lua backend and its headers, and `find_package(jfclua)` provides `jfc::lua`. No Lua header is installed, as none is needed to use the library. `test/consume` is a project that does this.

## Benchmarks
The `linux-gcc-bench` and `linux-gcc-lua51-bench` presets build `jfclua_bench`, which times calls in each direction against the raw Lua API, and counts allocations exactly.

## Why Lua 5.1?
Because it is what LuaJIT implements, and LuaJIT is by far the fastest Lua there is. Its weakness is that its interpreter is machine code, written for x86, ARM, PPC and MIPS, so it has no WebAssembly target, and cannot have one, since a page in a browser cannot write machine code to run. The reference implementation of Lua 5.1 is plain C, and builds anywhere a C compiler does, Emscripten included, so a browser build uses it. Apple's devices use it too. LuaJIT runs there without its JIT, which they forbid, and unwinds its errors itself rather than through the C++ runtime, so an error raised while jfc-lua's C++ code holds objects would skip their destructors. The reference implementation is built as C++, so its errors are C++ exceptions and unwind like any other. CI builds the Emscripten version and runs its tests under node, where the limits work as they do on the desktop.

Since both are Lua 5.1, the same scripts run on either, as long as they avoid LuaJIT's own extensions, such as `ffi` and the `bit` library.
