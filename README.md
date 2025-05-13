<h1 align="center">
  <a href="https://github.com/VTTI-CSM/TerminalSim">
    <img src="https://github.com/user-attachments/assets/40967d47-1d00-4eea-b590-b395b9cb25e0" alt="TerminalSim" width="200px"/>

  </a>
  <br/>
  TerminalSim [Terminal Simulator]
</h1>

<p align="center">
<!--   <a href="http://dx.doi.org/10.1109/SM63044.2024.10733439">
    <img src="https://zenodo.org/badge/DOI/10.1109/SM63044.2024.10733439.svg" alt="DOI">
  </a> -->
  <a href="https://www.gnu.org/licenses/gpl-3.0">
    <img src="https://img.shields.io/badge/License-GPLv3-blue.svg" alt="License: GNU GPL v3">
  </a>
  <a href="https://github.com/VTTI-CSM/TerminalSim/releases">
    <img alt="GitHub tag (latest by date)" src="https://img.shields.io/github/v/tag/VTTI-CSM/TerminalSim.svg?label=latest">
  </a>
  <img alt="GitHub All Releases" src="https://img.shields.io/github/downloads/VTTI-CSM/TerminalSim/total.svg">
  <a href="">
    <img src="https://img.shields.io/badge/CLA-CLA%20Required-red" alt="CLA Required">
    <a href="https://cla-assistant.io/VTTI-CSM/TerminalSim"><img src="https://cla-assistant.io/readme/badge/VTTI-CSM/TerminalSim" alt="CLA assistant" /></a>
  </a>
</p>

<p align="center">
  <a href="https://github.com/VTTI-CSM/TerminalSim/releases" target="_blank">Download TerminalSim</a> |
  <a href="https://VTTI-CSM.github.io/TerminalSim/" target="_blank">Documentation</a>
</p>

<p align="center">
  <a> For questions or feedback, contact <a href='mailto:AhmedAredah@vt.edu'>Ahmed Aredah</a> or <a href='mailto:HRakha@vtti.vt.edu'>Prof. Hesham Rakha<a>
</p>

<p align="center">
  <strong>TerminalSim is a server-side tool without a GUI, designed to operate in conjunction with <a href="https://github.com/VTTI-CSM/CargoNetSim" target="_blank">CargoNetSim</a>.</strong>
</p>

## Overview

TerminalSim is a comprehensive C++ library for modeling and simulating container terminals and transportation networks. The project provides tools for researchers, logistics professionals, and developers to create realistic terminal models, optimize routing, and analyze container handling operations.

Key features include:
- Terminal modeling with customizable parameters for capacity, dwell time, customs operations, and costs
- Multi-modal transportation network modeling (truck, train, ship)
- Path-finding algorithms for optimal container routing
- Statistical distributions for realistic container dwell time simulation
- RabbitMQ integration for microservices communication
- Thread-safe operations for high-performance simulations

## Table of Contents

- [TerminalSim](#terminalsim)
  - [Overview](#overview)
  - [Table of Contents](#table-of-contents)
  - [Prerequisites](#prerequisites)
    - [Container Library](#container-library)
  - [Building from Source](#building-from-source)
    - [Linux](#linux)
    - [macOS](#macos)
    - [Windows](#windows)
  - [Installation](#installation)
  - [Usage](#usage)
    - [Integrating with CMake Projects](#integrating-with-cmake-projects)
    - [Basic Code Example](#basic-code-example)
  - [Project Structure](#project-structure)
  - [Dependencies](#dependencies)
  - [Future Development](#future-development)
  - [License](#license)

## Prerequisites

Before building TerminalSim, ensure you have the following installed:

- C++17 compatible compiler (GCC 7+, Clang 5+, MSVC 2019+)
- CMake 3.16 or higher
- Qt 6.0 or higher
- RabbitMQ-C library
- Container library (see below)

### Container Library

TerminalSim depends on the Container library, which you can download and install from:
[https://github.com/AhmedAredah/container](https://github.com/AhmedAredah/container)

Follow the installation instructions in the Container repository before proceeding with TerminalSim.

## Building from Source

### Linux

```bash
# Clone the repository
git clone https://github.com/AhmedAredah/TerminalSim.git
cd TerminalSim

# Create build directory
mkdir build && cd build

# Configure with CMake
cmake .. -DCMAKE_BUILD_TYPE=Release

# Build
make -j$(nproc)

# Run tests (optional)
ctest

# Install
sudo make install
```

### macOS

```bash
# Clone the repository
git clone https://github.com/AhmedAredah/TerminalSim.git
cd TerminalSim

# Create build directory
mkdir build && cd build

# Configure with CMake
cmake .. -DCMAKE_BUILD_TYPE=Release

# Build
make -j$(sysctl -n hw.ncpu)

# Run tests (optional)
ctest

# Install
sudo make install
```

### Windows

Using PowerShell or Command Prompt with Visual Studio:

```powershell
# Clone the repository
git clone https://github.com/AhmedAredah/TerminalSim.git
cd TerminalSim

# Create build directory
mkdir build
cd build

# Configure with CMake
cmake .. -G "Visual Studio 17 2022" -A x64

# Build
cmake --build . --config Release

# Run tests (optional)
ctest -C Release

# Install
cmake --install . --config Release
```

Using Windows with MinGW:

```powershell
# Clone the repository
git clone https://github.com/AhmedAredah/TerminalSim.git
cd TerminalSim

# Create build directory
mkdir build
cd build

# Configure with CMake
cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release

# Build
mingw32-make -j4

# Run tests (optional)
ctest

# Install
mingw32-make install
```

## Installation

After building, the library can be installed system-wide:

```bash
# Linux/macOS
sudo make install

# Windows (run as Administrator)
cmake --install . --config Release
```

This will install:
- Library files to the system library directory
- Header files to the include directory
- CMake configuration files for easy integration with other projects

## Usage

### Integrating with CMake Projects

To use TerminalSim in your CMake project:

```cmake
find_package(TerminalSim REQUIRED)

add_executable(your_application main.cpp)
target_link_libraries(your_application TerminalSim::TerminalSim)
```

### Basic Code Example

```cpp
#include <TerminalSim/terminal.h>
#include <TerminalSim/terminal_graph.h>
#include <iostream>

int main() {
    using namespace TerminalSim;
    
    // Create a terminal graph
    TerminalGraph graph("/path/to/terminal/data");
    
    // Define terminal configurations
    QMap<TerminalInterface, QSet<TransportationMode>> interfaces;
    interfaces[TerminalInterface::LAND_SIDE].insert(TransportationMode::Truck);
    interfaces[TerminalInterface::SEA_SIDE].insert(TransportationMode::Ship);
    
    QVariantMap customConfig;
    customConfig["capacity"] = QVariantMap{{"max_capacity", 1000}};
    
    // Add terminals to the graph
    graph.addTerminal(QStringList{"TerminalA"}, customConfig, interfaces, "RegionA");
    graph.addTerminal(QStringList{"TerminalB"}, customConfig, interfaces, "RegionB");
    
    // Add route between terminals
    QVariantMap routeAttributes;
    routeAttributes["distance"] = 100.0;
    routeAttributes["travellTime"] = 2.0;
    
    graph.addRoute("Route1", "TerminalA", "TerminalB", TransportationMode::Ship, routeAttributes);
    
    // Find shortest path
    auto path = graph.findShortestPath("TerminalA", "TerminalB", TransportationMode::Ship);
    
    // Display path information
    std::cout << "Path segments: " << path.size() << std::endl;
    for (const auto& segment : path) {
        std::cout << "From: " << segment.from.toStdString() 
                  << " To: " << segment.to.toStdString() 
                  << " Mode: " << static_cast<int>(segment.mode) << std::endl;
    }
    
    return 0;
}
```

## Project Structure

```
terminalSim/
├── CMakeLists.txt                  # Main CMake file
├── src/                            # Source code directory
│   ├── common/                     # Common definitions and utilities
│   ├── dwell_time/                 # Container dwell time distributions
│   ├── terminal/                   # Terminal and graph implementation
│   ├── server/                     # RabbitMQ server integration
│   └── main.cpp                    # Application entry point
├── tests/                          # Test directory
├── examples/                       # Example applications
└── docs/                           # Documentation
```

## Dependencies

- **Qt 6**: Core, Concurrent, and Test modules
- **RabbitMQ-C**: C client library for RabbitMQ
- **Container**: Container management library ([GitHub](https://github.com/AhmedAredah/container))

## Future Development

This project provides a basic terminal representation framework. Future development aims to model the entire process of container terminals, including:

- Advanced scheduling algorithms
- Machine learning integration for predictive terminal operations
- Visualization tools for terminal simulation
- Real-time analytics and performance metrics

## License

This project is licensed under the GNU General Public License v3.0 - see the [LICENSE](LICENSE) file for details.

---

© 2025 TerminalSim Project Contributors
