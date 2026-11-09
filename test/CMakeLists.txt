# © Joseph Cameron - MIT License

jfc_add_tests(
    C++_STANDARD 20

    TEST_SOURCE_FILES
        "${CMAKE_CURRENT_LIST_DIR}/array_test.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/call_test.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/call_timer_test.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/coroutine_test.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/closure_boundary_test.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/data_table_access_test.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/exception_test.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/data_table_dense_test.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/data_table_storable_test.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/data_table_test.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/data_table_text_test.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/environment_test.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/function_argument_test.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/interpreter_test.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/keyed_access_test.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/limits_test.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/path_test.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/protected_test.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/reference_test.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/registered_test.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/stack_balance_test.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/table_reader_test.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/table_view_test.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/typed_call_test.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/typed_function_test.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/typed_method_test.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/userdata_crossing_test.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/userdata_methods_test.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/userdata_test.cpp"

    LIBRARIES
        jfclua
)
