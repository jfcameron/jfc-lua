# jfc-lua
A library for embedding Lua in a program. It runs scripts in sandboxed environments and passes values, functions and C++ objects between C++ and Lua. It can be built with either LuaJIT or the reference implementation of Lua 5.1.

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

To use it from another project, add it with `add_subdirectory` and link `jfclua`. The Lua implementation is chosen with `JFC_LUA_BACKEND`, either `luajit` (the default) or `lua51`.

## Overview
### Entry point
An interpreter owns a Lua state and the standard library loaded into it. It can run scripts and register functions itself, but those see the interpreter's own globals and the whole standard library, so scripts are usually run in environments instead.

### Environments
An environment is an independent set of global variables, made by an interpreter, and the scripts that run against it. A script in one environment cannot see or change the globals of another, while all of them share the one Lua state, so an environment is cheap to make: one per character, per mod, or per anything else that should not interfere with the rest. The standard library is shared between environments and is read only from inside them, so one cannot replace `string.format` for everyone else.

By default an environment can only reach the parts of the standard library that compute: string, table, math, coroutine, the clock parts of os, and the base functions that cannot reach outside the VM. It can be given the unrestricted standard library instead, but that includes `debug`, which can reach every other environment in the same interpreter and run commands on the system.

Functions registered with an environment are C++ lambdas, and are visible to that environment alone. A C++ exception thrown inside one becomes a Lua error in the script that called it, since a C++ exception cannot unwind through Lua.

### Paths
Values are read and written by path, in the same form Lua uses: `"player.hitbox.bounds.x"`, `"names[1]"`. A path can also be written out as its keys, `{"names", 1}`, for one assembled at runtime.

### Values
Reads return an optional, which is empty if nothing is at the path or what is there is a different type. Arguments to and results from calls are lists of numbers, booleans, strings, nil, data tables and userdata. Anything else, a function for example, is held from C++ through a reference, a handle that keeps the Lua value alive and can be called, read through and written through. Methods that can fail, such as running a script or calling a function, return an optional error message rather than throwing.

### Data tables

A data table is a Lua table restricted to what can be saved: numbers, booleans, strings, and other data tables. Reading one refuses functions, userdata, tables that contain themselves, and nesting deeper than 128 levels, or steps over them if asked to. A data table can be written out as a Lua table literal, and read back from that text without running it as Lua, so a save file is data and cannot run code when it's loaded.

### Userdata
A C++ object is given to Lua as a `std::shared_ptr`, and both sides own it: the object lives until neither C++ nor any script holds it, so neither can leave the other with a dangling pointer. The same object is always the same Lua value, so a script can compare objects and use them as keys. Registering a type gives its objects methods, called as `object:name(arguments)`, and scripts can also set fields of their own on an object. Reading an object back as the wrong type gives nothing rather than a bad cast.

### Coroutines
Any Lua function can be made into a coroutine, which C++ resumes, and which runs until it yields or returns. A function can be registered as yielding, so that a script calling it is suspended until C++ resumes it again: a `wait(seconds)` that a script calls in the middle of a loop, for example, without the script having to be written as a state machine.

### Limits
An interpreter can be given a memory budget for the whole state, a memory budget for how much one run may add to it, and an instruction budget for how much one run may execute. An environment can have an instruction budget of its own. When a script reaches its instruction budget it stops with an error, as it does when allocation goes over a memory budget, unless the program has given a callback that chooses to let it carry on. This stops an infinite loop or a runaway allocation in a script from taking the program down with it. Under LuaJIT, code run under an instruction budget is kept off the JIT, since compiled code is not counted. A call timer can also be set to time every call scripts make into registered functions, to see which of them a script spends its time in.

## Why Lua 5.1?
Because it is what LuaJIT implements, and LuaJIT is by far the fastest Lua there is. Its weakness is that its interpreter is machine code, written for x86, ARM, PPC and MIPS, so it has no WebAssembly target, and cannot have one, since a page in a browser cannot write machine code to run. The reference implementation of Lua 5.1 is plain C, and builds anywhere a C compiler does, Emscripten included, so a browser build uses it. CI builds the Emscripten version and runs its tests under node, where the limits work as they do on the desktop.

Since both are Lua 5.1, the same scripts run on either, as long as they avoid LuaJIT's own extensions, such as `ffi` and the `bit` library.
