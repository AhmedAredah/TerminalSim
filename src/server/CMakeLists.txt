# Server module
set(SERVER_SOURCES
    terminal_graph_server.cpp
    rabbit_mq_handler.cpp
    command_processor.cpp
)

set(SERVER_HEADERS
    terminal_graph_server.h
    rabbit_mq_handler.h
    command_processor.h
)

add_library(terminal_server STATIC ${SERVER_SOURCES} ${SERVER_HEADERS})
target_link_libraries(terminal_server
    PUBLIC
    terminal_common
    terminal_core
    Container::Container
    rabbitmq::rabbitmq
    Qt6::Core
    Qt6::Concurrent
)

target_include_directories(terminal_server PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
