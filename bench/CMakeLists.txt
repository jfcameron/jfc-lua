# © Joseph Cameron - MIT License

add_executable(jfclua_bench "${CMAKE_CURRENT_LIST_DIR}/bench.cpp")

target_link_libraries(jfclua_bench PRIVATE jfclua)

target_compile_definitions(jfclua_bench PRIVATE JFC_LUA_BENCH_BACKEND="${JFC_LUA_BACKEND}")

set_target_properties(jfclua_bench PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)

target_compile_options(jfclua_bench PRIVATE $<IF:$<CXX_COMPILER_ID:MSVC>,/O2,-O2>)
