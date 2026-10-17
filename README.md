## jfc-lua

c++ lua library. strong separation between interface and implementation. via cmake flags users can select either the standard lua implementation or jit. c++20 conforming

### In a browser

LuaJIT has no WebAssembly target: its interpreter is machine code, and a page cannot write any to run. Under Emscripten the backend defaults to `lua51`, and `luajit` is refused with that reason. Both are Lua 5.1, so scripts that keep to it (no `ffi`, no `bit` beyond what lua51 ships) run on either. The `emscripten` preset builds the library and its tests for node, where `ctest` runs them: the instruction budget's count hook and the memory budget's allocator work there as on the desktop.

