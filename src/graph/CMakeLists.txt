#terminal_graph module

set(TERMINAL_GRAPH_SOURCES
)

set(TERMINAL_GRAPH_HEADERS
    Edge.h
    Graph.h
    Algorithms.h
)

add_library(terminal_graph STATIC ${TERMINAL_GRAPH_SOURCES} ${TERMINAL_GRAPH_HEADERS})

target_include_directories(terminal_graph PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
