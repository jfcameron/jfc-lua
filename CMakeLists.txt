# © Joseph Cameron - All Rights Reserved

cmake_minimum_required(VERSION 3.9 FATAL_ERROR)

project(null)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/jfc-cmake/jfclib.cmake")

option(JFC_BUILD_DEMO "Build the demo" ON)
option(JFC_BUILD_DOCS "Build documentation" OFF)
option(JFC_BUILD_TESTS "Build unit tests" ON)

add_subdirectory(thirdparty)

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
        ${LuaJIT_INCLUDE_DIR}

    SOURCE_LIST
        ${CMAKE_CURRENT_SOURCE_DIR}/src/lua.cpp

    LIBRARIES
        "${LuaJIT_LIBRARIES}"

    DEPENDENCIES
        "libluajit"
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

