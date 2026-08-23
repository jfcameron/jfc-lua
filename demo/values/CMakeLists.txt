# © Joseph Cameron - All Rights Reserved

project(jfclua-demo-values)

add_executable(jfclua-demo-values
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)

target_link_libraries(jfclua-demo-values PRIVATE jfclua)

target_compile_features(jfclua-demo-values PRIVATE cxx_std_20)
