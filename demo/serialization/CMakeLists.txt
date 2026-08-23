# © Joseph Cameron - All Rights Reserved

jfc_project(executable
    NAME "jfclua-demo-serialization"
    VERSION 1.0
    DESCRIPTION "jfc-lua: serialization"
    C++_STANDARD 17
    C_STANDARD 90

    SOURCE_LIST
        ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp

    PRIVATE_INCLUDE_DIRECTORIES
        "${jfclua_INCLUDE_DIRECTORIES}"

    LIBRARIES
        "${jfclua_LIBRARIES}"

    DEPENDENCIES
        "jfclua"
)
