set(CMAKE_AUTOMOC ON) # enable Qt's meta-object compiler

# Terminal module
set(TERMINAL_SOURCES
    terminal.cpp
    terminal_graph.cpp
)

set(TERMINAL_HEADERS
    terminal.h
    terminal_graph.h
)

add_library(terminal_core STATIC ${TERMINAL_SOURCES} ${TERMINAL_HEADERS})
target_link_libraries(terminal_core
    PUBLIC
    terminal_common
    terminal_dwell_time
    Container::Container
    Qt6::Core
    Qt6::Concurrent
)

target_include_directories(terminal_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
