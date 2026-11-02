# © Joseph Cameron - MIT License

cmake_minimum_required(VERSION 3.21 FATAL_ERROR)

project(jfclua_consume LANGUAGES CXX)

find_package(jfclua REQUIRED)

add_executable(jfclua_consume main.cpp)

target_link_libraries(jfclua_consume PRIVATE jfc::lua)

set_target_properties(jfclua_consume PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
