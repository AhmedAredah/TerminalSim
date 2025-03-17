cmake_minimum_required(VERSION 3.16)
project(TerminalSimulation VERSION 1.0.0 LANGUAGES CXX)

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
find_package(Qt6 COMPONENTS Core Concurrent Test REQUIRED)

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
if(WIN32)
    # Windows-specific paths
    set(RABBITMQ_CMAKE_DIR "C:/Program Files/rabbitmq-c/lib/cmake/rabbitmq-c" CACHE PATH "Default path to RabbitMQ-C library on Windows")
elseif(APPLE)
    # macOS-specific paths
    set(RABBITMQ_CMAKE_DIR "/usr/local/lib/rabbitmq-c/cmake" CACHE PATH "Default path to RabbitMQ-C library on macOS")
elseif(UNIX)
    # Linux-specific paths
    set(RABBITMQ_CMAKE_DIR "/usr/local/lib/cmake/rabbitmq-c" CACHE PATH "Default path to RabbitMQ-C library on Linux")
else()
    message(FATAL_ERROR "Unsupported platform. Please set paths for CONTAINER_CMAKE_DIR and RABBITMQ_CMAKE_DIR manually.")
endif()

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
find_package(RabbitMQ-C REQUIRED CONFIG PATHS ${RABBITMQ_CMAKE_DIR})
if (NOT RabbitMQ-C_FOUND)
    message(FATAL_ERROR "RabbitMQ-C not found. Please specify the correct path to the RabbitMQ-C cmake installation.")
endif()

# Set and cache the path to the RabbitMQ bin directory using RABBITMQ_CMAKE_DIR
if(WIN32)
    # For Windows, use /bin
    set(RABBITMQ_SHRD_LIB_DIR "${RABBITMQ_CMAKE_DIR}/../../../bin" CACHE PATH "Path to the RabbitMQ-C library's bin directory")
elseif(UNIX AND NOT APPLE)
    # For Linux, use /lib or /lib64 (adjust based on your setup)
    set(RABBITMQ_SHRD_LIB_DIR "${RABBITMQ_CMAKE_DIR}/../../" CACHE PATH "Path to the RabbitMQ-C library's bin directory")
elseif(APPLE)
    # For macOS, use /lib or /lib64 (adjust based on your setup)
    set(RABBITMQ_SHRD_LIB_DIR "${RABBITMQ_CMAKE_DIR}/../../" CACHE PATH "Path to the RabbitMQ-C library's bin directory")
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
add_subdirectory(tests)
add_subdirectory(examples)

# Install targets
# install(DIRECTORY include/ DESTINATION include)
install(TARGETS terminal_simulation
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib)

# Package configuration
include(InstallRequiredSystemLibraries)
set(CPACK_PACKAGE_VENDOR "Virginia Tech Transportation Institute - Center for Sustainable Mobility.")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Terminal Simulation - A C++ library for simulating container terminals")
set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})
include(CPack)
