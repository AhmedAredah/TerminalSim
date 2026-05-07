# installer/deploy/deploy.cmake
#
# Umbrella for Qt + third-party runtime bundling. Each platform has its own
# helper script that runs at `cmake --install` time via install(CODE).
#
# Bundling strategy:
#   * Qt6 runtime  -> windeployqt / macdeployqt / linuxdeploy
#   * RabbitMQ-C   -> copy IMPORTED_LOCATION of rabbitmq::rabbitmq into bin/lib
#   * Container    -> copy IMPORTED_LOCATION of Container::Container similarly
#
# This keeps the install tree self-contained so end users do not need to install
# Qt or RabbitMQ separately.

if(NOT TARGET terminal_simulation)
    message(FATAL_ERROR "deploy.cmake requires the terminal_simulation target.")
endif()

if(WIN32)
    include("${CMAKE_CURRENT_LIST_DIR}/deploy_windows.cmake")
elseif(APPLE)
    include("${CMAKE_CURRENT_LIST_DIR}/deploy_macos.cmake")
elseif(UNIX)
    include("${CMAKE_CURRENT_LIST_DIR}/deploy_linux.cmake")
else()
    message(WARNING "deploy.cmake: unsupported platform; runtime bundling skipped.")
endif()
