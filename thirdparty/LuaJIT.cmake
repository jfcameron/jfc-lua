# © Joseph Cameron - All Rights Reserved

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/LuaJIT")

#set(LUAJIT_INCLUDE_DIR "${LUAJIT_INCLUDE_DIR}" PARENT_SCOPE)
#set(LUAJIT_LIBRARIES "${LUAJIT_LIBRARIES}" PARENT_SCOPE)

jfc_set_dependency_symbols(
    INCLUDE_PATHS
        "${LUAJIT_INCLUDE_DIR}"

    LIBRARIES
        "${LUAJIT_LIBRARIES}"
)

