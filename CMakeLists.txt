# © Joseph Cameron - All Rights Reserved

cmake_minimum_required(VERSION 3.9 FATAL_ERROR)

project(jfclua
    VERSION 0.0
    DESCRIPTION "c++ lua wrapper")

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

if (NOT TARGET jfclua)

    add_library(jfclua STATIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src/environment.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/exception.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/interpreter.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/path.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/reference.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/data_table.cpp)

    add_library(jfc::lua ALIAS jfclua)

    set_target_properties(jfclua PROPERTIES EXPORT_NAME lua)

    set_target_properties(jfclua PROPERTIES PREFIX "lib")

    jfc_generate_cmake_header()

    target_include_directories(jfclua
        PUBLIC
            "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
            "$<INSTALL_INTERFACE:include>"
        PRIVATE
            "${PROJECT_BINARY_DIR}/generated_include/"
            ${CMAKE_CURRENT_SOURCE_DIR}/src/include
        )

    target_compile_features(jfclua PUBLIC cxx_std_20)

    target_link_libraries(jfclua PUBLIC ${JFC_LUA_TARGET})

endif()

if (JFC_BUILD_DEMO)
    add_subdirectory(demo)
endif()

if (JFC_BUILD_TESTS)
    add_subdirectory(test)
endif()

if (JFC_BUILD_DOCS)
    add_subdirectory(docs)
endif()

if (JFC_LUA_TARGET AND TARGET ${JFC_LUA_TARGET})
    add_dependencies(jfclua ${JFC_LUA_TARGET})
endif()
