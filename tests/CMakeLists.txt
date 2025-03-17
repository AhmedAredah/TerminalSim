# Enable Qt's meta-object compiler
set(CMAKE_AUTOMOC ON)

# Find test package
find_package(Qt6 COMPONENTS Test REQUIRED)

# Define helper function for tests
function(add_terminal_test TEST_NAME TEST_SOURCES)
    add_executable(${TEST_NAME} ${TEST_SOURCES})
    target_link_libraries(${TEST_NAME}
        PRIVATE
        terminal_common
        terminal_dwell_time
        terminal_core
        terminal_server
        Container::Container
        Qt6::Test
        Qt6::Core
    )
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endfunction()

# Dwell time tests
add_terminal_test(test_dwell_time test_dwell_time.cpp)

# Terminal tests
add_terminal_test(test_terminal test_terminal.cpp)

# Terminal graph tests
add_terminal_test(test_terminal_graph test_terminal_graph.cpp)

# Server tests
add_terminal_test(test_server test_server.cpp)

# Enable testing in this directory
enable_testing()
