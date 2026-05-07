cmake_minimum_required(VERSION 3.16)
project(TerminalSim VERSION 1.0.0 LANGUAGES CXX)

# Enable Qt features
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Set default build type if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Output directories
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Find Qt packages
find_package(Qt6 COMPONENTS Core Concurrent Network Test REQUIRED)

# Find Container library
if(WIN32)
    # Windows-specific paths
    set(CONTAINER_SEARCH_PATHS "C:/Program Files/Container/cmake" CACHE PATH "Default path to container's library")
elseif(APPLE)
    # macOS-specific paths
    set(CONTAINER_SEARCH_PATHS "/usr/local/lib/cmake/Container" CACHE PATH "Default path to container's library on macOS")
elseif(UNIX)
    # Linux-specific paths
    set(CONTAINER_SEARCH_PATHS "/usr/local/lib/cmake/Container" CACHE PATH "Default path to container's library on Linux")
else()
    message(FATAL_ERROR "Unsupported platform. Please set path for CONTAINER_CMAKE_DIR manually.")
endif()

# Find RabbitMQ-C library
set(RABBITMQ_CMAKE_DIR "" CACHE PATH
    "Optional explicit path to the RabbitMQ-C CMake package directory")
set(RABBITMQ_SEARCH_PATHS "")

if(WIN32)
    list(APPEND RABBITMQ_SEARCH_PATHS
        "${RABBITMQ_CMAKE_DIR}"
        "C:/Program Files/rabbitmq-c/lib/cmake/rabbitmq-c")
elseif(APPLE)
    list(APPEND RABBITMQ_SEARCH_PATHS
        "${RABBITMQ_CMAKE_DIR}"
        "/usr/local/lib/rabbitmq-c/cmake"
        "/usr/local/lib/cmake/rabbitmq-c")
elseif(UNIX)
    # Support the local split-install layout used by this environment, where
    # release and debug RabbitMQ-C packages are deployed under /usr/local.
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        list(APPEND RABBITMQ_SEARCH_PATHS
            "${RABBITMQ_CMAKE_DIR}"
            "/usr/local/Debug/lib/cmake/rabbitmq-c"
            "/usr/local/lib/cmake/rabbitmq-c"
            "/usr/local/Release/lib/cmake/rabbitmq-c"
            "/usr/lib/cmake/rabbitmq-c")
    else()
        list(APPEND RABBITMQ_SEARCH_PATHS
            "${RABBITMQ_CMAKE_DIR}"
            "/usr/local/Release/lib/cmake/rabbitmq-c"
            "/usr/local/lib/cmake/rabbitmq-c"
            "/usr/local/Debug/lib/cmake/rabbitmq-c"
            "/usr/lib/cmake/rabbitmq-c")
    endif()
else()
    message(FATAL_ERROR "Unsupported platform. Please set paths for CONTAINER_CMAKE_DIR and RABBITMQ_CMAKE_DIR manually.")
endif()

list(FILTER RABBITMQ_SEARCH_PATHS EXCLUDE REGEX "^$")
list(REMOVE_DUPLICATES RABBITMQ_SEARCH_PATHS)

# Find the installed Container library
set(CONTAINER_CMAKE_DIR "${CONTAINER_SEARCH_PATHS}" CACHE PATH "Path to Container library's CMake files")
find_package(Container REQUIRED PATHS ${CONTAINER_CMAKE_DIR} NO_DEFAULT_PATH)
if (NOT Container_FOUND)
    message(FATAL_ERROR "Container not found. Please specify the correct path to the Container Library cmake installation.")
endif()

# Check if the directory exists
if(NOT EXISTS "${CONTAINER_LIB_DIR}")
    message(FATAL_ERROR "The specified CONTAINER_LIB_DIR does not exist: ${CONTAINER_LIB_DIR}")
endif()

# Find RabbitMQ-C
find_package(RabbitMQ-C REQUIRED CONFIG PATHS ${RABBITMQ_SEARCH_PATHS})
if (NOT RabbitMQ-C_FOUND)
    message(FATAL_ERROR
        "RabbitMQ-C not found. Checked: ${RABBITMQ_SEARCH_PATHS}. "
        "Please specify the correct path to the RabbitMQ-C CMake installation.")
endif()

# Derive the RabbitMQ shared-library directory from the imported target that
# was actually found, instead of assuming a fixed install layout.
set(_rabbitmq_imported_location "")
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    get_target_property(_rabbitmq_imported_location rabbitmq::rabbitmq IMPORTED_LOCATION_DEBUG)
endif()
if(NOT _rabbitmq_imported_location)
    get_target_property(_rabbitmq_imported_location rabbitmq::rabbitmq IMPORTED_LOCATION_RELEASE)
endif()
if(NOT _rabbitmq_imported_location)
    get_target_property(_rabbitmq_imported_location rabbitmq::rabbitmq IMPORTED_LOCATION_RELWITHDEBINFO)
endif()
if(NOT _rabbitmq_imported_location)
    get_target_property(_rabbitmq_imported_location rabbitmq::rabbitmq IMPORTED_LOCATION_MINSIZEREL)
endif()
if(NOT _rabbitmq_imported_location)
    get_target_property(_rabbitmq_imported_location rabbitmq::rabbitmq IMPORTED_LOCATION)
endif()

if(_rabbitmq_imported_location)
    get_filename_component(_rabbitmq_lib_dir "${_rabbitmq_imported_location}" DIRECTORY)
    set(RABBITMQ_SHRD_LIB_DIR "${_rabbitmq_lib_dir}" CACHE PATH
        "Path to the RabbitMQ-C shared library directory" FORCE)
endif()

# Add compile options
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -Wall -Wextra -Werror")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")

# Define RABBITMQ settings as compile definitions
add_compile_definitions(
    CARGONET_EXCHANGE="CargoNetSim.Exchange"
    CARGONET_COMMAND_QUEUE="CargoNetSim.CommandQueue.TerminalSim"
    CARGONET_RESPONSE_QUEUE="CargoNetSim.ResponseQueue.TerminalSim"
    CARGONET_COMMAND_ROUTING_KEY="CargoNetSim.Command.TerminalSim"
    CARGONET_RESPONSE_ROUTING_KEY="CargoNetSim.Response.TerminalSim"
)

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

# Add subdirectories
add_subdirectory(src)
enable_testing()
add_subdirectory(tests)
add_subdirectory(examples)

# Install rules (componentized so installers can split runtime / service / docs)
install(TARGETS terminal_simulation
        RUNTIME DESTINATION bin           COMPONENT Runtime
        LIBRARY DESTINATION lib           COMPONENT Runtime
        ARCHIVE DESTINATION lib           COMPONENT Runtime)

install(FILES LICENSE README.md
        DESTINATION share/terminalsim
        COMPONENT Runtime)

# Optional installer subproject (CPack + Qt Installer Framework, deploy, service files).
option(BUILD_INSTALLER "Build the cross-platform installer subproject" OFF)
if(BUILD_INSTALLER)
    add_subdirectory(installer)
endif()
