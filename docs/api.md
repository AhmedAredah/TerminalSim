# TerminalSimulation API Documentation

## Table of Contents

- [TerminalSimulation API Documentation](#terminalsimulation-api-documentation)
  - [Table of Contents](#table-of-contents)
  - [Introduction](#introduction)
  - [Namespace](#namespace)
  - [Core Components](#core-components)
    - [Common](#common)
    - [Dwell Time](#dwell-time)
    - [Terminal](#terminal)
    - [Terminal Graph](#terminal-graph)
    - [Server](#server)
  - [Detailed Class Reference](#detailed-class-reference)
    - [Enumerations](#enumerations)
      - [TransportationMode](#transportationmode)
      - [TerminalInterface](#terminalinterface)
    - [ContainerDwellTime Class](#containerdwelltime-class)
      - [Methods](#methods)
        - [Example](#example)
    - [Terminal Class](#terminal-class)
      - [Construction](#construction)
      - [Key Methods](#key-methods)
        - [Example](#example-1)
    - [TerminalGraph Class](#terminalgraph-class)
      - [Construction](#construction-1)
      - [Key Methods](#key-methods-1)
        - [Example](#example-2)
    - [TerminalGraphServer Class](#terminalgraphserver-class)
      - [Access](#access)
      - [Key Methods](#key-methods-2)
        - [Example](#example-3)
    - [RabbitMQHandler Class](#rabbitmqhandler-class)
      - [Construction](#construction-2)
      - [Key Methods](#key-methods-3)
    - [CommandProcessor Class](#commandprocessor-class)
      - [Construction](#construction-3)
      - [Key Methods](#key-methods-4)
  - [Advanced Usage](#advanced-usage)
    - [Path Finding](#path-finding)
    - [RabbitMQ Integration](#rabbitmq-integration)
    - [Thread Safety](#thread-safety)
  - [Examples](#examples)
    - [Basic Terminal Creation](#basic-terminal-creation)
    - [Network Simulation](#network-simulation)

## Introduction

The TerminalSimulation API provides a comprehensive set of tools for modeling container terminals and transportation networks. This document outlines the key components, classes, and methods available to developers.

The API is designed with a focus on:
- Clean separation of concerns
- Thread safety for parallel operations
- Flexible configuration options
- Robust error handling
- Integration with messaging systems

## Namespace

All components in the TerminalSimulation library reside within the `TerminalSim` namespace to prevent naming conflicts.

```cpp
using namespace TerminalSim;  // Access all components
```

## Core Components

### Common

The common module provides fundamental definitions used throughout the library:

- Enumerations for transportation modes
- Interface types for terminals
- Utility functions for string conversions

### Dwell Time

The dwell time module implements statistical distributions for modeling container dwell times:

- Gamma distribution
- Exponential distribution
- Normal distribution
- Lognormal distribution

### Terminal

The terminal module provides classes for modeling container terminal operations:

- Container storage and management
- Capacity constraints
- Customs operations
- Cost calculations

### Terminal Graph

The terminal graph module implements a network of interconnected terminals:

- Multi-modal transportation network modeling
- Path finding algorithms
- Region-based operations
- Terminal connectivity management

### Server

The server module provides communication capabilities:

- RabbitMQ integration for messaging
- Command processing
- Thread safety for concurrent operations
- Terminal graph management

## Detailed Class Reference

### Enumerations

#### TransportationMode

```cpp
enum class TransportationMode {
    Truck = 0,
    Train = 1,
    Ship = 2
};
```

Represents the possible transportation modes for containers.

#### TerminalInterface

```cpp
enum class TerminalInterface {
    LAND_SIDE = 0,
    SEA_SIDE = 1,
    RAIL_SIDE = 2
};
```

Represents the different interfaces available at terminals.

### ContainerDwellTime Class

Static methods for generating dwell times using various statistical distributions.

#### Methods

```cpp
static double gammaDistributionDwellTime(double shape, double scale);
static double exponentialDistributionDwellTime(double scale);
static double normalDistributionDwellTime(double mean, double stdDev);
static double lognormalDistributionDwellTime(double mean, double sigma);
static double getDepartureTime(double arrivalTime, const QString& method, const QVariantMap& params);
```

##### Example

```cpp
// Generate dwell time using gamma distribution
double shape = 2.0;
double scale = 24.0 * 3600.0;  // 24 hours in seconds
double dwellTime = ContainerDwellTime::gammaDistributionDwellTime(shape, scale);

// Calculate departure time
double arrivalTime = QDateTime::currentSecsSinceEpoch();
QVariantMap params;
params["shape"] = shape;
params["scale"] = scale;

double departureTime = ContainerDwellTime::getDepartureTime(arrivalTime, "gamma", params);
```

### Terminal Class

Represents a container terminal with configurable properties.

#### Construction

```cpp
Terminal(
    const QString& terminalName,
    const QMap<TerminalInterface, QSet<TransportationMode>>& interfaces,
    const QMap<QPair<TransportationMode, QString>, QString>& modeNetworkAliases = {},
    const QVariantMap& capacity = {},
    const QVariantMap& dwellTime = {},
    const QVariantMap& customs = {},
    const QVariantMap& cost = {},
    const QString& pathToTerminalFolder = QString()
);
```

#### Key Methods

```cpp
// Terminal properties
QString getTerminalName() const;
const QMap<TerminalInterface, QSet<TransportationMode>>& getInterfaces() const;

// Alias management
QString getAliasByModeNetwork(TransportationMode mode, const QString& network) const;
void addAliasForModeNetwork(TransportationMode mode, const QString& network, const QString& alias);

// Capacity management
QPair<bool, QString> checkCapacityStatus(int additionalContainers) const;
int getContainerCount() const;
int getAvailableCapacity() const;
int getMaxCapacity() const;

// Container handling estimation
double estimateContainerHandlingTime() const;
double estimateContainerCost(const Container::Container* container = nullptr, bool applyCustoms = false) const;
bool canAcceptTransport(TransportationMode mode, TerminalInterface side) const;

// Container operations
void addContainer(const Container::Container& container, double addingTime = -1);
void addContainers(const QList<Container::Container>& containers, double addingTime = -1);
void addContainersFromJson(const QJsonObject& containers, double addingTime = -1);
QJsonArray getContainersByDepatingTime(double departingTime, const QString& condition = "<") const;
QJsonArray getContainersByAddedTime(double addedTime, const QString& condition) const;
QJsonArray getContainersByNextDestination(const QString& destination) const;
QJsonArray dequeueContainersByNextDestination(const QString& destination);
void clear();

// Serialization
QJsonObject toJson() const;
static Terminal* fromJson(const QJsonObject& json, const QString& pathToTerminalFolder = QString());
```

##### Example

```cpp
// Create a terminal with truck and ship interfaces
QMap<TerminalInterface, QSet<TransportationMode>> interfaces;
interfaces[TerminalInterface::LAND_SIDE].insert(TransportationMode::Truck);
interfaces[TerminalInterface::SEA_SIDE].insert(TransportationMode::Ship);

// Configure capacity
QVariantMap capacity;
capacity["max_capacity"] = 1000;
capacity["critical_threshold"] = 0.8;

// Configure dwell time
QVariantMap dwellTime;
dwellTime["method"] = "gamma";
dwellTime["parameters"] = QVariantMap{{"shape", 2.0}, {"scale", 24.0 * 3600.0}};

// Create terminal
Terminal terminal("Port_Terminal", interfaces, {}, capacity, dwellTime);

// Add a container
Container::Container container(QJsonObject{{"containerID", "CONT123"}, {"containerSize", 40}});
terminal.addContainer(container, QDateTime::currentSecsSinceEpoch());

// Check capacity
int availableCapacity = terminal.getAvailableCapacity();
int containerCount = terminal.getContainerCount();
```

### TerminalGraph Class

Represents a network of interconnected terminals supporting multiple transportation modes.

#### Construction

```cpp
explicit TerminalGraph(const QString& pathToTerminalsDirectory = QString());
```

#### Key Methods

```cpp
// Terminal management
void addTerminal(
    const QStringList& terminalNames,
    const QVariantMap& customConfig,
    const QMap<TerminalInterface, QSet<TransportationMode>>& terminalInterfaces,
    const QString& region = QString()
);
void addAliasToTerminal(const QString& terminalName, const QString& alias);
QStringList getAliasesOfTerminal(const QString& terminalName) const;
Terminal* getTerminal(const QString& terminalName) const;
bool terminalExists(const QString& terminalName) const;
bool removeTerminal(const QString& terminalName);
int getTerminalCount() const;
QMap<QString, QStringList> getAllTerminalNames(bool includeAliases = false) const;
QVariantMap getTerminalStatus(const QString& terminalName = QString()) const;
void clear();

// Route management
void addRoute(
    const QString& routeId,
    const QString& startTerminal,
    const QString& endTerminal,
    TransportationMode mode,
    const QVariantMap& attributes = QVariantMap()
);
QVariantMap getEdgeByMode(const QString& startTerminal, 
                          const QString& endTerminal, 
                          TransportationMode mode) const;
void changeRouteWeight(
    const QString& startTerminal,
    const QString& endTerminal,
    TransportationMode mode,
    const QVariantMap& newAttributes
);

// Region operations
QStringList getTerminalsByRegion(const QString& region) const;
QList<QVariantMap> getRoutesBetweenRegions(const QString& regionA, const QString& regionB) const;

// Auto-connection
void connectTerminalsByInterfaceModes();
void connectTerminalsInRegionByMode(const QString& region);
void connectRegionsByMode(TransportationMode mode);

// Path finding
QList<PathSegment> findShortestPath(
    const QString& startTerminal,
    const QString& endTerminal,
    TransportationMode mode = TransportationMode::Truck
) const;

QList<PathSegment> findShortestPathWithinRegions(
    const QString& startTerminal,
    const QString& endTerminal,
    const QStringList& allowedRegions,
    TransportationMode mode = TransportationMode::Truck
) const;

QList<Path> findTopNShortestPaths(
    const QString& startTerminal,
    const QString& endTerminal,
    int n = 5,
    TransportationMode mode = TransportationMode::Truck,
    bool skipSameModeTerminalDelaysAndCosts = true
) const;

// Serialization
QJsonObject serializeGraph() const;
static TerminalGraph* deserializeGraph(
    const QJsonObject& graphData, 
    const QString& pathToTerminalsDirectory = QString()
);

void saveToFile(const QString& filepath) const;
static TerminalGraph* loadFromFile(
    const QString& filepath,
    const QString& pathToTerminalsDirectory = QString()
);
```

##### Example

```cpp
// Create a graph
TerminalGraph graph("/path/to/terminals");

// Add terminals
QMap<TerminalInterface, QSet<TransportationMode>> interfaces;
interfaces[TerminalInterface::LAND_SIDE].insert(TransportationMode::Truck);

QVariantMap customConfig;
customConfig["capacity"] = QVariantMap{{"max_capacity", 1000}};

graph.addTerminal(QStringList{"TerminalA"}, customConfig, interfaces, "RegionA");
graph.addTerminal(QStringList{"TerminalB"}, customConfig, interfaces, "RegionB");

// Add route
QVariantMap attributes;
attributes["distance"] = 100.0;
attributes["travellTime"] = 2.0;

graph.addRoute("Route1", "TerminalA", "TerminalB", TransportationMode::Truck, attributes);

// Find shortest path
auto path = graph.findShortestPath("TerminalA", "TerminalB", TransportationMode::Truck);

// Find top N paths
auto topPaths = graph.findTopNShortestPaths("TerminalA", "TerminalB", 3);

// Save graph
graph.saveToFile("/path/to/graph.json");
```

### TerminalGraphServer Class

Singleton server that manages a TerminalGraph instance and provides RabbitMQ integration.

#### Access

```cpp
static TerminalGraphServer* getInstance(const QString& pathToTerminalsDirectory = QString());
```

#### Key Methods

```cpp
// Initialization
bool initialize(
    const QString& rabbitMQHost = "localhost",
    int rabbitMQPort = 5672,
    const QString& rabbitMQUser = "guest",
    const QString& rabbitMQPassword = "guest"
);
void shutdown();
bool isConnected() const;

// Command processing
QVariant processCommand(const QString& command, const QVariantMap& params);

// Graph serialization
QJsonObject serializeGraph() const;
bool deserializeGraph(const QJsonObject& graphData);
bool saveGraph(const QString& filepath) const;
bool loadGraph(const QString& filepath);
```

##### Example

```cpp
// Get server instance
TerminalGraphServer* server = TerminalGraphServer::getInstance("/path/to/terminals");

// Initialize server
server->initialize("localhost", 5672, "guest", "guest");

// Process a command
QVariantMap params;
params["terminal_names"] = QStringList{"NewTerminal"};
params["custom_config"] = QVariantMap{{"capacity", QVariantMap{{"max_capacity", 1000}}}};
params["terminal_interfaces"] = QVariantMap{{"0", QVariantList{0}}};
params["region"] = "NewRegion";

QVariant result = server->processCommand("add_terminal", params);

// Save current graph
server->saveGraph("/path/to/graph.json");

// Shutdown server
server->shutdown();
```

### RabbitMQHandler Class

Handles communication with RabbitMQ server for messaging.

#### Construction

```cpp
explicit RabbitMQHandler(QObject* parent = nullptr);
```

#### Key Methods

```cpp
bool connect(
    const QString& host = "localhost",
    int port = 5672,
    const QString& username = "guest",
    const QString& password = "guest"
);
void disconnect();
bool isConnected() const;
bool sendResponse(const QJsonObject& message);
```

### CommandProcessor Class

Processes commands for the terminal graph server.

#### Construction

```cpp
explicit CommandProcessor(TerminalGraph* graph, QObject* parent = nullptr);
```

#### Key Methods

```cpp
QVariant processCommand(const QString& command, const QVariantMap& params);
QJsonObject processJsonCommand(const QJsonObject& commandObject);
```

## Advanced Usage

### Path Finding

The TerminalGraph class provides advanced path finding capabilities:

1. **Shortest Path**: Find the optimal path between two terminals.
   ```cpp
   auto path = graph.findShortestPath("TerminalA", "TerminalB", TransportationMode::Ship);
   ```

2. **Region-Constrained Paths**: Find paths that stay within specific regions.
   ```cpp
   auto path = graph.findShortestPathWithinRegions(
       "TerminalA", "TerminalB", QStringList{"RegionA", "RegionB"}, TransportationMode::Truck
   );
   ```

3. **K-Shortest Paths**: Find the top N shortest paths between terminals.
   ```cpp
   auto paths = graph.findTopNShortestPaths("TerminalA", "TerminalB", 5, TransportationMode::Train);
   
   // Access the best path
   TerminalGraph::Path bestPath = paths[0];
   
   // Print path details
   qDebug() << "Path cost:" << bestPath.totalPathCost;
   qDebug() << "Edge costs:" << bestPath.totalEdgeCosts;
   qDebug() << "Terminal costs:" << bestPath.totalTerminalCosts;
   
   // Print path segments
   for (const auto& segment : bestPath.segments) {
       qDebug() << "From:" << segment.from << "To:" << segment.to 
                << "Mode:" << static_cast<int>(segment.mode);
   }
   ```

### RabbitMQ Integration

The TerminalGraphServer integrates with RabbitMQ for distributed messaging:

1. **Server Initialization**: Connect to RabbitMQ server.
   ```cpp
   TerminalGraphServer* server = TerminalGraphServer::getInstance();
   server->initialize("rabbitmq-host", 5672, "username", "password");
   ```

2. **Command Processing**: Process commands received via RabbitMQ.
   ```cpp
   // Commands are automatically processed when received
   // You can also manually process commands
   QVariantMap params = { /* command parameters */ };
   QVariant result = server->processCommand("command_name", params);
   ```

3. **Custom Command Handlers**: Extend CommandProcessor to add custom handlers.
   ```cpp
   class MyCommandProcessor : public CommandProcessor {
   public:
       MyCommandProcessor(TerminalGraph* graph) : CommandProcessor(graph) {
           registerCommand("my_custom_command", [this](const QVariantMap& params) {
               // Process custom command
               return QVariant("Custom result");
           });
       }
   };
   ```

### Thread Safety

The TerminalSimulation API is designed for thread safety:

1. **Mutex Protection**: Critical sections are protected with mutexes.
   ```cpp
   // Inside class methods
   QMutexLocker locker(&m_mutex);
   ```

2. **Lock Order**: When acquiring multiple locks, always follow the same order.
   ```cpp
   // Correct order
   QMutexLocker graphLocker(&m_graphMutex);
   QMutexLocker terminalLocker(&m_terminalMutex);
   ```

3. **Deadlock Prevention**: Release locks before calling external methods.
   ```cpp
   QMutexLocker locker(&m_mutex);
   // Collect necessary data
   Terminal* terminal = m_terminals[terminalName];
   // Release lock before calling external method
   locker.unlock();
   // Call method that might acquire its own lock
   terminal->someMethod();
   ```

## Examples

### Basic Terminal Creation

```cpp
#include <TerminalSimulation/terminal.h>
#include <QJsonObject>

using namespace TerminalSim;

// Create terminal interfaces
QMap<TerminalInterface, QSet<TransportationMode>> interfaces;
interfaces[TerminalInterface::LAND_SIDE].insert(TransportationMode::Truck);
interfaces[TerminalInterface::SEA_SIDE].insert(TransportationMode::Ship);

// Create terminal configuration
QVariantMap capacity;
capacity["max_capacity"] = 5000;
capacity["critical_threshold"] = 0.85;

QVariantMap dwellTime;
dwellTime["method"] = "gamma";
dwellTime["parameters"] = QVariantMap{
    {"shape", 2.0},
    {"scale", 24.0 * 3600.0} // 24 hours in seconds
};

QVariantMap customs;
customs["probability"] = 0.1;
customs["delay_mean"] = 48.0; // 48 hours
customs["delay_variance"] = 12.0;

QVariantMap cost;
cost["fixed_fees"] = 200.0;
cost["customs_fees"] = 100.0;
cost["risk_factor"] = 0.01;

// Create terminal
Terminal terminal(
    "Main_Terminal",
    interfaces,
    {}, // No aliases
    capacity,
    dwellTime,
    customs,
    cost,
    "/path/to/storage"
);

// Get terminal information
qDebug() << "Terminal:" << terminal.getTerminalName();
qDebug() << "Max capacity:" << terminal.getMaxCapacity();
qDebug() << "Available capacity:" << terminal.getAvailableCapacity();
qDebug() << "Estimated handling time:" << terminal.estimateContainerHandlingTime() << "seconds";
```

### Network Simulation

```cpp
#include <TerminalSimulation/terminal_graph.h>
#include <TerminalSimulation/dwell_time/container_dwell_time.h>
#include <QJsonDocument>
#include <QFile>

using namespace TerminalSim;

// Create a transportation network
TerminalGraph graph("/path/to/data");

// Define terminal configurations
QMap<TerminalInterface, QSet<TransportationMode>> portInterfaces;
portInterfaces[TerminalInterface::LAND_SIDE].insert(TransportationMode::Truck);
portInterfaces[TerminalInterface::SEA_SIDE].insert(TransportationMode::Ship);

QMap<TerminalInterface, QSet<TransportationMode>> hubInterfaces;
hubInterfaces[TerminalInterface::LAND_SIDE].insert(TransportationMode::Truck);
hubInterfaces[TerminalInterface::RAIL_SIDE].insert(TransportationMode::Train);

QVariantMap portConfig;
portConfig["capacity"] = QVariantMap{{"max_capacity", 10000}};
portConfig["dwell_time"] = QVariantMap{
    {"method", "gamma"},
    {"parameters", QVariantMap{{"shape", 2.0}, {"scale", 48.0 * 3600.0}}}
};

QVariantMap hubConfig;
hubConfig["capacity"] = QVariantMap{{"max_capacity", 5000}};
hubConfig["dwell_time"] = QVariantMap{
    {"method", "normal"},
    {"parameters", QVariantMap{{"mean", 24.0 * 3600.0}, {"std_dev", 6.0 * 3600.0}}}
};

// Add terminals
graph.addTerminal(QStringList{"Port_A", "PA"}, portConfig, portInterfaces, "Region_East");
graph.addTerminal(QStringList{"Port_B", "PB"}, portConfig, portInterfaces, "Region_West");
graph.addTerminal(QStringList{"Hub_A", "HA"}, hubConfig, hubInterfaces, "Region_Central");
graph.addTerminal(QStringList{"Hub_B", "HB"}, hubConfig, hubInterfaces, "Region_Central");

// Add routes
QVariantMap seaRouteAttrs;
seaRouteAttrs["distance"] = 1000.0;
seaRouteAttrs["travellTime"] = 72.0;
seaRouteAttrs["cost"] = 1500.0;
seaRouteAttrs["carbonEmissions"] = 500.0;

QVariantMap roadRouteAttrs;
roadRouteAttrs["distance"] = 250.0;
roadRouteAttrs["travellTime"] = 5.0;
roadRouteAttrs["cost"] = 300.0;
roadRouteAttrs["carbonEmissions"] = 150.0;

QVariantMap railRouteAttrs;
railRouteAttrs["distance"] = 300.0;
railRouteAttrs["travellTime"] = 8.0;
railRouteAttrs["cost"] = 250.0;
railRouteAttrs["carbonEmissions"] = 50.0;

graph.addRoute("Sea_AB", "Port_A", "Port_B", TransportationMode::Ship, seaRouteAttrs);
graph.addRoute("Road_AH", "Port_A", "Hub_A", TransportationMode::Truck, roadRouteAttrs);
graph.addRoute("Road_BH", "Port_B", "Hub_B", TransportationMode::Truck, roadRouteAttrs);
graph.addRoute("Rail_HH", "Hub_A", "Hub_B", TransportationMode::Train, railRouteAttrs);

// Find optimal paths
QList<Path> paths = graph.findTopNShortestPaths("Port_A", "Port_B", 3);

// Save results
QJsonObject results;
QJsonArray pathsArray;

for (const Path& path : paths) {
    QJsonObject pathObj;
    pathObj["total_cost"] = path.totalPathCost;
    pathObj["total_edge_costs"] = path.totalEdgeCosts;
    pathObj["total_terminal_costs"] = path.totalTerminalCosts;
    
    QJsonArray segmentsArray;
    for (const PathSegment& segment : path.segments) {
        QJsonObject segmentObj;
        segmentObj["from"] = segment.from;
        segmentObj["to"] = segment.to;
        segmentObj["mode"] = static_cast<int>(segment.mode);
        segmentObj["weight"] = segment.weight;
        segmentsArray.append(segmentObj);
    }
    
    pathObj["segments"] = segmentsArray;
    pathsArray.append(pathObj);
}

results["paths"] = pathsArray;

// Save to file
QFile file("simulation_results.json");
if (file.open(QIODevice::WriteOnly)) {
    file.write(QJsonDocument(results).toJson(QJsonDocument::Indented));
    file.close();
}
```