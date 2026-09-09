# © Joseph Cameron - All Rights Reserved

project(jfclua-demo-environments)

add_executable(jfclua-demo-environments
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)

target_link_libraries(jfclua-demo-environments PRIVATE jfclua)

target_compile_features(jfclua-demo-environments PRIVATE cxx_std_20)
