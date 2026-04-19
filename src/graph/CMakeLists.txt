# terminal_graph module (header-only; depends on terminal_common for LogCategories)
set(TERMINAL_GRAPH_HEADERS
    Edge.h
    Graph.h
    Algorithms.h
)

add_library(terminal_graph STATIC ${TERMINAL_GRAPH_HEADERS})

target_link_libraries(terminal_graph
    PUBLIC
    terminal_common
)

target_include_directories(terminal_graph PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
