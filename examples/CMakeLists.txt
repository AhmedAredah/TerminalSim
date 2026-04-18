# Verification example for the 2026-04-17 time-unit unification.
add_executable(time_units_verification time_units_verification.cpp)

target_link_libraries(time_units_verification
    PRIVATE
    terminal_common
    terminal_core
    Container::Container
    Qt6::Core
)
