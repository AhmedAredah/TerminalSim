# Add libraries for each component
add_subdirectory(common)
add_subdirectory(dwell_time)
add_subdirectory(graph)
add_subdirectory(terminal)
add_subdirectory(server)

# Create the main target
add_executable(terminal_simulation main.cpp)

# Link libraries to the main target
target_link_libraries(terminal_simulation
    PRIVATE
    terminal_common
    terminal_graph
    terminal_dwell_time
    terminal_core
    terminal_server
    Container::Container
    Qt6::Core
    Qt6::Concurrent
    Qt6::Network
)

# Install the executable
install(TARGETS terminal_simulation DESTINATION bin)
