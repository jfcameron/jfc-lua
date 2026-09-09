# © Joseph Cameron - All Rights Reserved

project(jfclua-demo-limits)

add_executable(jfclua-demo-limits
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)

target_link_libraries(jfclua-demo-limits PRIVATE jfclua)

target_compile_features(jfclua-demo-limits PRIVATE cxx_std_20)
