# Dwell time module
set(DWELL_TIME_SOURCES
    container_dwell_time.cpp
)

set(DWELL_TIME_HEADERS
    container_dwell_time.h
)

add_library(terminal_dwell_time STATIC ${DWELL_TIME_SOURCES} ${DWELL_TIME_HEADERS})
target_link_libraries(terminal_dwell_time
    PUBLIC
    terminal_common
    Container::Container
    Qt6::Core
)

target_include_directories(terminal_dwell_time PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})