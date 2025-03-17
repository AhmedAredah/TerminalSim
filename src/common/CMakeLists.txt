# Common module
set(COMMON_SOURCES
    common.cpp
)

set(COMMON_HEADERS
    common.h
)

add_library(terminal_common STATIC ${COMMON_SOURCES} ${COMMON_HEADERS})
target_link_libraries(terminal_common
    PUBLIC
    Qt6::Core
)

target_include_directories(terminal_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})