# © Joseph Cameron - All Rights Reserved

project(jfclua-demo-serialization)

add_executable(jfclua-demo-serialization
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)

target_link_libraries(jfclua-demo-serialization PRIVATE jfclua)

target_compile_features(jfclua-demo-serialization PRIVATE cxx_std_20)
