find_package(Qt6 COMPONENTS Test REQUIRED)

add_executable(test_terminalsim_logger
    test_terminalsim_logger.cpp
)

target_link_libraries(test_terminalsim_logger
    PRIVATE
    terminal_common
    Qt6::Core
    Qt6::Test
)

add_test(NAME test_terminalsim_logger COMMAND test_terminalsim_logger)

add_executable(test_pathfound_contract
    test_pathfound_contract.cpp
)

target_link_libraries(test_pathfound_contract
    PRIVATE
    terminal_core
    terminal_common
    Qt6::Core
    Qt6::Test
)

add_test(NAME test_pathfound_contract COMMAND test_pathfound_contract)

add_executable(test_terminal_actuals_contract
    test_terminal_actuals_contract.cpp
)

target_link_libraries(test_terminal_actuals_contract
    PRIVATE
    terminal_core
    terminal_server
    terminal_common
    Qt6::Core
    Qt6::Test
)

add_test(NAME test_terminal_actuals_contract COMMAND test_terminal_actuals_contract)
