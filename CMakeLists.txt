# © Joseph Cameron - All Rights Reserved

cmake_minimum_required(VERSION 3.9 FATAL_ERROR)

project(null)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/jfc-cmake/jfclib.cmake")

option(JFC_BUILD_DEMO "Build the demo" ON)
option(JFC_BUILD_DOCS "Build documentation" OFF)
option(JFC_BUILD_TESTS "Build unit tests" ON)

set(JFC_LUA_BACKEND "luajit" CACHE STRING "lua implementation to build against: luajit or lua51")
set_property(CACHE JFC_LUA_BACKEND PROPERTY STRINGS "luajit" "lua51")

if (NOT JFC_LUA_BACKEND MATCHES "^(luajit|lua51)$")
    message(FATAL_ERROR
        "JFC_LUA_BACKEND is \"${JFC_LUA_BACKEND}\" but must be one of: luajit, lua51")
endif()

if (JFC_LUA_BACKEND STREQUAL "luajit")
    add_definitions(-DJFC_LUA_LUAJIT)
endif()

set(_jfc_outer_build_tests "${JFC_BUILD_TESTS}")
set(_jfc_outer_build_docs "${JFC_BUILD_DOCS}")
set(_jfc_outer_build_demo "${JFC_BUILD_DEMO}")

set(JFC_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(JFC_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(JFC_BUILD_DEMO OFF CACHE BOOL "" FORCE)

add_subdirectory(extern)

set(JFC_BUILD_TESTS "${_jfc_outer_build_tests}" CACHE BOOL "" FORCE)
set(JFC_BUILD_DOCS "${_jfc_outer_build_docs}" CACHE BOOL "" FORCE)
set(JFC_BUILD_DEMO "${_jfc_outer_build_demo}" CACHE BOOL "" FORCE)

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (MSVC)
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/permissive->)
else()
    if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-pedantic>)
    endif()

    include(CheckCXXCompilerFlag)

    foreach (_strict_flag "-Werror=c++20-extensions" "-Werror=c++23-extensions")
        string(MAKE_C_IDENTIFIER "HAS${_strict_flag}" _strict_flag_symbol)

        check_cxx_compiler_flag("${_strict_flag}" ${_strict_flag_symbol})

        if (${_strict_flag_symbol})
            add_compile_options($<$<COMPILE_LANGUAGE:CXX>:${_strict_flag}>)
        endif()
    endforeach()
endif()

jfc_project(library
    NAME "jfclua"
    VERSION 0.0
    DESCRIPTION "c++ lua wrapper"
    C++_STANDARD 17
    C_STANDARD 90
    TYPE STATIC

    PUBLIC_INCLUDE_DIRECTORIES
        ${CMAKE_CURRENT_SOURCE_DIR}/include

    PRIVATE_INCLUDE_DIRECTORIES
        ${CMAKE_CURRENT_SOURCE_DIR}/src/include
        ${Lua_INCLUDE_DIR}

    SOURCE_LIST
        ${CMAKE_CURRENT_SOURCE_DIR}/src/lua_environment.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/lua_exception.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/lua_interpreter.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/lua_path.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/lua_reference.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/lua_data_table.cpp

    LIBRARIES
        "${Lua_LIBRARIES}"

    DEPENDENCIES
        "${JFC_LUA_TARGET}"
)

if (JFC_BUILD_DEMO)
    add_subdirectory(demo)
endif()

if (JFC_BUILD_TESTS)
    add_subdirectory(test)
endif()

if (JFC_BUILD_DOCS)
    add_subdirectory(docs)
endif()

