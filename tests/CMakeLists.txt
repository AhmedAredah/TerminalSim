find_package(Qt6 COMPONENTS Test REQUIRED)
enable_testing()

add_executable(test_terminalsim_logger
    test_terminalsim_logger.cpp
)

target_link_libraries(test_terminalsim_logger
    PRIVATE
    terminal_common
    Qt6::Test
)

add_test(NAME test_terminalsim_logger COMMAND test_terminalsim_logger)
