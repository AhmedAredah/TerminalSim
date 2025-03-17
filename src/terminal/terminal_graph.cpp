#include "terminal_graph.h"

#include <QDebug>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDir>
#include <QQueue>
#include <QPair>
#include <QSet>
#include <QRandomGenerator>
#include <QThread>
#include <QCoreApplication>
#include <cmath>
#include <limits>
#include <algorithm>
#include <stdexcept>

namespace TerminalSim {

// Private implementation of graph structure using adjacency list
class TerminalGraph::GraphImpl {
public:
    // Internal edge structure
    struct InternalEdge {
        QString to;
        QString routeId;
        TransportationMode mode;
        QVariantMap attributes;
        
        bool operator==(const InternalEdge& other) const {
            return to == other.to && 
                   routeId == other.routeId && 
                   mode == other.mode;
        }
    };

    // Graph structure: node -> list of edges
    QHash<QString, QList<InternalEdge>> adjacencyList;
    
    // Node attributes
    QHash<QString, QVariantMap> nodeAttributes;
    
    // Find all edges between nodes
    QList<InternalEdge> getEdges(const QString& from, const QString& to) const {
        QList<InternalEdge> result;
        
        if (!adjacencyList.contains(from)) {
            return result;
        }
        
        const QList<InternalEdge>& edges = adjacencyList[from];
        for (const InternalEdge& edge : edges) {
            if (edge.to == to) {
                result.append(edge);
            }
        }
        
        return result;
    }
    
    // Find specific edge by mode
    InternalEdge* findEdge(const QString& from, const QString& to, TransportationMode mode) {
        if (!adjacencyList.contains(from)) {
            return nullptr;
        }
        
        QList<InternalEdge>& edges = adjacencyList[from];
        for (InternalEdge& edge : edges) {
            if (edge.to == to && edge.mode == mode) {
                return &edge;
            }
        }
        
        return nullptr;
    }
    
    // Add an edge to the graph
    void addEdge(const QString& from, const QString& to, const QString& routeId, 
                 TransportationMode mode, const QVariantMap& attributes) {
        // Ensure nodes exist
        if (!adjacencyList.contains(from)) {
            adjacencyList[from] = QList<InternalEdge>();
        }
        if (!adjacencyList.contains(to)) {
            adjacencyList[to] = QList<InternalEdge>();
        }
        
        // Create edge
        InternalEdge edge;
        edge.to = to;
        edge.routeId = routeId;
        edge.mode = mode;
        edge.attributes = attributes;
        
        // Check for duplicate
        QList<InternalEdge>& edges = adjacencyList[from];
        for (int i = 0; i < edges.size(); ++i) {
            if (edges[i].to == to && edges[i].mode == mode) {
                // Update existing edge
                edges[i] = edge;
                
                // Also add reverse edge for undirected graph
                InternalEdge reverseEdge;
                reverseEdge.to = from;
                reverseEdge.routeId = routeId;
                reverseEdge.mode = mode;
                reverseEdge.attributes = attributes;
                
                bool found = false;
                QList<InternalEdge>& reverseEdges = adjacencyList[to];
                for (int j = 0; j < reverseEdges.size(); ++j) {
                    if (reverseEdges[j].to == from && reverseEdges[j].mode == mode) {
                        reverseEdges[j] = reverseEdge;
                        found = true;
                        break;
                    }
                }
                
                if (!found) {
                    adjacencyList[to].append(reverseEdge);
                }
                
                return;
            }
        }
        
        // Add new edge
        adjacencyList[from].append(edge);
        
        // Add reverse edge for undirected graph
        InternalEdge reverseEdge;
        reverseEdge.to = from;
        reverseEdge.routeId = routeId;
        reverseEdge.mode = mode;
        reverseEdge.attributes = attributes;
        adjacencyList[to].append(reverseEdge);
    }
    
    // Remove a node and all its edges
    void removeNode(const QString& node) {
        if (!adjacencyList.contains(node)) {
            return;
        }
        
        // Remove all edges to this node
        for (auto it = adjacencyList.begin(); it != adjacencyList.end(); ++it) {
            if (it.key() == node) {
                continue;
            }
            
            QList<InternalEdge>& edges = it.value();
            for (int i = edges.size() - 1; i >= 0; --i) {
                if (edges[i].to == node) {
                    edges.removeAt(i);
                }
            }
        }
        
        // Remove the node itself
        adjacencyList.remove(node);
        nodeAttributes.remove(node);
    }
    
    // Clear the graph
    void clear() {
        adjacencyList.clear();
        nodeAttributes.clear();
    }
    
    // Get all nodes in the graph
    QStringList getNodes() const {
        return adjacencyList.keys();
    }
    
    // Set node attribute
    void setNodeAttribute(const QString& node, const QString& key, const QVariant& value) {
        if (!adjacencyList.contains(node)) {
            adjacencyList[node] = QList<InternalEdge>();
        }
        
        if (!nodeAttributes.contains(node)) {
            nodeAttributes[node] = QVariantMap();
        }
        
        nodeAttributes[node][key] = value;
    }
    
    // Get node attribute
    QVariant getNodeAttribute(const QString& node, const QString& key) const {
        if (!nodeAttributes.contains(node)) {
            return QVariant();
        }
        
        return nodeAttributes[node].value(key);
    }
};

TerminalGraph::TerminalGraph(const QString& pathToTerminalsDirectory)
    : QObject(nullptr), 
      m_graph(new GraphImpl()),
      m_pathToTerminalsDirectory(pathToTerminalsDirectory)
{
    // Initialize default cost function parameters
    m_costFunctionParametersWeights = {
        {"default", QVariantMap{
            {"cost", 1.0},
            {"travellTime", 1.0},
            {"distance", 1.0},
            {"carbonEmissions", 1.0},
            {"risk", 1.0},
            {"energyConsumption", 1.0},
            {"terminal_delay", 1.0},
            {"terminal_cost", 1.0}
        }},
        {QString::number(static_cast<int>(TransportationMode::Ship)), QVariantMap{
            {"cost", 1.0},
            {"travellTime", 1.0},
            {"distance", 1.0},
            {"carbonEmissions", 1.0},
            {"risk", 1.0},
            {"energyConsumption", 1.0},
            {"terminal_delay", 1.0},
            {"terminal_cost", 1.0}
        }},
        {QString::number(static_cast<int>(TransportationMode::Train)), QVariantMap{
            {"cost", 1.0},
            {"travellTime", 1.0},
            {"distance", 1.0},
            {"carbonEmissions", 1.0},
            {"risk", 1.0},
            {"energyConsumption", 1.0},
            {"terminal_delay", 1.0},
            {"terminal_cost", 1.0}
        }},
        {QString::number(static_cast<int>(TransportationMode::Truck)), QVariantMap{
            {"cost", 1.0},
            {"travellTime", 1.0},
            {"distance", 1.0},
            {"carbonEmissions", 1.0},
            {"risk", 1.0},
            {"energyConsumption", 1.0},
            {"terminal_delay", 1.0},
            {"terminal_cost", 1.0}
        }}
    };
    
    // Initialize default link attributes
    m_defaultLinkAttributes = {
        {"cost", 1.0},
        {"travellTime", 1.0},
        {"distance", 1.0},
        {"carbonEmissions", 1.0},
        {"risk", 1.0},
        {"energyConsumption", 1.0}
    };
    
    qInfo() << "Terminal graph initialized with terminal directory:" << (!pathToTerminalsDirectory.isEmpty() ? pathToTerminalsDirectory : "None");
}

TerminalGraph::~TerminalGraph()
{
    QMutexLocker locker(&m_mutex);
    
    // Clean up terminals
    for (auto it = m_terminals.begin(); it != m_terminals.end(); ++it) {
        delete it.value();
    }
    
    // Clean up graph implementation
    delete m_graph;
    
    qDebug() << "Terminal graph destroyed";
}

void TerminalGraph::setLinkDefaultAttributes(const QVariantMap& attributes)
{
    QMutexLocker locker(&m_mutex);
    m_defaultLinkAttributes = attributes;
}

void TerminalGraph::setCostFunctionParameters(const QVariantMap& parametersWeights)
{
    QMutexLocker locker(&m_mutex);
    m_costFunctionParametersWeights = parametersWeights;
}

void TerminalGraph::addTerminal(
    const QStringList& terminalNames,
    const QVariantMap& customConfig,
    const QMap<TerminalInterface, QSet<TransportationMode>>& terminalInterfaces,
    const QString& region)
{
    QMutexLocker locker(&m_mutex);
    
    if (terminalNames.isEmpty()) {
        throw std::invalid_argument("At least one terminal name must be provided");
    }
    
    // First name is the canonical name
    QString canonicalName = terminalNames.first();
    
    // Check if terminal already exists
    if (m_terminals.contains(canonicalName)) {
        throw std::invalid_argument(QString("Terminal '%1' already exists").arg(canonicalName).toStdString());
    }
    
    // Create terminal instance
    Terminal* terminal = new Terminal(
        canonicalName,
        terminalInterfaces,
        QMap<QPair<TransportationMode, QString>, QString>(),
        customConfig.value("capacity").toMap(),
        customConfig.value("dwell_time").toMap(),
        customConfig.value("customs").toMap(),
        customConfig.value("cost").toMap(),
        m_pathToTerminalsDirectory
    );
    
    // Add the terminal to the graph
    m_graph->adjacencyList[canonicalName] = QList<GraphImpl::InternalEdge>();
    if (!region.isEmpty()) {
        m_graph->setNodeAttribute(canonicalName, "region", region);
    }
    
    // Store the terminal instance
    m_terminals[canonicalName] = terminal;
    
    // Store aliases
    m_canonicalToAliases[canonicalName] = QSet<QString>(terminalNames.begin(), terminalNames.end());
    for (const QString& alias : terminalNames) {
        m_terminalAliases[alias] = canonicalName;
    }
    
    qDebug() << "Added terminal" << canonicalName << "with" << (terminalNames.size() - 1) 
             << "aliases and" << terminalInterfaces.size() << "interfaces";
}

void TerminalGraph::addAliasToTerminal(const QString& terminalName, const QString& alias)
{
    QMutexLocker locker(&m_mutex);
    
    QString canonicalName = getCanonicalName(terminalName);
    
    if (!m_terminals.contains(canonicalName)) {
        throw std::invalid_argument(QString("Terminal '%1' not found").arg(terminalName).toStdString());
    }
    
    // Add alias to mappings
    m_terminalAliases[alias] = canonicalName;
    m_canonicalToAliases[canonicalName].insert(alias);
    
    qDebug() << "Added alias" << alias << "to terminal" << canonicalName;
}

QStringList TerminalGraph::getAliasesOfTerminal(const QString& terminalName) const
{
    QMutexLocker locker(&m_mutex);
    
    QString canonicalName = getCanonicalName(terminalName);
    return m_canonicalToAliases.value(canonicalName).values();
}

void TerminalGraph::addRoute(
    const QString& routeId,
    const QString& startTerminal,
    const QString& endTerminal,
    TransportationMode mode,
    const QVariantMap& attributes)
{
    QMutexLocker locker(&m_mutex);
    
    QString startCanonical = getCanonicalName(startTerminal);
    QString endCanonical = getCanonicalName(endTerminal);
    
    if (!m_terminals.contains(startCanonical) || !m_terminals.contains(endCanonical)) {
        throw std::invalid_argument(
            QString("One or both terminals ('%1', '%2') not found").arg(startTerminal).arg(endTerminal).toStdString()
        );
    }
    
    // Create attribute map, using defaults for missing values
    QVariantMap routeAttributes = m_defaultLinkAttributes;
    
    // Override with provided attributes
    for (auto it = attributes.constBegin(); it != attributes.constEnd(); ++it) {
        routeAttributes[it.key()] = it.value();
    }
    
    // Add the route to the graph
    m_graph->addEdge(startCanonical, endCanonical, routeId, mode, routeAttributes);
    
    qDebug() << "Added route" << routeId << "from" << startCanonical << "to" << endCanonical
             << "with mode" << static_cast<int>(mode);
}

QVariantMap TerminalGraph::getEdgeByMode(const QString& startTerminal, const QString& endTerminal, TransportationMode mode) const
{
    QMutexLocker locker(&m_mutex);
    
    QString startCanonical = getCanonicalName(startTerminal);
    QString endCanonical = getCanonicalName(endTerminal);
    
    if (!m_terminals.contains(startCanonical) || !m_terminals.contains(endCanonical)) {
        return QVariantMap();
    }
    
    // Find the edge with the specified mode
    GraphImpl::InternalEdge* edge = m_graph->findEdge(startCanonical, endCanonical, mode);
    if (!edge) {
        return QVariantMap();
    }
    
    // Convert to QVariantMap
    QVariantMap result = edge->attributes;
    result["mode"] = static_cast<int>(edge->mode);
    result["route_id"] = edge->routeId;
    
    return result;
}

QStringList TerminalGraph::getTerminalsByRegion(const QString& region) const
{
    QMutexLocker locker(&m_mutex);
    
    QStringList result;
    
    for (const QString& nodeName : m_graph->getNodes()) {
        QVariant nodeRegion = m_graph->getNodeAttribute(nodeName, "region");
        if (nodeRegion.isValid() && nodeRegion.toString() == region) {
            result.append(nodeName);
        }
    }
    
    return result;
}

QList<QVariantMap> TerminalGraph::getRoutesBetweenRegions(const QString& regionA, const QString& regionB) const
{
    // Get all terminals in both regions
    QStringList terminalsInRegionA = getTerminalsByRegion(regionA);
    QStringList terminalsInRegionB = getTerminalsByRegion(regionB);


    QMutexLocker locker(&m_mutex);

    QList<QVariantMap> routes;

    // Find all routes between terminals in the two regions
    for (const QString& terminalA : terminalsInRegionA) {
        for (const QString& terminalB : terminalsInRegionB) {
            // Skip if same terminal
            if (terminalA == terminalB) {
                continue;
            }

            // Get all edges between the two terminals
            QList<GraphImpl::InternalEdge> edges = m_graph->getEdges(terminalA, terminalB);

            for (const GraphImpl::InternalEdge& edge : edges) {
                QVariantMap route;
                route["start"] = terminalA;
                route["end"] = terminalB;
                route["route_id"] = edge.routeId;
                route["mode"] = static_cast<int>(edge.mode);

                // Add all other attributes
                QVariantMap attributes = edge.attributes;
                route["attributes"] = attributes;

                routes.append(route);
            }
        }
    }

    return routes;
}

void TerminalGraph::connectTerminalsByInterfaceModes()
{
    // Collect all data we need while holding the mutex
    QList<QPair<QString, QString>> terminalsToConnect;
    QMap<QPair<QString, QString>, QList<QPair<TransportationMode, QString>>> routesToAdd;
    int routeCounter = 0;

    {
        QMutexLocker locker(&m_mutex);

        QStringList terminalList = m_terminals.keys();

        for (int i = 0; i < terminalList.size(); ++i) {
            const QString& terminalA = terminalList[i];
            Terminal* terminalAInstance = m_terminals[terminalA];
            QMap<TerminalInterface, QSet<TransportationMode>> terminalAInterfaces = terminalAInstance->getInterfaces();

            for (int j = i + 1; j < terminalList.size(); ++j) {
                const QString& terminalB = terminalList[j];
                Terminal* terminalBInstance = m_terminals[terminalB];
                QMap<TerminalInterface, QSet<TransportationMode>> terminalBInterfaces = terminalBInstance->getInterfaces();

                // Find common interfaces
                QSet<TerminalInterface> commonInterfaces;
                for (auto it = terminalAInterfaces.constBegin(); it != terminalAInterfaces.constEnd(); ++it) {
                    if (terminalBInterfaces.contains(it.key())) {
                        commonInterfaces.insert(it.key());
                    }
                }

                // For each common interface, collect routes for common modes
                for (TerminalInterface interface : commonInterfaces) {
                    QSet<TransportationMode> modesA = terminalAInterfaces[interface];
                    QSet<TransportationMode> modesB = terminalBInterfaces[interface];

                    // Find common transportation modes
                    QSet<TransportationMode> commonModes = modesA;
                    commonModes.intersect(modesB);

                    // Store the terminal pair and modes to connect
                    if (!commonModes.isEmpty()) {
                        terminalsToConnect.append(qMakePair(terminalA, terminalB));

                        QPair<QString, QString> terminalPair(terminalA, terminalB);

                        // For each common mode, prepare a route
                        for (TransportationMode mode : commonModes) {
                            QString routeId = QString("auto_route_%1").arg(++routeCounter);
                            routesToAdd[terminalPair].append(qMakePair(mode, routeId));
                        }
                    }
                }
            }
        }
    }

    // Now add the routes without holding the mutex
    for (auto it = routesToAdd.constBegin(); it != routesToAdd.constEnd(); ++it) {
        const QPair<QString, QString>& terminalPair = it.key();
        const QList<QPair<TransportationMode, QString>>& modeRoutes = it.value();

        for (const QPair<TransportationMode, QString>& modeRoute : modeRoutes) {
            TransportationMode mode = modeRoute.first;
            const QString& routeId = modeRoute.second;

            addRoute(routeId, terminalPair.first, terminalPair.second, mode, m_defaultLinkAttributes);
        }
    }

    qDebug() << "Connected terminals by interface modes. Created" << routeCounter << "routes";
}

void TerminalGraph::connectTerminalsInRegionByMode(const QString& region)
{
    // First, gather all the data we need while holding the lock
    QList<QPair<QString, QString>> terminalPairs;
    QMap<QString, QMap<TransportationMode, QString>> routesToAdd;

    QStringList terminalsInRegion = getTerminalsByRegion(region);
    if (terminalsInRegion.size() < 2) {
        throw std::invalid_argument(
            QString("Region '%1' must have at least two terminals "
                    "to create connections").arg(region).toStdString()
            );
    }

    {
        QMutexLocker locker(&m_mutex);

        int routeCounter = 0;

        // Iterate through pairs of terminals and collect the data needed
        for (int i = 0; i < terminalsInRegion.size(); ++i) {
            const QString& terminalA = terminalsInRegion[i];
            Terminal* terminalAInstance = m_terminals[terminalA];

            for (int j = i + 1; j < terminalsInRegion.size(); ++j) {
                const QString& terminalB = terminalsInRegion[j];
                Terminal* terminalBInstance = m_terminals[terminalB];

                // Store the terminal pair for later
                terminalPairs.append(qMakePair(terminalA, terminalB));

                // Get all modes supported by terminal A
                QSet<TransportationMode> modesA;
                for (auto it = terminalAInstance->getInterfaces().constBegin();
                     it != terminalAInstance->getInterfaces().constEnd(); ++it) {
                    modesA.unite(it.value());
                }

                // Get all modes supported by terminal B
                QSet<TransportationMode> modesB;
                for (auto it = terminalBInstance->getInterfaces().constBegin();
                     it != terminalBInstance->getInterfaces().constEnd(); ++it) {
                    modesB.unite(it.value());
                }

                // Find common transportation modes
                QSet<TransportationMode> sharedModes = modesA;
                sharedModes.intersect(modesB);

                // Store the routes that need to be added
                QMap<TransportationMode, QString> routes;
                for (TransportationMode mode : sharedModes) {
                    QString routeId =
                        QString("region_%1_route_%2").arg(region).arg(++routeCounter);
                    routes[mode] = routeId;
                }

                if (!routes.isEmpty()) {
                    routesToAdd[terminalA + "-" + terminalB] = routes;
                }
            }
        }
    } // mutex lock is released here

    // Now add all the routes without holding the lock
    int routeCounter = 0;
    for (const auto& pair : terminalPairs) {
        const QString& terminalA = pair.first;
        const QString& terminalB = pair.second;

        QString key = terminalA + "-" + terminalB;
        if (routesToAdd.contains(key)) {
            const QMap<TransportationMode, QString>& routes = routesToAdd[key];

            for (auto it = routes.constBegin(); it != routes.constEnd(); ++it) {
                TransportationMode mode = it.key();
                QString routeId = it.value();

                addRoute(routeId, terminalA, terminalB, mode, m_defaultLinkAttributes);
                routeCounter++;
            }
        }
    }

    qDebug() << "Connected terminals in region" << region << ". Created" << routeCounter << "routes";
}

void TerminalGraph::connectRegionsByMode(TransportationMode mode)
{
    // Create a list of routes to add while holding the lock
    struct RouteToAdd {
        QString routeId;
        QString terminalA;
        QString terminalB;
        TransportationMode mode;
    };
    QList<RouteToAdd> routesToAdd;

    // First phase: gather information under the lock
    {
        QMutexLocker locker(&m_mutex);

        // Get all terminals that support the given mode
        QStringList terminalsWithMode;

        for (auto it = m_terminals.constBegin(); it != m_terminals.constEnd(); ++it) {
            Terminal* terminal = it.value();
            bool supportsMode = false;

            for (auto interfaceIt = terminal->getInterfaces().constBegin();
                 interfaceIt != terminal->getInterfaces().constEnd(); ++interfaceIt) {
                if (interfaceIt.value().contains(mode)) {
                    supportsMode = true;
                    break;
                }
            }

            if (supportsMode) {
                terminalsWithMode.append(it.key());
            }
        }

        int routeCounter = 0;

        // Identify routes to create across regions
        for (int i = 0; i < terminalsWithMode.size(); ++i) {
            const QString& terminalA = terminalsWithMode[i];

            for (int j = i + 1; j < terminalsWithMode.size(); ++j) {
                const QString& terminalB = terminalsWithMode[j];

                // Check if in different regions
                QVariant regionA = m_graph->getNodeAttribute(terminalA, "region");
                QVariant regionB = m_graph->getNodeAttribute(terminalB, "region");

                // Skip if in same region or either doesn't have a region
                if (!regionA.isValid() || !regionB.isValid() || regionA.toString() == regionB.toString()) {
                    continue;
                }

                // Store route to add
                QString routeId = QString("inter_region_route_%1").arg(++routeCounter);
                routesToAdd.append({routeId, terminalA, terminalB, mode});
            }
        }

        qDebug() << "Identified" << routesToAdd.size() << "routes to create between regions for mode" << static_cast<int>(mode);
    }

    // Second phase: add the routes (this will acquire its own lock)
    for (const RouteToAdd& route : routesToAdd) {
        addRoute(route.routeId, route.terminalA, route.terminalB, route.mode, m_defaultLinkAttributes);
    }

    qDebug() << "Connected regions by mode" << static_cast<int>(mode) << ". Created" << routesToAdd.size() << "routes";
}

void TerminalGraph::changeRouteWeight(
    const QString& startTerminal,
    const QString& endTerminal,
    TransportationMode mode,
    const QVariantMap& newAttributes)
{
    QMutexLocker locker(&m_mutex);
    
    QString startCanonical = getCanonicalName(startTerminal);
    QString endCanonical = getCanonicalName(endTerminal);
    
    if (!m_terminals.contains(startCanonical) || !m_terminals.contains(endCanonical)) {
        throw std::invalid_argument(
            QString("One or both terminals ('%1', '%2') not found").arg(startTerminal).arg(endTerminal).toStdString()
        );
    }
    
    // Find the edge
    GraphImpl::InternalEdge* edge = m_graph->findEdge(startCanonical, endCanonical, mode);
    if (!edge) {
        throw std::invalid_argument(
            QString("No route with mode %1 exists between %2 and %3")
                .arg(static_cast<int>(mode)).arg(startTerminal).arg(endTerminal).toStdString()
        );
    }
    
    // Update attributes
    for (auto it = newAttributes.constBegin(); it != newAttributes.constEnd(); ++it) {
        edge->attributes[it.key()] = it.value();
    }
    
    // Update reverse edge (for undirected graph)
    GraphImpl::InternalEdge* reverseEdge = m_graph->findEdge(endCanonical, startCanonical, mode);
    if (reverseEdge) {
        for (auto it = newAttributes.constBegin(); it != newAttributes.constEnd(); ++it) {
            reverseEdge->attributes[it.key()] = it.value();
        }
    }
    
    qDebug() << "Updated route weights between" << startCanonical << "and" << endCanonical 
             << "for mode" << static_cast<int>(mode);
}

Terminal* TerminalGraph::getTerminal(const QString& terminalName) const
{
    QMutexLocker locker(&m_mutex);
    
    QString canonicalName = getCanonicalName(terminalName);
    if (!m_terminals.contains(canonicalName)) {
        throw std::invalid_argument(QString("Terminal '%1' not found").arg(terminalName).toStdString());
    }
    
    return m_terminals[canonicalName];
}

bool TerminalGraph::terminalExists(const QString& terminalName) const
{
    QMutexLocker locker(&m_mutex);
    
    QString canonicalName = getCanonicalName(terminalName);
    return m_terminals.contains(canonicalName);
}

bool TerminalGraph::removeTerminal(const QString& terminalName)
{
    QMutexLocker locker(&m_mutex);
    
    QString canonicalName = getCanonicalName(terminalName);
    if (!m_terminals.contains(canonicalName)) {
        return false;
    }
    
    // Get aliases to remove
    QSet<QString> aliases = m_canonicalToAliases.value(canonicalName);
    
    // Remove from alias mappings
    for (const QString& alias : aliases) {
        m_terminalAliases.remove(alias);
    }
    
    // Remove from canonical mappings
    m_canonicalToAliases.remove(canonicalName);
    
    // Delete terminal instance
    Terminal* terminal = m_terminals.take(canonicalName);
    delete terminal;
    
    // Remove from graph
    m_graph->removeNode(canonicalName);
    
    qDebug() << "Removed terminal" << canonicalName << "with" << aliases.size() << "aliases";
    
    return true;
}

int TerminalGraph::getTerminalCount() const
{
    QMutexLocker locker(&m_mutex);
    return m_terminals.size();
}

QMap<QString, QStringList> TerminalGraph::getAllTerminalNames(bool includeAliases) const
{
    QMutexLocker locker(&m_mutex);
    
    QMap<QString, QStringList> result;
    
    if (includeAliases) {
        // Include canonical names with their aliases
        for (auto it = m_canonicalToAliases.constBegin(); it != m_canonicalToAliases.constEnd(); ++it) {
            result[it.key()] = it.value().values();
        }
    } else {
        // Just include canonical names with empty lists
        for (auto it = m_terminals.constBegin(); it != m_terminals.constEnd(); ++it) {
            result[it.key()] = QStringList();
        }
    }
    
    return result;
}

void TerminalGraph::clear()
{
    QMutexLocker locker(&m_mutex);
    
    // Clean up terminal instances
    for (auto it = m_terminals.begin(); it != m_terminals.end(); ++it) {
        delete it.value();
    }
    
    // Clear all containers
    m_terminals.clear();
    m_terminalAliases.clear();
    m_canonicalToAliases.clear();
    m_graph->clear();
    
    qDebug() << "Terminal graph cleared";
}

QVariantMap TerminalGraph::getTerminalStatus(const QString& terminalName) const
{
    QMutexLocker locker(&m_mutex);

    if (!terminalName.isEmpty()) {
        // Get status for a specific terminal
        QString canonicalName = getCanonicalName(terminalName);
        if (!m_terminals.contains(canonicalName)) {
            throw std::invalid_argument(QString("Terminal '%1' not found").arg(terminalName).toStdString());
        }

        Terminal* terminal = m_terminals[canonicalName];
        QVariant region = m_graph->getNodeAttribute(canonicalName, "region");

        // Release mutex before calling Terminal methods to avoid deadlock
        locker.unlock();

        QVariantMap status;
        status["container_count"] = terminal->getContainerCount();
        status["available_capacity"] = terminal->getAvailableCapacity();
        status["max_capacity"] = terminal->getMaxCapacity();

        // Reacquire mutex for accessing graph data
        locker.relock();
        status["region"] = region;
        status["aliases"] = QVariant(m_canonicalToAliases[canonicalName].values());

        return status;
    } else {
        // Get status for all terminals
        // First collect all terminal pointers while holding the lock
        QHash<QString, Terminal*> terminalsCopy = m_terminals;
        QHash<QString, QSet<QString>> aliasesCopy = m_canonicalToAliases;

        // Store node attributes while holding the lock
        QMap<QString, QVariant> regionAttributes;
        for (auto it = terminalsCopy.constBegin(); it != terminalsCopy.constEnd(); ++it) {
            QString canonicalName = it.key();
            regionAttributes[canonicalName] = m_graph->getNodeAttribute(canonicalName, "region");
        }

        // Release the lock before calling terminal methods
        locker.unlock();

        // Now build the result without holding the lock
        QVariantMap result;
        for (auto it = terminalsCopy.constBegin(); it != terminalsCopy.constEnd(); ++it) {
            QString canonicalName = it.key();
            Terminal* terminal = it.value();

            QVariantMap status;
            status["container_count"] = terminal->getContainerCount();
            status["available_capacity"] = terminal->getAvailableCapacity();
            status["max_capacity"] = terminal->getMaxCapacity();
            status["region"] = regionAttributes[canonicalName];
            status["aliases"] = QVariant(aliasesCopy[canonicalName].values());

            result[canonicalName] = status;
        }

        return result;
    }
}

const QString &TerminalGraph::getPathToTerminalsDirectory() const
{
    return m_pathToTerminalsDirectory;
}

QList<TerminalGraph::PathSegment> TerminalGraph::findShortestPath(
    const QString& startTerminal,
    const QString& endTerminal,
    TransportationMode mode) const
{
    QMutexLocker locker(&m_mutex);

    QString startCanonical = getCanonicalName(startTerminal);
    QString endCanonical = getCanonicalName(endTerminal);

    if (!m_terminals.contains(startCanonical) || !m_terminals.contains(endCanonical)) {
        throw std::invalid_argument(
            QString("One or both terminals ('%1', '%2') not found").arg(startTerminal).arg(endTerminal).toStdString()
            );
    }

    // Implementation of Dijkstra's algorithm
    QHash<QString, double> distance;
    QHash<QString, QString> previous;
    QHash<QString, TransportationMode> edgeMode;
    QHash<QString, QVariantMap> edgeAttributes;

    QStringList nodes = m_graph->getNodes();
    for (const QString& node : nodes) {
        distance[node] = std::numeric_limits<double>::infinity();
        previous[node] = QString();
    }

    distance[startCanonical] = 0.0;

    // Priority queue for Dijkstra's algorithm
    QSet<QString> unvisited;
    for (const QString& node : nodes) {
        unvisited.insert(node);
    }

    while (!unvisited.isEmpty()) {
        // Find node with minimum distance
        QString current;
        double minDist = std::numeric_limits<double>::infinity();

        for (const QString& node : unvisited) {
            if (distance[node] < minDist) {
                minDist = distance[node];
                current = node;
            }
        }

        // If all remaining nodes are inaccessible, break
        if (current.isEmpty() || qFuzzyCompare(minDist, std::numeric_limits<double>::infinity())) {
            break;
        }

        // If we reached the destination, we're done
        if (current == endCanonical) {
            break;
        }

        // Remove current node from unvisited set
        unvisited.remove(current);

        // Process neighbors
        const QList<GraphImpl::InternalEdge>& edges = m_graph->adjacencyList[current];
        for (const GraphImpl::InternalEdge& edge : edges) {
            // Skip edges with wrong mode if specified
            if (mode != TransportationMode::Truck && edge.mode != mode) {
                continue;
            }

            QString neighbor = edge.to;

            // Skip if already visited
            if (!unvisited.contains(neighbor)) {
                continue;
            }

            // Add terminal handling costs
            Terminal* currentTerminal = m_terminals[current];
            Terminal* neighborTerminal = m_terminals[neighbor];

            double terminalDelay = currentTerminal->estimateContainerHandlingTime() +
                                   neighborTerminal->estimateContainerHandlingTime();
            double terminalCost = currentTerminal->estimateContainerCost() +
                                  neighborTerminal->estimateContainerCost();

            // Create parameters for cost computation including both edge and terminal costs
            QVariantMap costParams = edge.attributes;
            costParams["terminal_delay"] = terminalDelay;
            costParams["terminal_cost"] = terminalCost;

            // Compute combined cost
            double totalCost = computeCost(costParams, m_costFunctionParametersWeights, edge.mode);

            // Update distance if this path is shorter
            double alt = distance[current] + totalCost;
            if (alt < distance[neighbor]) {
                distance[neighbor] = alt;
                previous[neighbor] = current;
                edgeMode[neighbor] = edge.mode;
                edgeAttributes[neighbor] = edge.attributes;
            }
        }
    }

    // Reconstruct the path
    QList<TerminalGraph::PathSegment> path;

    // Check if a path was found
    if (previous[endCanonical].isEmpty() && startCanonical != endCanonical) {
        throw std::runtime_error(
            QString("No path found between '%1' and '%2'").arg(startTerminal).arg(endTerminal).toStdString()
            );
    }

    // Build the path in reverse
    QString current = endCanonical;
    while (!previous[current].isEmpty()) {
        QString from = previous[current];
        QString to = current;

        PathSegment segment;
        segment.from = from;
        segment.to = to;
        segment.mode = edgeMode[to];
        segment.weight = distance[to] - distance[from];
        segment.fromTerminalId = from;
        segment.toTerminalId = to;
        segment.attributes = edgeAttributes[to];

        path.prepend(segment);

        current = from;
    }

    return path;
}

QList<TerminalGraph::PathSegment> TerminalGraph::findShortestPathWithinRegions(
    const QString& startTerminal,
    const QString& endTerminal,
    const QStringList& allowedRegions,
    TransportationMode mode) const
{
    QMutexLocker locker(&m_mutex);

    QString startCanonical = getCanonicalName(startTerminal);
    QString endCanonical = getCanonicalName(endTerminal);

    if (!m_terminals.contains(startCanonical) || !m_terminals.contains(endCanonical)) {
        throw std::invalid_argument(
            QString("One or both terminals ('%1', '%2') not found").arg(startTerminal).arg(endTerminal).toStdString()
            );
    }

    // Check if start and end terminals are in allowed regions
    QVariant startRegion = m_graph->getNodeAttribute(startCanonical, "region");
    QVariant endRegion = m_graph->getNodeAttribute(endCanonical, "region");

    if (startRegion.isValid() && !allowedRegions.contains(startRegion.toString())) {
        throw std::invalid_argument(
            QString("Start terminal '%1' is not in allowed regions").arg(startTerminal).toStdString()
            );
    }

    if (endRegion.isValid() && !allowedRegions.contains(endRegion.toString())) {
        throw std::invalid_argument(
            QString("End terminal '%1' is not in allowed regions").arg(endTerminal).toStdString()
            );
    }

    // Create a subgraph containing only nodes in allowed regions
    GraphImpl subgraph;

    // Copy nodes in allowed regions
    for (const QString& node : m_graph->getNodes()) {
        QVariant region = m_graph->getNodeAttribute(node, "region");

        if (!region.isValid() || allowedRegions.contains(region.toString())) {
            subgraph.adjacencyList[node] = QList<GraphImpl::InternalEdge>();

            // Copy node attributes
            if (m_graph->nodeAttributes.contains(node)) {
                subgraph.nodeAttributes[node] = m_graph->nodeAttributes[node];
            }
        }
    }

    // Copy edges between nodes in the subgraph
    for (auto it = subgraph.adjacencyList.constBegin(); it != subgraph.adjacencyList.constEnd(); ++it) {
        const QString& node = it.key();

        if (m_graph->adjacencyList.contains(node)) {
            for (const GraphImpl::InternalEdge& edge : m_graph->adjacencyList[node]) {
                if (subgraph.adjacencyList.contains(edge.to)) {
                    subgraph.adjacencyList[node].append(edge);
                }
            }
        }
    }

    // Use Dijkstra's algorithm on the subgraph
    // Implementation of Dijkstra's algorithm
    QHash<QString, double> distance;
    QHash<QString, QString> previous;
    QHash<QString, TransportationMode> edgeMode;
    QHash<QString, QVariantMap> edgeAttributes;

    QStringList nodes = subgraph.getNodes();
    for (const QString& node : nodes) {
        distance[node] = std::numeric_limits<double>::infinity();
        previous[node] = QString();
    }

    distance[startCanonical] = 0.0;

    // Priority queue for Dijkstra's algorithm
    QSet<QString> unvisited;
    for (const QString& node : nodes) {
        unvisited.insert(node);
    }

    while (!unvisited.isEmpty()) {
        // Find node with minimum distance
        QString current;
        double minDist = std::numeric_limits<double>::infinity();

        for (const QString& node : unvisited) {
            if (distance[node] < minDist) {
                minDist = distance[node];
                current = node;
            }
        }

        // If all remaining nodes are inaccessible, break
        if (current.isEmpty() || qFuzzyCompare(minDist, std::numeric_limits<double>::infinity())) {
            break;
        }

        // If we reached the destination, we're done
        if (current == endCanonical) {
            break;
        }

        // Remove current node from unvisited set
        unvisited.remove(current);

        // Process neighbors
        const QList<GraphImpl::InternalEdge>& edges = subgraph.adjacencyList[current];
        for (const GraphImpl::InternalEdge& edge : edges) {
            // Skip edges with wrong mode if specified
            if (mode != TransportationMode::Truck && edge.mode != mode) {
                continue;
            }

            QString neighbor = edge.to;

            // Skip if already visited
            if (!unvisited.contains(neighbor)) {
                continue;
            }

            // Add terminal handling costs
            Terminal* currentTerminal = m_terminals[current];
            Terminal* neighborTerminal = m_terminals[neighbor];

            double terminalDelay = currentTerminal->estimateContainerHandlingTime() +
                                   neighborTerminal->estimateContainerHandlingTime();
            double terminalCost = currentTerminal->estimateContainerCost() +
                                  neighborTerminal->estimateContainerCost();

            // Create parameters for cost computation including both edge and terminal costs
            QVariantMap costParams = edge.attributes;
            costParams["terminal_delay"] = terminalDelay;
            costParams["terminal_cost"] = terminalCost;

            // Compute combined cost
            double totalCost = computeCost(costParams, m_costFunctionParametersWeights, edge.mode);

            // Update distance if this path is shorter
            double alt = distance[current] + totalCost;
            if (alt < distance[neighbor]) {
                distance[neighbor] = alt;
                previous[neighbor] = current;
                edgeMode[neighbor] = edge.mode;
                edgeAttributes[neighbor] = edge.attributes;
            }
        }
    }

    // Reconstruct the path
    QList<TerminalGraph::PathSegment> path;

    // Check if a path was found
    if (previous[endCanonical].isEmpty() && startCanonical != endCanonical) {
        throw std::runtime_error(
            QString("No path found between '%1' and '%2' within allowed regions")
                .arg(startTerminal).arg(endTerminal).toStdString()
            );
    }

    // Build the path in reverse
    QString current = endCanonical;
    while (!previous[current].isEmpty()) {
        QString from = previous[current];
        QString to = current;

        PathSegment segment;
        segment.from = from;
        segment.to = to;
        segment.mode = edgeMode[to];
        segment.weight = distance[to] - distance[from];
        segment.fromTerminalId = from;
        segment.toTerminalId = to;
        segment.attributes = edgeAttributes[to];

        path.prepend(segment);

        current = from;
    }

    return path;
}

QList<TerminalGraph::PathSegment> TerminalGraph::findShortestPathWithExclusions(
    const QString& startTerminal,
    const QString& endTerminal,
    TransportationMode mode,
    const QSet<QPair<QString, QString>>& edgesToExclude,
    const QSet<QString>& nodesToExclude) const
{

    QString startCanonical = getCanonicalName(startTerminal);
    QString endCanonical = getCanonicalName(endTerminal);

    if (!m_terminals.contains(startCanonical) || !m_terminals.contains(endCanonical)) {
        throw std::invalid_argument(
            QString("One or both terminals ('%1', '%2') not found").arg(startTerminal).arg(endTerminal).toStdString()
            );
    }

    // Implementation of Dijkstra's algorithm with exclusions
    QHash<QString, double> distance;
    QHash<QString, QString> previous;
    QHash<QString, TransportationMode> edgeMode;
    QHash<QString, QVariantMap> edgeAttributes;

    // Get all terminal names
    QStringList nodes = m_terminals.keys();

    for (const QString& node : nodes) {
        // Skip excluded nodes
        if (nodesToExclude.contains(node)) {
            continue;
        }

        distance[node] = std::numeric_limits<double>::infinity();
        previous[node] = QString();
    }

    distance[startCanonical] = 0.0;

    // Priority queue for Dijkstra's algorithm
    QSet<QString> unvisited;
    for (const QString& node : nodes) {
        if (!nodesToExclude.contains(node)) {
            unvisited.insert(node);
        }
    }

    while (!unvisited.isEmpty()) {
        // Find node with minimum distance
        QString current;
        double minDist = std::numeric_limits<double>::infinity();

        for (const QString& node : unvisited) {
            if (distance[node] < minDist) {
                minDist = distance[node];
                current = node;
            }
        }

        // If all remaining nodes are inaccessible, break
        if (current.isEmpty() || qFuzzyCompare(minDist, std::numeric_limits<double>::infinity())) {
            break;
        }

        // If we reached the destination, we're done
        if (current == endCanonical) {
            break;
        }

        // Remove current node from unvisited set
        unvisited.remove(current);

        // Process all terminals to find neighbors
        for (const QString& neighbor : nodes) {
            // Skip if already visited or excluded
            if (!unvisited.contains(neighbor) || nodesToExclude.contains(neighbor)) {
                continue;
            }

            // Skip if this edge is excluded
            QPair<QString, QString> edge = qMakePair(current, neighbor);
            if (edgesToExclude.contains(edge)) {
                continue;
            }

            // Get edge attributes if an edge exists
            QVariantMap edgeAttrs = getEdgeByMode(current, neighbor, mode);
            if (edgeAttrs.isEmpty()) {
                continue; // No edge exists
            }

            // Calculate cost of this edge
            Terminal* currentTerminal = m_terminals[current];
            Terminal* neighborTerminal = m_terminals[neighbor];

            double terminalDelay = currentTerminal->estimateContainerHandlingTime() +
                                   neighborTerminal->estimateContainerHandlingTime();
            double terminalCost = currentTerminal->estimateContainerCost() +
                                  neighborTerminal->estimateContainerCost();

            QVariantMap costParams = edgeAttrs;
            costParams["terminal_delay"] = terminalDelay;
            costParams["terminal_cost"] = terminalCost;

            double totalCost = computeCost(costParams, m_costFunctionParametersWeights, mode);

            // Update distance if this path is shorter
            double alt = distance[current] + totalCost;
            if (alt < distance[neighbor]) {
                distance[neighbor] = alt;
                previous[neighbor] = current;
                edgeMode[neighbor] = mode;
                edgeAttributes[neighbor] = edgeAttrs;
            }
        }
    }

    // Reconstruct the path
    QList<TerminalGraph::PathSegment> path;

    // Check if a path was found
    if (previous[endCanonical].isEmpty() && startCanonical != endCanonical) {
        throw std::runtime_error(
            QString("No path found between '%1' and '%2'").arg(startTerminal).arg(endTerminal).toStdString()
            );
    }

    // Build the path in reverse
    QString current = endCanonical;
    while (!previous[current].isEmpty()) {
        QString from = previous[current];
        QString to = current;

        PathSegment segment;
        segment.from = from;
        segment.to = to;
        segment.mode = edgeMode[to];
        segment.weight = distance[to] - distance[from];
        segment.fromTerminalId = from;
        segment.toTerminalId = to;
        segment.attributes = edgeAttributes[to];

        path.prepend(segment);

        current = from;
    }

    return path;
}

QList<TerminalGraph::Path> TerminalGraph::findTopNShortestPaths(
    const QString& startTerminal,
    const QString& endTerminal,
    int n,
    TransportationMode mode,
    bool skipSameModeTerminalDelaysAndCosts) const
{
    qDebug() << "Starting findTopNShortestPaths from" << startTerminal << "to" << endTerminal;

    // Pre-fetch terminal canonical names and check existence
    QString startCanonical;
    QString endCanonical;
    bool terminalsExist = true;

    {
        qDebug() << "Acquiring mutex to check terminal existence";
        QMutexLocker locker(&m_mutex);
        qDebug() << "Mutex acquired for terminal existence check";
        startCanonical = getCanonicalName(startTerminal);
        endCanonical = getCanonicalName(endTerminal);

        if (!m_terminals.contains(startCanonical) ||
            !m_terminals.contains(endCanonical)) {
            terminalsExist = false;
        }
        qDebug() << "Releasing mutex after terminal existence check";
    }

    if (!terminalsExist) {
        qDebug() << "One or both terminals not found:" << startTerminal << endTerminal;
        throw std::invalid_argument(
            QString("One or both terminals ('%1', '%2') not found")
                .arg(startTerminal).arg(endTerminal).toStdString()
            );
    }

    QList<TerminalGraph::Path> result;

    // Get the shortest path first (outside of any mutex lock)
    qDebug() << "Calling findShortestPath";
    QList<TerminalGraph::PathSegment> shortestPath;
    try {
        shortestPath = findShortestPath(startCanonical, endCanonical, mode);
        qDebug() << "findShortestPath returned successfully with" << shortestPath.size() << "segments";
    } catch (const std::exception& e) {
        qWarning() << "No path found between" << startCanonical << "and" << endCanonical << ":" << e.what();
        return result;
    }

    // If shortest path is empty, return empty result
    if (shortestPath.isEmpty()) {
        qDebug() << "Shortest path is empty, returning empty result";
        return result;
    }

    // Collect all terminal names needed for path calculations
    QSet<QString> terminalNamesNeeded;
    terminalNamesNeeded.insert(startCanonical);
    for (const PathSegment& segment : shortestPath) {
        terminalNamesNeeded.insert(segment.from);
        terminalNamesNeeded.insert(segment.to);
    }
    qDebug() << "Terminals needed for path:" << terminalNamesNeeded;

    // Collect terminal pointers first with a brief lock
    QHash<QString, Terminal*> terminalPointers;
    {
        qDebug() << "Acquiring mutex to get terminal pointers";
        QMutexLocker locker(&m_mutex);
        qDebug() << "Mutex acquired for getting terminal pointers";
        for (const QString& termName : terminalNamesNeeded) {
            if (m_terminals.contains(termName)) {
                terminalPointers[termName] = m_terminals[termName];
            }
        }
        qDebug() << "Releasing mutex after getting terminal pointers";
    }

    // Collect terminal handling times and costs outside of the main lock
    struct TerminalInfo {
        double handlingTime;
        double cost;
    };
    QMap<QString, TerminalInfo> terminalInfoMap;

    // For each terminal, get its info without holding the main mutex
    for (auto it = terminalPointers.constBegin(); it != terminalPointers.constEnd(); ++it) {
        QString termName = it.key();
        Terminal* terminal = it.value();

        qDebug() << "Getting handling time and cost for terminal" << termName;
        double handlingTime;
        double cost;

        try {
            handlingTime = terminal->estimateContainerHandlingTime();
            qDebug() << "Got handling time for" << termName << ":" << handlingTime;
        } catch (const std::exception& e) {
            qWarning() << "Exception getting handling time for" << termName << ":" << e.what();
            handlingTime = 0.0;
        }

        try {
            cost = terminal->estimateContainerCost();
            qDebug() << "Got cost for" << termName << ":" << cost;
        } catch (const std::exception& e) {
            qWarning() << "Exception getting cost for" << termName << ":" << e.what();
            cost = 0.0;
        }

        // Store the information for later use
        terminalInfoMap[termName] = {handlingTime, cost};
    }

    qDebug() << "Building first path";
    // Now we can build the first path
    double totalPathCost = 0.0;
    double totalEdgeCosts = 0.0;
    double totalTerminalCosts = 0.0;

    QList<QVariantMap> terminalsInPath;
    terminalsInPath.append(QVariantMap{
        {"terminal", startCanonical},
        {"handling_time", terminalInfoMap[startCanonical].handlingTime},
        {"cost", terminalInfoMap[startCanonical].cost},
        {"costs_skipped", false}
    });

    for (const PathSegment& segment : shortestPath) {
        totalEdgeCosts += segment.weight;

        // Calculate terminal costs
        QVariantMap termInfo;
        termInfo["terminal"] = segment.to;
        termInfo["handling_time"] = terminalInfoMap[segment.to].handlingTime;
        termInfo["cost"] = terminalInfoMap[segment.to].cost;

        // Check if costs should be skipped
        bool costsSkipped = false;
        if (skipSameModeTerminalDelaysAndCosts && terminalsInPath.size() > 1) {
            TransportationMode prevMode = shortestPath.at(terminalsInPath.size() - 2).mode;
            TransportationMode currMode = segment.mode;

            if (prevMode == currMode) {
                costsSkipped = true;
            }
        }

        termInfo["costs_skipped"] = costsSkipped;
        terminalsInPath.append(termInfo);
    }

    // Calculate total path cost
    totalPathCost = totalEdgeCosts;
    for (const QVariantMap& termInfo : terminalsInPath) {
        if (!termInfo["costs_skipped"].toBool()) {
            totalTerminalCosts += termInfo["cost"].toDouble();
        }
    }
    totalPathCost += totalTerminalCosts;

    TerminalGraph::Path firstPath;
    firstPath.pathId = 1;
    firstPath.totalPathCost = totalPathCost;
    firstPath.totalEdgeCosts = totalEdgeCosts;
    firstPath.totalTerminalCosts = totalTerminalCosts;
    firstPath.terminalsInPath = terminalsInPath;
    firstPath.segments = shortestPath;

    result.append(firstPath);

    // If n <= 1, we're done
    if (n <= 1) {
        qDebug() << "n <= 1, returning single path";
        return result;
    }

    qDebug() << "Starting Yen's algorithm for k-shortest paths";
    // A set to keep track of potential paths
    struct PotentialPath {
        double cost;
        QList<QString> nodes;
        QList<QPair<QString, QString>> edges;

        bool operator<(const PotentialPath& other) const {
            return cost < other.cost;
        }
    };

    QList<PotentialPath> potentialPaths;
    QSet<QStringList> pathsFound;

    // Add first path to paths found
    QStringList firstPathNodes;
    for (const PathSegment& segment : shortestPath) {
        if (firstPathNodes.isEmpty()) {
            firstPathNodes.append(segment.from);
        }
        firstPathNodes.append(segment.to);
    }
    pathsFound.insert(firstPathNodes);

    // Yen's algorithm main loop
    for (int k = 1; k < n; ++k) {
        qDebug() << "Yen's algorithm iteration" << k;
        const TerminalGraph::Path& prevPath = result.at(k - 1);
        const QList<TerminalGraph::PathSegment>& prevPathSegments = prevPath.segments;

        // For each node in the previous path
        for (int i = 0; i < prevPathSegments.size(); ++i) {
            // Get the spur node
            QString spurNode = prevPathSegments.at(i).from;
            qDebug() << "Processing spur node" << spurNode << "at index" << i;

            // Get the root path
            QList<TerminalGraph::PathSegment> rootPath;
            for (int j = 0; j < i; ++j) {
                rootPath.append(prevPathSegments.at(j));
            }

            // Build the root path nodes
            QStringList rootPathNodes;
            if (rootPath.isEmpty()) {
                rootPathNodes.append(spurNode);
            } else {
                rootPathNodes.append(rootPath.first().from);
                for (const PathSegment& segment : rootPath) {
                    rootPathNodes.append(segment.to);
                }
            }

            qDebug() << "Root path nodes:" << rootPathNodes;

            // Create exclusion sets
            QSet<QPair<QString, QString>> edgesToExclude;
            QSet<QString> nodesToExclude;

            // Exclude edges in the root path
            for (int j = 0; j < rootPathNodes.size() - 1; ++j) {
                QString node1 = rootPathNodes.at(j);
                QString node2 = rootPathNodes.at(j + 1);

                edgesToExclude.insert(qMakePair(node1, node2));
                edgesToExclude.insert(qMakePair(node2, node1)); // Exclude both directions
            }

            // Exclude nodes in the root path (except spur node)
            for (int j = 0; j < rootPathNodes.size() - 1; ++j) {
                QString node = rootPathNodes.at(j);
                if (node != spurNode) {
                    nodesToExclude.insert(node);
                }
            }

            // Also exclude edges from other paths that match our root path prefix
            for (const TerminalGraph::Path& otherPath : result) {
                const QList<TerminalGraph::PathSegment>& otherSegments = otherPath.segments;

                // Check if this path has a matching prefix with our root path
                bool prefixMatches = true;
                for (int j = 0; j < rootPath.size(); ++j) {
                    if (j >= otherSegments.size() ||
                        rootPath[j].from != otherSegments[j].from ||
                        rootPath[j].to != otherSegments[j].to) {
                        prefixMatches = false;
                        break;
                    }
                }

                // If the prefix matches, exclude the next edge
                if (prefixMatches && rootPath.size() < otherSegments.size()) {
                    QString node1 = otherSegments[rootPath.size()].from;
                    QString node2 = otherSegments[rootPath.size()].to;

                    edgesToExclude.insert(qMakePair(node1, node2));
                    edgesToExclude.insert(qMakePair(node2, node1)); // Exclude both directions
                }
            }

            qDebug() << "Edges to exclude:" << edgesToExclude.size();
            qDebug() << "Nodes to exclude:" << nodesToExclude.size();

            // Try to find a spur path using a modified Dijkstra algorithm
            // Call findShortestPathWithExclusions WITHOUT holding the mutex
            try {
                qDebug() << "Calling findShortestPathWithExclusions for spur path";
                QList<TerminalGraph::PathSegment> spurPath = findShortestPathWithExclusions(
                    spurNode, endCanonical, mode, edgesToExclude, nodesToExclude);
                qDebug() << "findShortestPathWithExclusions returned" << spurPath.size() << "segments";

                // Combine root path and spur path
                QList<TerminalGraph::PathSegment> totalPath = rootPath;
                totalPath.append(spurPath);

                // Convert to a list of nodes for comparison
                QStringList totalPathNodes;
                if (totalPath.isEmpty()) {
                    qDebug() << "Total path is empty, skipping";
                    continue;
                }

                totalPathNodes.append(totalPath.first().from);
                for (const PathSegment& segment : totalPath) {
                    totalPathNodes.append(segment.to);
                }

                qDebug() << "Total path nodes:" << totalPathNodes;

                // Collect terminal info for any new terminals in the path
                QSet<QString> newTerminals;
                for (const QString& nodeName : totalPathNodes) {
                    if (!terminalInfoMap.contains(nodeName)) {
                        newTerminals.insert(nodeName);
                    }
                }

                if (!newTerminals.isEmpty()) {
                    qDebug() << "Found new terminals in path:" << newTerminals;
                    QHash<QString, Terminal*> newTerminalPointers;

                    {
                        qDebug() << "Acquiring mutex to get new terminal pointers";
                        QMutexLocker locker(&m_mutex);
                        qDebug() << "Mutex acquired for new terminal pointers";
                        for (const QString& termName : newTerminals) {
                            if (m_terminals.contains(termName)) {
                                newTerminalPointers[termName] = m_terminals[termName];
                            }
                        }
                        qDebug() << "Releasing mutex after getting new terminal pointers";
                    }

                    // Get info for new terminals without holding the mutex
                    for (auto it = newTerminalPointers.constBegin(); it != newTerminalPointers.constEnd(); ++it) {
                        QString termName = it.key();
                        Terminal* terminal = it.value();

                        qDebug() << "Getting info for new terminal" << termName;
                        double handlingTime;
                        double cost;

                        try {
                            handlingTime = terminal->estimateContainerHandlingTime();
                            qDebug() << "Got handling time for new terminal" << termName << ":" << handlingTime;
                        } catch (const std::exception& e) {
                            qWarning() << "Exception getting handling time for new terminal" << termName << ":" << e.what();
                            handlingTime = 0.0;
                        }

                        try {
                            cost = terminal->estimateContainerCost();
                            qDebug() << "Got cost for new terminal" << termName << ":" << cost;
                        } catch (const std::exception& e) {
                            qWarning() << "Exception getting cost for new terminal" << termName << ":" << e.what();
                            cost = 0.0;
                        }

                        terminalInfoMap[termName] = {handlingTime, cost};
                    }
                }

                // Skip if we've already found this path
                if (pathsFound.contains(totalPathNodes)) {
                    qDebug() << "Path already found, skipping";
                    continue;
                }

                // Check for circulations (repeated nodes)
                QSet<QString> uniqueNodes;
                bool hasCirculation = false;
                for (const QString& node : totalPathNodes) {
                    if (uniqueNodes.contains(node)) {
                        hasCirculation = true;
                        break;
                    }
                    uniqueNodes.insert(node);
                }
                if (hasCirculation) {
                    qDebug() << "Path has circulation, skipping";
                    continue;
                }

                // Calculate path cost
                double pathCost = 0.0;
                for (const PathSegment& segment : totalPath) {
                    pathCost += segment.weight;
                }

                qDebug() << "Path cost:" << pathCost;

                // Add to potential paths
                PotentialPath potentialPath;
                potentialPath.cost = pathCost;
                potentialPath.nodes = totalPathNodes;

                for (int j = 0; j < totalPath.size(); ++j) {
                    potentialPath.edges.append(qMakePair(totalPath.at(j).from, totalPath.at(j).to));
                }

                potentialPaths.append(potentialPath);
                qDebug() << "Added potential path, now have" << potentialPaths.size() << "potential paths";

            } catch (const std::exception& e) {
                // No spur path found, continue to next node
                qDebug() << "Exception finding spur path:" << e.what() << ", continuing to next node";
                continue;
            }
        }

        // If there are no potential paths, we're done
        if (potentialPaths.isEmpty()) {
            qDebug() << "No more potential paths, breaking out of main loop";
            break;
        }

        // Sort potential paths by cost
        qDebug() << "Sorting" << potentialPaths.size() << "potential paths";
        std::sort(potentialPaths.begin(), potentialPaths.end());

        // Get the best potential path
        PotentialPath bestPath = potentialPaths.takeFirst();
        qDebug() << "Best path cost:" << bestPath.cost;

        // Convert back to segments - do this without the main lock where possible
        QList<TerminalGraph::PathSegment> bestPathSegments;

        for (int i = 0; i < bestPath.edges.size(); ++i) {
            QString from = bestPath.edges.at(i).first;
            QString to = bestPath.edges.at(i).second;

            qDebug() << "Processing edge" << from << "->" << to;

            // Get edge attributes - brief lock needed
            QVariantMap edgeAttrs = getEdgeByMode(from, to, mode);

            if (edgeAttrs.isEmpty()) {
                qDebug() << "Edge attributes empty, skipping";
                // Shouldn't happen, but just in case
                continue;
            }

            PathSegment segment;
            segment.from = from;
            segment.to = to;
            segment.mode = mode;

            // Use our pre-collected terminal info
            double fromHandlingTime = terminalInfoMap[from].handlingTime;
            double fromCost = terminalInfoMap[from].cost;
            double toHandlingTime = terminalInfoMap[to].handlingTime;
            double toCost = terminalInfoMap[to].cost;

            double terminalDelay = fromHandlingTime + toHandlingTime;
            double terminalCost = fromCost + toCost;

            QVariantMap costParams = edgeAttrs;
            costParams["terminal_delay"] = terminalDelay;
            costParams["terminal_cost"] = terminalCost;

            // Compute cost with a brief lock
            double totalCost;
            {
                qDebug() << "Acquiring mutex to compute cost";
                QMutexLocker locker(&m_mutex);
                qDebug() << "Mutex acquired for cost computation";
                totalCost = computeCost(costParams, m_costFunctionParametersWeights, mode);
                qDebug() << "Releasing mutex after cost computation";
            }

            segment.weight = totalCost;
            segment.fromTerminalId = from;
            segment.toTerminalId = to;
            segment.attributes = edgeAttrs;

            bestPathSegments.append(segment);
            qDebug() << "Added segment to best path, weight:" << totalCost;
        }

        // Calculate path cost details
        qDebug() << "Calculating final path cost details";
        double totalPathCost = 0.0;
        double totalEdgeCosts = 0.0;
        double totalTerminalCosts = 0.0;

        QList<QVariantMap> terminalsInPath;
        terminalsInPath.append(QVariantMap{
            {"terminal", startCanonical},
            {"handling_time", terminalInfoMap[startCanonical].handlingTime},
            {"cost", terminalInfoMap[startCanonical].cost},
            {"costs_skipped", false}
        });

        for (const PathSegment& segment : bestPathSegments) {
            totalEdgeCosts += segment.weight;

            // Calculate terminal costs
            QVariantMap termInfo;
            termInfo["terminal"] = segment.to;
            termInfo["handling_time"] = terminalInfoMap[segment.to].handlingTime;
            termInfo["cost"] = terminalInfoMap[segment.to].cost;

            // Check if costs should be skipped
            bool costsSkipped = false;
            if (skipSameModeTerminalDelaysAndCosts && terminalsInPath.size() > 1) {
                TransportationMode prevMode = bestPathSegments.at(terminalsInPath.size() - 2).mode;
                TransportationMode currMode = segment.mode;

                if (prevMode == currMode) {
                    costsSkipped = true;
                }
            }

            termInfo["costs_skipped"] = costsSkipped;
            terminalsInPath.append(termInfo);
        }

        // Calculate total path cost
        totalPathCost = totalEdgeCosts;
        for (const QVariantMap& termInfo : terminalsInPath) {
            if (!termInfo["costs_skipped"].toBool()) {
                totalTerminalCosts += termInfo["cost"].toDouble();
            }
        }
        totalPathCost += totalTerminalCosts;

        TerminalGraph::Path nextPath;
        nextPath.pathId = k + 1;
        nextPath.totalPathCost = totalPathCost;
        nextPath.totalEdgeCosts = totalEdgeCosts;
        nextPath.totalTerminalCosts = totalTerminalCosts;
        nextPath.terminalsInPath = terminalsInPath;
        nextPath.segments = bestPathSegments;

        result.append(nextPath);
        qDebug() << "Added path" << k+1 << "to results";

        // Add to paths found
        pathsFound.insert(bestPath.nodes);
    }

    qDebug() << "Returning" << result.size() << "paths";
    return result;
}

QJsonObject TerminalGraph::serializeGraph() const
{
    qDebug() << "Starting serializeGraph";

    // First collect all terminal JSON data without the main mutex locked
    QMap<QString, QJsonObject> terminalJsonMap;
    QHash<QString, Terminal*> terminalsCopy;

    {
        qDebug() << "Acquiring mutex to copy terminals";
        QMutexLocker locker(&m_mutex);
        qDebug() << "Mutex acquired for terminal copy";
        // Make a copy of the terminals map to work with outside the lock
        terminalsCopy = m_terminals;
        qDebug() << "Terminal copy completed with" << terminalsCopy.size() << "terminals";
        qDebug() << "Releasing mutex after terminal copy";
    }

    // Get terminal data without holding the main mutex
    qDebug() << "Starting to collect terminal JSON data outside mutex";
    for (auto it = terminalsCopy.constBegin(); it != terminalsCopy.constEnd(); ++it) {
        QString terminalName = it.key();
        Terminal* terminal = it.value();

        qDebug() << "Calling toJson() for terminal" << terminalName;
        // Call toJson() without the main mutex locked
        QJsonObject terminalConfig = terminal->toJson();
        qDebug() << "toJson() completed for terminal" << terminalName;

        // Store for later use
        QJsonObject terminalData;
        terminalData["terminal_config"] = terminalConfig;
        terminalJsonMap[terminalName] = terminalData;
    }
    qDebug() << "Completed collecting terminal JSON data for" << terminalJsonMap.size() << "terminals";

    // Now lock the mutex and complete the serialization
    qDebug() << "Acquiring mutex for main serialization";
    QMutexLocker locker(&m_mutex);
    qDebug() << "Mutex acquired for main serialization";

    QJsonObject graphData;

    // Serialize terminals
    qDebug() << "Serializing terminals";
    QJsonObject terminalsJson;
    for (auto it = m_terminals.constBegin(); it != m_terminals.constEnd(); ++it) {
        QString terminalName = it.key();

        // Get the pre-collected terminal data
        qDebug() << "Processing terminal" << terminalName;
        QJsonObject terminalData = terminalJsonMap[terminalName];

        // Node data
        QJsonObject nodeData;
        if (m_graph->nodeAttributes.contains(terminalName)) {
            qDebug() << "Collecting node attributes for" << terminalName;
            for (auto attrIt = m_graph->nodeAttributes[terminalName].constBegin();
                 attrIt != m_graph->nodeAttributes[terminalName].constEnd(); ++attrIt) {
                nodeData[attrIt.key()] = QJsonValue::fromVariant(attrIt.value());
            }
        }
        terminalData["node_data"] = nodeData;
        terminalsJson[terminalName] = terminalData;
    }
    graphData["terminals"] = terminalsJson;
    qDebug() << "Terminal serialization completed";

    // Serialize edges
    qDebug() << "Starting edge serialization";
    QJsonArray edgesJson;
    int edgeCount = 0;
    for (auto it = m_graph->adjacencyList.constBegin(); it != m_graph->adjacencyList.constEnd(); ++it) {
        QString from = it.key();
        for (const GraphImpl::InternalEdge& edge : it.value()) {
            // Only serialize one direction (avoid duplicates)
            if (from < edge.to) {
                edgeCount++;
                QJsonObject edgeData;
                edgeData["from"] = from;
                edgeData["to"] = edge.to;
                edgeData["route_id"] = edge.routeId;
                edgeData["mode"] = static_cast<int>(edge.mode);
                QJsonObject attributes;
                for (auto attrIt = edge.attributes.constBegin(); attrIt != edge.attributes.constEnd(); ++attrIt) {
                    attributes[attrIt.key()] = QJsonValue::fromVariant(attrIt.value());
                }
                edgeData["attributes"] = attributes;
                edgesJson.append(edgeData);
            }
        }
    }
    graphData["edges"] = edgesJson;
    qDebug() << "Edge serialization completed with" << edgeCount << "edges";

    // Serialize terminal aliases
    qDebug() << "Starting alias serialization";
    QJsonObject aliasesJson;
    for (auto it = m_terminalAliases.constBegin(); it != m_terminalAliases.constEnd(); ++it) {
        aliasesJson[it.key()] = it.value();
    }
    graphData["terminal_aliases"] = aliasesJson;
    qDebug() << "Alias serialization completed with" << m_terminalAliases.size() << "aliases";

    // Serialize canonical to aliases mapping
    qDebug() << "Starting canonical-to-aliases serialization";
    QJsonObject canonicalToAliasesJson;
    for (auto it = m_canonicalToAliases.constBegin(); it != m_canonicalToAliases.constEnd(); ++it) {
        QJsonArray aliasesArray;
        for (const QString& alias : it.value()) {
            aliasesArray.append(alias);
        }
        canonicalToAliasesJson[it.key()] = aliasesArray;
    }
    graphData["canonical_to_aliases"] = canonicalToAliasesJson;
    qDebug() << "Canonical-to-aliases serialization completed";

    // Serialize cost function parameters
    qDebug() << "Starting cost function serialization";
    QJsonObject costFunctionJson;
    for (auto it = m_costFunctionParametersWeights.constBegin(); it != m_costFunctionParametersWeights.constEnd(); ++it) {
        QJsonObject paramsJson;
        QVariantMap params = it.value().toMap();
        for (auto paramIt = params.constBegin(); paramIt != params.constEnd(); ++paramIt) {
            paramsJson[paramIt.key()] = QJsonValue::fromVariant(paramIt.value());
        }
        costFunctionJson[it.key()] = paramsJson;
    }
    graphData["cost_function_weights"] = costFunctionJson;
    qDebug() << "Cost function serialization completed";

    // Serialize default link attributes
    qDebug() << "Starting default link attributes serialization";
    QJsonObject defaultAttributesJson;
    for (auto it = m_defaultLinkAttributes.constBegin(); it != m_defaultLinkAttributes.constEnd(); ++it) {
        defaultAttributesJson[it.key()] = QJsonValue::fromVariant(it.value());
    }
    graphData["default_link_attributes"] = defaultAttributesJson;
    qDebug() << "Default link attributes serialization completed";

    qDebug() << "Serialization complete, returning data";
    return graphData;
}

TerminalGraph* TerminalGraph::deserializeGraph(const QJsonObject& graphData, const QString& pathToTerminalsDirectory)
{
    // Create new graph instance
    TerminalGraph* graph = new TerminalGraph(pathToTerminalsDirectory);
    
    try {
        // Restore cost function settings
        if (graphData.contains("cost_function_weights") && graphData["cost_function_weights"].isObject()) {
            QJsonObject costFunctionJson = graphData["cost_function_weights"].toObject();
            
            for (auto it = costFunctionJson.constBegin(); it != costFunctionJson.constEnd(); ++it) {
                if (it.value().isObject()) {
                    QVariantMap paramsMap;
                    QJsonObject paramsJson = it.value().toObject();
                    
                    for (auto paramIt = paramsJson.constBegin(); paramIt != paramsJson.constEnd(); ++paramIt) {
                        paramsMap[paramIt.key()] = paramIt.value().toVariant();
                    }
                    
                    graph->m_costFunctionParametersWeights[it.key()] = paramsMap;
                }
            }
        }
        
        // Restore default link attributes
        if (graphData.contains("default_link_attributes") && graphData["default_link_attributes"].isObject()) {
            QJsonObject defaultAttributesJson = graphData["default_link_attributes"].toObject();
            
            for (auto it = defaultAttributesJson.constBegin(); it != defaultAttributesJson.constEnd(); ++it) {
                graph->m_defaultLinkAttributes[it.key()] = it.value().toVariant();
            }
        }
        
        // Restore terminal aliases
        if (graphData.contains("terminal_aliases") && graphData["terminal_aliases"].isObject()) {
            QJsonObject aliasesJson = graphData["terminal_aliases"].toObject();
            
            for (auto it = aliasesJson.constBegin(); it != aliasesJson.constEnd(); ++it) {
                if (it.value().isString()) {
                    graph->m_terminalAliases[it.key()] = it.value().toString();
                }
            }
        }
        
        // Restore canonical to aliases mapping
        if (graphData.contains("canonical_to_aliases") && graphData["canonical_to_aliases"].isObject()) {
            QJsonObject canonicalToAliasesJson = graphData["canonical_to_aliases"].toObject();
            
            for (auto it = canonicalToAliasesJson.constBegin(); it != canonicalToAliasesJson.constEnd(); ++it) {
                if (it.value().isArray()) {
                    QSet<QString> aliases;
                    QJsonArray aliasesArray = it.value().toArray();
                    
                    for (const QJsonValue& value : aliasesArray) {
                        if (value.isString()) {
                            aliases.insert(value.toString());
                        }
                    }
                    
                    graph->m_canonicalToAliases[it.key()] = aliases;
                }
            }
        }
        
        // Restore terminals
        if (graphData.contains("terminals") && graphData["terminals"].isObject()) {
            QJsonObject terminalsJson = graphData["terminals"].toObject();
            
            for (auto it = terminalsJson.constBegin(); it != terminalsJson.constEnd(); ++it) {
                QString terminalName = it.key();
                
                if (!it.value().isObject()) {
                    continue;
                }
                
                QJsonObject terminalData = it.value().toObject();
                
                // Create terminal instance from config
                if (!terminalData.contains("terminal_config") || !terminalData["terminal_config"].isObject()) {
                    continue;
                }
                
                QJsonObject terminalConfig = terminalData["terminal_config"].toObject();
                
                // Make sure we have the same terminal name
                terminalConfig["terminal_name"] = terminalName;
                
                Terminal* terminal = Terminal::fromJson(terminalConfig, pathToTerminalsDirectory);
                if (!terminal) {
                    qWarning() << "Failed to create terminal from configuration:" << terminalName;
                    continue;
                }
                
                // Add terminal to graph
                graph->m_terminals[terminalName] = terminal;
                graph->m_graph->adjacencyList[terminalName] = QList<GraphImpl::InternalEdge>();
                
                // Add node attributes
                if (terminalData.contains("node_data") && terminalData["node_data"].isObject()) {
                    QJsonObject nodeData = terminalData["node_data"].toObject();
                    
                    for (auto nodeIt = nodeData.constBegin(); nodeIt != nodeData.constEnd(); ++nodeIt) {
                        graph->m_graph->setNodeAttribute(terminalName, nodeIt.key(), nodeIt.value().toVariant());
                    }
                }
            }
        }
        
        // Restore edges
        if (graphData.contains("edges") && graphData["edges"].isArray()) {
            QJsonArray edgesJson = graphData["edges"].toArray();
            
            for (const QJsonValue& value : edgesJson) {
                if (!value.isObject()) {
                    continue;
                }
                
                QJsonObject edgeData = value.toObject();
                
                if (!edgeData.contains("from") || !edgeData.contains("to") || 
                    !edgeData.contains("mode") || !edgeData.contains("attributes")) {
                    continue;
                }
                
                QString from = edgeData["from"].toString();
                QString to = edgeData["to"].toString();
                int modeInt = edgeData["mode"].toInt();
                QString routeId = edgeData.contains("route_id") ? edgeData["route_id"].toString() : QString();
                
                TransportationMode mode = static_cast<TransportationMode>(modeInt);
                
                // Extract attributes
                QVariantMap attributes;
                if (edgeData["attributes"].isObject()) {
                    QJsonObject attributesJson = edgeData["attributes"].toObject();
                    
                    for (auto attrIt = attributesJson.constBegin(); attrIt != attributesJson.constEnd(); ++attrIt) {
                        attributes[attrIt.key()] = attrIt.value().toVariant();
                    }
                }
                
                // Add edge to graph
                GraphImpl::InternalEdge edge;
                edge.to = to;
                edge.mode = mode;
                edge.routeId = routeId;
                edge.attributes = attributes;
                
                graph->m_graph->adjacencyList[from].append(edge);
                
                // Add reverse edge (for undirected graph)
                GraphImpl::InternalEdge reverseEdge;
                reverseEdge.to = from;
                reverseEdge.mode = mode;
                reverseEdge.routeId = routeId;
                reverseEdge.attributes = attributes;
                
                graph->m_graph->adjacencyList[to].append(reverseEdge);
            }
        }
        
        qDebug() << "Graph deserialized successfully with" << graph->m_terminals.size() << "terminals and"
                << graph->m_terminalAliases.size() << "aliases";
        
        return graph;
        
    } catch (const std::exception& e) {
        qWarning() << "Failed to deserialize graph:" << e.what();
        delete graph;
        throw;
    }
}

void TerminalGraph::saveToFile(const QString& filepath) const
{
    QJsonObject graphData = serializeGraph();
    
    QFile file(filepath);
    if (!file.open(QIODevice::WriteOnly)) {
        throw std::runtime_error(
            QString("Failed to open file for writing: %1").arg(filepath).toStdString()
        );
    }
    
    QJsonDocument doc(graphData);
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
    
    qInfo() << "Graph saved to file:" << filepath;
}

TerminalGraph* TerminalGraph::loadFromFile(const QString& filepath, const QString& pathToTerminalsDirectory)
{
    QFile file(filepath);
    if (!file.open(QIODevice::ReadOnly)) {
        throw std::runtime_error(
            QString("Failed to open file for reading: %1").arg(filepath).toStdString()
        );
    }
    
    QByteArray data = file.readAll();
    file.close();
    
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(data, &error);
    
    if (error.error != QJsonParseError::NoError) {
        throw std::runtime_error(
            QString("Failed to parse JSON: %1").arg(error.errorString()).toStdString()
        );
    }
    
    if (!doc.isObject()) {
        throw std::runtime_error("Invalid JSON format: root element is not an object");
    }
    
    TerminalGraph* graph = deserializeGraph(doc.object(), pathToTerminalsDirectory);
    
    qDebug() << "Graph loaded from file:" << filepath;
    
    return graph;
}

QString TerminalGraph::getCanonicalName(const QString& terminalName) const
{
    return m_terminalAliases.value(terminalName, terminalName);
}

double TerminalGraph::computeCost(const QVariantMap& parameters, const QVariantMap& weights, TransportationMode mode) const
{
    double cost = 0.0;
    
    // Get mode-specific weights if a mode is provided
    QVariantMap modeWeights;
    QString modeStr = QString::number(static_cast<int>(mode));
    
    if (weights.contains(modeStr)) {
        modeWeights = weights.value(modeStr).toMap();
    } else if (weights.contains("default")) {
        modeWeights = weights.value("default").toMap();
    }
    
    if (modeWeights.isEmpty()) {
        qWarning() << "No weights found for mode" << static_cast<int>(mode);
        return 0.0;
    }
    
    // Apply weights to parameters
    for (auto it = parameters.constBegin(); it != parameters.constEnd(); ++it) {
        QString key = it.key();
        QVariant value = it.value();
        
        // Skip non-numeric values
        bool ok;
        double numericValue = value.toDouble(&ok);
        if (!ok) {
            continue;
        }
        
        // Get weight for parameter
        double weight = 1.0;
        if (modeWeights.contains(key)) {
            weight = modeWeights.value(key).toDouble();
        } else {
            qDebug() << "Warning: Weight for key" << key << "not found, using default 1.0";
        }
        
        // Apply weight
        cost += weight * numericValue;
    }
    
    return cost;
}

} // namespace TerminalSim
