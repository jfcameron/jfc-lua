# © Joseph Cameron - All Rights Reserved

jfc_add_tests(
    C++_STANDARD 20
    C_STANDARD 90

    TEST_SOURCE_FILES
        "${CMAKE_CURRENT_LIST_DIR}/array_test.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/call_test.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/closure_boundary_test.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/data_table_access_test.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/data_table_dense_test.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/data_table_storable_test.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/data_table_test.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/data_table_text_test.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/environment_test.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/interpreter_test.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/keyed_access_test.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/limits_test.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/path_test.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/reference_test.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/stack_balance_test.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/userdata_crossing_test.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/userdata_test.cpp"

    LIBRARIES
        jfclua
)
