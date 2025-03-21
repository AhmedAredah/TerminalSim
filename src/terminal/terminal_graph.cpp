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
#include <limits>
#include <algorithm>
#include <stdexcept>


namespace TerminalSim {

/**
 * @brief Constructs a TerminalGraph instance
 * @param dir Directory path for terminal storage
 *
 * Initializes graph with default cost weights and
 * link attributes. Logs initialization details.
 */
TerminalGraph::TerminalGraph(const QString& dir)
    : QObject(nullptr),
    m_graph(new GraphImpl()),
    m_pathToTerminalsDirectory(dir)
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
        {QString::number(static_cast<int>(TransportationMode::Ship)),
         QVariantMap{
             {"cost", 1.0},
             {"travellTime", 1.0},
             {"distance", 1.0},
             {"carbonEmissions", 1.0},
             {"risk", 1.0},
             {"energyConsumption", 1.0},
             {"terminal_delay", 1.0},
             {"terminal_cost", 1.0}
         }},
        {QString::number(static_cast<int>(TransportationMode::Train)),
         QVariantMap{
             {"cost", 1.0},
             {"travellTime", 1.0},
             {"distance", 1.0},
             {"carbonEmissions", 1.0},
             {"risk", 1.0},
             {"energyConsumption", 1.0},
             {"terminal_delay", 1.0},
             {"terminal_cost", 1.0}
         }},
        {QString::number(static_cast<int>(TransportationMode::Truck)),
         QVariantMap{
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

    // Set default link attributes
    m_defaultLinkAttributes = {
        {"cost", 1.0},
        {"travellTime", 1.0},
        {"distance", 1.0},
        {"carbonEmissions", 1.0},
        {"risk", 1.0},
        {"energyConsumption", 1.0}
    };

    // Log initialization
    qInfo() << "Graph initialized with dir:"
            << (dir.isEmpty() ? "None" : dir);
}

/**
 * @brief Destructor, cleans up resources
 *
 * Deletes all terminal instances and the graph impl.
 * Ensures thread-safe cleanup.
 */
TerminalGraph::~TerminalGraph()
{
    QMutexLocker locker(&m_mutex); // Lock for thread safety
    for (auto it = m_terminals.begin(); it != m_terminals.end(); ++it) {
        delete it.value(); // Free each terminal
    }
    delete m_graph; // Free graph implementation
    qDebug() << "Graph destroyed"; // Log destruction
}

/**
 * @brief Sets default attributes for links
 * @param attrs Attributes to set as default
 *
 * Updates default link attributes thread-safely.
 */
void TerminalGraph::setLinkDefaultAttributes(const QVariantMap& attrs)
{
    QMutexLocker locker(&m_mutex); // Ensure thread safety
    m_defaultLinkAttributes = attrs; // Update attributes
}

/**
 * @brief Sets cost function parameters
 * @param params Weights for cost computation
 *
 * Updates cost weights thread-safely.
 */
void TerminalGraph::setCostFunctionParameters(const QVariantMap& params)
{
    QMutexLocker locker(&m_mutex); // Ensure thread safety
    m_costFunctionParametersWeights = params; // Update weights
}

/**
 * @brief Adds a terminal to the graph
 * @param names List of names (first is canonical)
 * @param config Custom config for terminal
 * @param interfaces Interfaces and modes
 * @param region Region name (optional)
 *
 * Creates and adds a terminal with aliases.
 */
void TerminalGraph::addTerminal(const QStringList& names,
                                const QVariantMap& config,
                                const QMap<TerminalInterface,
                                           QSet<TransportationMode>>& interfaces,
                                const QString& region)
{
    QMutexLocker locker(&m_mutex); // Lock for thread safety
    if (names.isEmpty()) {
        throw std::invalid_argument("No terminal names provided");
    }

    QString canonical = names.first(); // First name is canonical
    if (m_terminals.contains(canonical)) {
        throw std::invalid_argument("Terminal exists: " + canonical.toStdString());
    }

    // Create new terminal instance
    Terminal* term = new Terminal(
        canonical,
        interfaces,
        QMap<QPair<TransportationMode, QString>, QString>(),
        config.value("capacity").toMap(),
        config.value("dwell_time").toMap(),
        config.value("customs").toMap(),
        config.value("cost").toMap(),
        m_pathToTerminalsDirectory
        );

    // Add to graph structure
    m_graph->adjacencyList[canonical] = QList<GraphImpl::InternalEdge>();
    if (!region.isEmpty()) {
        m_graph->setNodeAttribute(canonical, "region", region);
    }

    // Store terminal and aliases
    m_terminals[canonical] = term;
    m_canonicalToAliases[canonical] = QSet<QString>(names.begin(), names.end());
    for (const QString& alias : names) {
        m_terminalAliases[alias] = canonical;
    }

    qDebug() << "Added terminal" << canonical
             << "with" << (names.size() - 1) << "aliases";
}

/**
 * @brief Adds an alias to a terminal
 * @param name Terminal name to alias
 * @param alias New alias to add
 *
 * Associates an alias with a canonical name.
 */
void TerminalGraph::addAliasToTerminal(const QString& name,
                                       const QString& alias)
{
    QMutexLocker locker(&m_mutex); // Lock for thread safety
    QString canonical = getCanonicalName(name); // Resolve name
    if (!m_terminals.contains(canonical)) {
        throw std::invalid_argument("Terminal not found: " + name.toStdString());
    }

    // Update alias mappings
    m_terminalAliases[alias] = canonical;
    m_canonicalToAliases[canonical].insert(alias);
    qDebug() << "Added alias" << alias << "to" << canonical;
}

/**
 * @brief Gets aliases for a terminal
 * @param name Terminal name to query
 * @return List of aliases
 *
 * Returns all aliases for a given terminal.
 */
QStringList TerminalGraph::getAliasesOfTerminal(const QString& name) const
{
    QMutexLocker locker(&m_mutex); // Lock for thread safety
    QString canonical = getCanonicalName(name); // Resolve name
    return m_canonicalToAliases.value(canonical).values();
}

/**
 * @brief Adds a route between terminals
 * @param id Unique route identifier
 * @param start Starting terminal name
 * @param end Ending terminal name
 * @param mode Transportation mode
 * @param attrs Route attributes
 *
 * Adds a route with attributes to the graph.
 */
void TerminalGraph::addRoute(const QString& id,
                             const QString& start,
                             const QString& end,
                             TransportationMode mode,
                             const QVariantMap& attrs)
{
    QMutexLocker locker(&m_mutex); // Lock for thread safety
    QString startCanonical = getCanonicalName(start);
    QString endCanonical = getCanonicalName(end);

    // Validate terminals exist
    if (!m_terminals.contains(startCanonical) ||
        !m_terminals.contains(endCanonical)) {
        throw std::invalid_argument("Terminal not found");
    }

    // Merge default and provided attributes
    QVariantMap routeAttrs = m_defaultLinkAttributes;
    for (auto it = attrs.begin(); it != attrs.end(); ++it) {
        routeAttrs[it.key()] = it.value();
    }

    // Add edge to graph
    m_graph->addEdge(startCanonical, endCanonical, id, mode, routeAttrs);
    qDebug() << "Added route" << id << "from" << startCanonical
             << "to" << endCanonical;
}

/**
 * @brief Gets edge attributes by mode
 * @param start Starting terminal name
 * @param end Ending terminal name
 * @param mode Transportation mode
 * @return Edge attributes, or empty if not found
 *
 * Retrieves edge details for a specific mode.
 */
QVariantMap TerminalGraph::getEdgeByMode(const QString& start,
                                         const QString& end,
                                         TransportationMode mode) const
{
    QMutexLocker locker(&m_mutex); // Lock for thread safety
    QString startCanonical = getCanonicalName(start);
    QString endCanonical = getCanonicalName(end);

    // Check terminal existence
    if (!m_terminals.contains(startCanonical) ||
        !m_terminals.contains(endCanonical)) {
        return QVariantMap(); // Return empty if not found
    }

    // Find edge by mode
    GraphImpl::InternalEdge* edge = m_graph->findEdge(startCanonical,
                                                      endCanonical, mode);
    if (!edge) {
        return QVariantMap(); // No edge found
    }

    // Construct result map
    QVariantMap result = edge->attributes;
    result["mode"] = static_cast<int>(edge->mode);
    result["route_id"] = edge->routeId;
    return result;
}

/**
 * @brief Gets terminals in a region
 * @param region Region name
 * @return List of terminal names
 *
 * Returns all terminals in the specified region.
 */
QStringList TerminalGraph::getTerminalsByRegion(const QString& region) const
{
    QMutexLocker locker(&m_mutex); // Lock for thread safety
    QStringList result;
    for (const QString& node : m_graph->getNodes()) {
        QVariant nodeRegion = m_graph->getNodeAttribute(node, "region");
        if (nodeRegion.isValid() && nodeRegion.toString() == region) {
            result.append(node);
        }
    }
    return result;
}

/**
 * @brief Gets routes between two regions
 * @param regionA First region name
 * @param regionB Second region name
 * @return List of route details
 *
 * Finds all routes connecting two regions.
 * Avoids deadlock by inlining terminal retrieval.
 */
QList<QVariantMap>
TerminalGraph::getRoutesBetweenRegions(const QString& regionA,
                                       const QString& regionB) const
{
    QMutexLocker locker(&m_mutex); // Lock for thread safety

    // Get terminals for region A
    QStringList regionATerminals;
    for (const QString& node : m_graph->getNodes()) {
        QVariant nodeRegion = m_graph->getNodeAttribute(node, "region");
        if (nodeRegion.isValid() && nodeRegion.toString() == regionA) {
            regionATerminals.append(node);
        }
    }

    // Get terminals for region B
    QStringList regionBTerminals;
    for (const QString& node : m_graph->getNodes()) {
        QVariant nodeRegion = m_graph->getNodeAttribute(node, "region");
        if (nodeRegion.isValid() && nodeRegion.toString() == regionB) {
            regionBTerminals.append(node);
        }
    }

    // Collect routes between regions
    QList<QVariantMap> routes;
    for (const QString& termA : regionATerminals) {
        for (const QString& termB : regionBTerminals) {
            if (termA == termB) {
                continue; // Skip self-loops
            }

            // Get edges between terminals
            QList<GraphImpl::InternalEdge> edges =
                m_graph->getEdges(termA, termB);
            for (const GraphImpl::InternalEdge& edge : edges) {
                QVariantMap route;
                route["start"] = termA;
                route["end"] = termB;
                route["route_id"] = edge.routeId;
                route["mode"] = static_cast<int>(edge.mode);
                route["attributes"] = edge.attributes;
                routes.append(route);
            }
        }
    }

    // Log for debugging
    qDebug() << "Found" << routes.size()
             << "routes between" << regionA << "and" << regionB;
    return routes;
}

/**
 * @brief Connects terminals by interface modes
 *
 * Creates bidirectional routes for common modes.
 */
void TerminalGraph::connectTerminalsByInterfaceModes()
{
    QMutexLocker locker(&m_mutex); // Lock for thread safety
    QStringList terminals = m_terminals.keys();
    int routeCounter = 0;

    // Map to store routes for both directions
    QMap<QPair<QString, QString>,
         QList<QPair<TransportationMode, QString>>> routes;

    // Iterate all terminal pairs
    for (int i = 0; i < terminals.size(); ++i) {
        const QString& termA = terminals[i];
        Terminal* termAInstance = m_terminals[termA];
        auto interfacesA = termAInstance->getInterfaces();

        for (int j = i + 1; j < terminals.size(); ++j) {
            const QString& termB = terminals[j];
            Terminal* termBInstance = m_terminals[termB];
            auto interfacesB = termBInstance->getInterfaces();

            // Find common interfaces
            QSet<TerminalInterface> commonInterfaces;
            for (auto it = interfacesA.begin();
                 it != interfacesA.end(); ++it) {
                if (interfacesB.contains(it.key())) {
                    commonInterfaces.insert(it.key());
                }
            }

            // Collect common modes
            for (TerminalInterface intf : commonInterfaces) {
                QSet<TransportationMode> modesA = interfacesA[intf];
                QSet<TransportationMode> modesB = interfacesB[intf];
                QSet<TransportationMode> commonModes = modesA & modesB;

                if (!commonModes.isEmpty()) {
                    QPair<QString, QString> pairAB(termA, termB);
                    QPair<QString, QString> pairBA(termB, termA);

                    // Add routes in both directions
                    for (TransportationMode mode : commonModes) {
                        QString routeId = QString("auto_%1").arg(++routeCounter);
                        routes[pairAB].append({mode, routeId});

                        // Reverse direction
                        routeId = QString("auto_%1").arg(++routeCounter);
                        routes[pairBA].append({mode, routeId});
                    }
                }
            }
        }
    }

    // Add routes outside lock to avoid deadlock
    locker.unlock();
    for (auto it = routes.begin(); it != routes.end(); ++it) {
        const QPair<QString, QString>& pair = it.key();
        for (const auto& modeRoute : it.value()) {
            addRoute(modeRoute.second, pair.first, pair.second,
                     modeRoute.first, m_defaultLinkAttributes);
        }
    }
    qDebug() << "Connected terminals, added" << routeCounter << "routes";
}

/**
 * @brief Connects terminals in a region by mode
 * @param region Region name to connect within
 *
 * Adds bidirectional routes for shared modes.
 * Ensures no deadlock by avoiding nested locks.
 */
void TerminalGraph::connectTerminalsInRegionByMode(const QString& region)
{
    QMutexLocker locker(&m_mutex); // Lock for thread safety

    // Get terminals in region
    QStringList terminals;
    for (const QString& node : m_graph->getNodes()) {
        QVariant nodeRegion = m_graph->getNodeAttribute(node, "region");
        if (nodeRegion.isValid() && nodeRegion.toString() == region) {
            terminals.append(node);
        }
    }

    // Validate terminal count
    if (terminals.size() < 2) {
        throw std::invalid_argument("Too few terminals in region");
    }

    int routeCounter = 0;
    QMap<QString, QMap<TransportationMode, QString>> routesToAdd;

    // Collect routes in both directions
    for (int i = 0; i < terminals.size(); ++i) {
        const QString& termA = terminals[i];
        Terminal* termAInstance = m_terminals[termA];

        for (int j = i + 1; j < terminals.size(); ++j) {
            const QString& termB = terminals[j];
            Terminal* termBInstance = m_terminals[termB];

            // Gather modes for terminal A
            QSet<TransportationMode> modesA;
            for (auto it = termAInstance->getInterfaces().begin();
                 it != termAInstance->getInterfaces().end(); ++it) {
                modesA.unite(it.value());
            }

            // Gather modes for terminal B
            QSet<TransportationMode> modesB;
            for (auto it = termBInstance->getInterfaces().begin();
                 it != termBInstance->getInterfaces().end(); ++it) {
                modesB.unite(it.value());
            }

            // Find shared modes
            QSet<TransportationMode> sharedModes = modesA & modesB;

            // Store routes for A to B
            QMap<TransportationMode, QString> routesAB;
            for (TransportationMode mode : sharedModes) {
                QString routeId = QString("region_%1_%2")
                .arg(region).arg(++routeCounter);
                routesAB[mode] = routeId;
            }
            if (!routesAB.isEmpty()) {
                routesToAdd[termA + "-" + termB] = routesAB;
            }

            // Store routes for B to A
            QMap<TransportationMode, QString> routesBA;
            for (TransportationMode mode : sharedModes) {
                QString routeId = QString("region_%1_%2")
                .arg(region).arg(++routeCounter);
                routesBA[mode] = routeId;
            }
            if (!routesBA.isEmpty()) {
                routesToAdd[termB + "-" + termA] = routesBA;
            }
        }
    }

    // Unlock before adding routes
    locker.unlock();

    // Add routes outside lock
    for (int i = 0; i < terminals.size(); ++i) {
        for (int j = 0; j < terminals.size(); ++j) {
            if (i == j) continue; // Skip self-loops

            const QString& termA = terminals[i];
            const QString& termB = terminals[j];
            QString key = termA + "-" + termB;

            if (routesToAdd.contains(key)) {
                for (auto it = routesToAdd[key].begin();
                     it != routesToAdd[key].end(); ++it) {
                    addRoute(it.value(), termA, termB,
                             it.key(), m_defaultLinkAttributes);
                    qDebug() << "Added route" << it.value()
                             << "from" << termA << "to" << termB;
                }
            }
        }
    }

    qDebug() << "Connected region" << region
             << "with" << routeCounter << "routes";
}

/**
 * @brief Connects regions by a specific mode
 * @param mode Transportation mode
 *
 * Links terminals across regions bidirectionally.
 */
void TerminalGraph::connectRegionsByMode(TransportationMode mode)
{
    // Struct for routes to add
    struct RouteToAdd {
        QString routeId;
        QString terminalA;
        QString terminalB;
        TransportationMode mode;
    };
    QList<RouteToAdd> routesToAdd;

    // First phase: gather routes under lock
    {
        QMutexLocker locker(&m_mutex); // Lock for thread safety

        // Get terminals supporting the mode
        QStringList terminalsWithMode;
        for (auto it = m_terminals.constBegin();
             it != m_terminals.constEnd(); ++it) {
            Terminal* terminal = it.value();
            bool supportsMode = false;

            for (auto intfIt = terminal->getInterfaces().constBegin();
                 intfIt != terminal->getInterfaces().constEnd();
                 ++intfIt) {
                if (intfIt.value().contains(mode)) {
                    supportsMode = true;
                    break;
                }
            }

            if (supportsMode) {
                terminalsWithMode.append(it.key());
            }
        }

        int routeCounter = 0;

        // Identify cross-region routes in both directions
        for (int i = 0; i < terminalsWithMode.size(); ++i) {
            const QString& terminalA = terminalsWithMode[i];
            QVariant regionA =
                m_graph->getNodeAttribute(terminalA, "region");

            for (int j = 0; j < terminalsWithMode.size(); ++j) {
                if (i == j) continue; // Skip self-loops

                const QString& terminalB = terminalsWithMode[j];
                QVariant regionB =
                    m_graph->getNodeAttribute(terminalB, "region");

                // Skip if same region or invalid
                if (!regionA.isValid() || !regionB.isValid() ||
                    regionA.toString() == regionB.toString()) {
                    continue;
                }

                // Add route from A to B
                QString routeId = QString("inter_region_route_%1")
                                      .arg(++routeCounter);
                routesToAdd.append({routeId, terminalA, terminalB, mode});
            }
        }

        qDebug() << "Identified" << routesToAdd.size()
                 << "routes to create between regions for mode"
                 << static_cast<int>(mode);
    }

    // Second phase: add routes outside lock
    for (const RouteToAdd& route : routesToAdd) {
        addRoute(route.routeId, route.terminalA, route.terminalB,
                 route.mode, m_defaultLinkAttributes);
        qDebug() << "Added route" << route.routeId
                 << "from" << route.terminalA << "to" << route.terminalB;
    }

    qDebug() << "Connected regions by mode"
             << static_cast<int>(mode)
             << ". Created" << routesToAdd.size() << "routes";
}

/**
 * @brief Changes weight of a route
 * @param start Starting terminal name
 * @param end Ending terminal name
 * @param mode Transportation mode
 * @param attrs New attributes to apply
 *
 * Updates route attributes thread-safely.
 */
void TerminalGraph::changeRouteWeight(const QString& start,
                                      const QString& end,
                                      TransportationMode mode,
                                      const QVariantMap& attrs)
{
    QMutexLocker locker(&m_mutex); // Lock for thread safety
    QString startCanonical = getCanonicalName(start);
    QString endCanonical = getCanonicalName(end);

    if (!m_terminals.contains(startCanonical) ||
        !m_terminals.contains(endCanonical)) {
        throw std::invalid_argument("Terminal not found");
    }

    GraphImpl::InternalEdge* edge = m_graph->findEdge(startCanonical,
                                                      endCanonical, mode);
    if (!edge) {
        throw std::invalid_argument("Route not found");
    }

    // Update edge attributes
    for (auto it = attrs.begin(); it != attrs.end(); ++it) {
        edge->attributes[it.key()] = it.value();
    }
    qDebug() << "Updated route weight from" << startCanonical
             << "to" << endCanonical;
}

/**
 * @brief Gets a terminal by name
 * @param name Terminal name
 * @return Pointer to terminal, or throws if not found
 */
Terminal* TerminalGraph::getTerminal(const QString& name) const
{
    QMutexLocker locker(&m_mutex); // Lock for thread safety
    QString canonical = getCanonicalName(name);
    if (!m_terminals.contains(canonical)) {
        throw std::invalid_argument("Terminal not found: " + name.toStdString());
    }
    return m_terminals[canonical];
}

/**
 * @brief Checks if a terminal exists
 * @param name Terminal name
 * @return True if exists, false otherwise
 */
bool TerminalGraph::terminalExists(const QString& name) const
{
    QMutexLocker locker(&m_mutex); // Lock for thread safety
    QString canonical = getCanonicalName(name);
    return m_terminals.contains(canonical);
}

/**
 * @brief Removes a terminal from the graph
 * @param name Terminal name
 * @return True if removed, false if not found
 */
bool TerminalGraph::removeTerminal(const QString& name)
{
    QMutexLocker locker(&m_mutex); // Lock for thread safety
    QString canonical = getCanonicalName(name);
    if (!m_terminals.contains(canonical)) {
        return false; // Terminal not found
    }

    // Remove aliases
    QSet<QString> aliases = m_canonicalToAliases.value(canonical);
    for (const QString& alias : aliases) {
        m_terminalAliases.remove(alias);
    }
    m_canonicalToAliases.remove(canonical);

    // Delete terminal and remove from graph
    delete m_terminals.take(canonical);
    m_graph->removeNode(canonical);
    qDebug() << "Removed terminal" << canonical;
    return true;
}

/**
 * @brief Gets the number of terminals
 * @return Terminal count
 */
int TerminalGraph::getTerminalCount() const
{
    QMutexLocker locker(&m_mutex); // Lock for thread safety
    return m_terminals.size();
}

/**
 * @brief Gets all terminal names
 * @param includeAliases Include aliases if true
 * @return Map of canonical names to alias lists
 */
QMap<QString, QStringList>
TerminalGraph::getAllTerminalNames(bool includeAliases) const
{
    QMutexLocker locker(&m_mutex); // Lock for thread safety
    QMap<QString, QStringList> result;

    if (includeAliases) {
        for (auto it = m_canonicalToAliases.begin();
             it != m_canonicalToAliases.end(); ++it) {
            result[it.key()] = it.value().values();
        }
    } else {
        for (auto it = m_terminals.begin(); it != m_terminals.end(); ++it) {
            result[it.key()] = QStringList();
        }
    }
    return result;
}

/**
 * @brief Clears the graph
 *
 * Deletes all terminals and resets graph state.
 */
void TerminalGraph::clear()
{
    QMutexLocker locker(&m_mutex); // Lock for thread safety
    for (auto it = m_terminals.begin(); it != m_terminals.end(); ++it) {
        delete it.value(); // Free each terminal
    }

    // Clear all containers
    m_terminals.clear();
    m_terminalAliases.clear();
    m_canonicalToAliases.clear();
    m_graph->clear();
    qDebug() << "Graph cleared";
}

/**
 * @brief Gets status of a terminal or all terminals
 * @param name Terminal name, empty for all
 * @return Status details
 *
 * Returns status info for one or all terminals.
 */
QVariantMap TerminalGraph::getTerminalStatus(const QString& name) const
{
    QMutexLocker locker(&m_mutex); // Lock for thread safety
    if (!name.isEmpty()) {
        QString canonical = getCanonicalName(name);
        if (!m_terminals.contains(canonical)) {
            throw std::invalid_argument("Terminal not found");
        }

        Terminal* term = m_terminals[canonical];
        QVariant region = m_graph->getNodeAttribute(canonical, "region");

        // Unlock before calling terminal methods
        locker.unlock();
        QVariantMap status;
        status["container_count"] = term->getContainerCount();
        status["available_capacity"] = term->getAvailableCapacity();
        status["max_capacity"] = term->getMaxCapacity();

        // Relock to access graph data
        locker.relock();
        status["region"] = region;
        status["aliases"] = QVariant(m_canonicalToAliases[canonical].values());
        return status;
    } else {
        // Copy data while locked
        QHash<QString, Terminal*> termsCopy = m_terminals;
        QHash<QString, QSet<QString>> aliasesCopy = m_canonicalToAliases;
        QMap<QString, QVariant> regions;
        for (auto it = termsCopy.begin(); it != termsCopy.end(); ++it) {
            regions[it.key()] = m_graph->getNodeAttribute(it.key(), "region");
        }

        // Unlock for terminal queries
        locker.unlock();
        QVariantMap result;
        for (auto it = termsCopy.begin(); it != termsCopy.end(); ++it) {
            QString canonical = it.key();
            Terminal* term = it.value();

            QVariantMap status;
            status["container_count"] = term->getContainerCount();
            status["available_capacity"] = term->getAvailableCapacity();
            status["max_capacity"] = term->getMaxCapacity();
            status["region"] = regions[canonical];
            status["aliases"] = QVariant(aliasesCopy[canonical].values());
            result[canonical] = status;
        }
        return result;
    }
}

/**
 * @brief Gets the terminal directory path
 * @return Directory path
 */
const QString& TerminalGraph::getPathToTerminalsDirectory() const
{
    return m_pathToTerminalsDirectory; // No lock needed for const
}

/**
 * @brief Finds the shortest path between terminals
 * @param start Starting terminal name
 * @param end Ending terminal name
 * @param mode Transportation mode
 * @return List of path segments
 *
 * Uses Dijkstra's algorithm to find shortest path.
 */
QList<PathSegment>
TerminalGraph::findShortestPath(const QString& start,
                                const QString& end,
                                TransportationMode mode) const
{
    QMutexLocker locker(&m_mutex); // Lock for thread safety
    QString startCanonical = getCanonicalName(start);
    QString endCanonical = getCanonicalName(end);

    if (!m_terminals.contains(startCanonical) ||
        !m_terminals.contains(endCanonical)) {
        throw std::invalid_argument("Terminal not found");
    }

    // Dijkstra's setup
    QHash<QString, double> distance;
    QHash<QString, QString> previous;
    QHash<QString, TransportationMode> edgeMode;
    QHash<QString, QVariantMap> edgeAttrs;

    QStringList nodes = m_graph->getNodes();
    for (const QString& node : nodes) {
        distance[node] = std::numeric_limits<double>::infinity();
        previous[node] = QString();
    }
    distance[startCanonical] = 0.0;

    QSet<QString> unvisited(nodes.begin(), nodes.end());
    while (!unvisited.isEmpty()) {
        // Find node with min distance
        QString current;
        double minDist = std::numeric_limits<double>::infinity();
        for (const QString& node : unvisited) {
            if (distance[node] < minDist) {
                minDist = distance[node];
                current = node;
            }
        }

        if (current.isEmpty() || minDist == std::numeric_limits<double>::infinity()) {
            break; // No reachable nodes left
        }

        if (current == endCanonical) {
            break; // Reached destination
        }

        unvisited.remove(current);
        const QList<GraphImpl::InternalEdge>& edges = m_graph->adjacencyList[current];
        for (const GraphImpl::InternalEdge& edge : edges) {
            if (mode != TransportationMode::Truck && edge.mode != mode) {
                continue; // Skip wrong mode
            }

            QString neighbor = edge.to;
            if (!unvisited.contains(neighbor)) {
                continue; // Already visited
            }

            // Compute cost including terminal delays
            Terminal* currTerm = m_terminals[current];
            Terminal* nextTerm = m_terminals[neighbor];
            double delay = currTerm->estimateContainerHandlingTime() +
                           nextTerm->estimateContainerHandlingTime();
            double cost = currTerm->estimateContainerCost() +
                          nextTerm->estimateContainerCost();

            QVariantMap params = edge.attributes;
            params["terminal_delay"] = delay;
            params["terminal_cost"] = cost;

            double totalCost = computeCost(params, m_costFunctionParametersWeights, edge.mode);
            double alt = distance[current] + totalCost;
            if (alt < distance[neighbor]) {
                distance[neighbor] = alt;
                previous[neighbor] = current;
                edgeMode[neighbor] = edge.mode;
                edgeAttrs[neighbor] = edge.attributes;
            }
        }
    }

    // Reconstruct path
    QList<PathSegment> path;
    if (previous[endCanonical].isEmpty() && startCanonical != endCanonical) {
        throw std::runtime_error("No path found");
    }

    QString current = endCanonical;
    while (!previous[current].isEmpty()) {
        PathSegment seg;
        seg.from = previous[current];
        seg.to = current;
        seg.mode = edgeMode[current];
        seg.weight = distance[current] - distance[seg.from];
        seg.fromTerminalId = seg.from;
        seg.toTerminalId = seg.to;
        seg.attributes = edgeAttrs[current];
        path.prepend(seg);
        current = seg.from;
    }
    return path;
}

/**
 * @brief Finds shortest path within regions
 * @param start Starting terminal name
 * @param end Ending terminal name
 * @param regions Allowed regions
 * @param mode Transportation mode
 * @return List of path segments
 *
 * Limits path finding to specified regions.
 */
QList<PathSegment>
TerminalGraph::findShortestPathWithinRegions(const QString& start,
                                             const QString& end,
                                             const QStringList& regions,
                                             TransportationMode mode) const
{
    QMutexLocker locker(&m_mutex); // Lock for thread safety
    QString startCanonical = getCanonicalName(start);
    QString endCanonical = getCanonicalName(end);

    if (!m_terminals.contains(startCanonical) ||
        !m_terminals.contains(endCanonical)) {
        throw std::invalid_argument("Terminal not found");
    }

    // Validate regions
    QVariant startRegion = m_graph->getNodeAttribute(startCanonical, "region");
    QVariant endRegion = m_graph->getNodeAttribute(endCanonical, "region");
    if (startRegion.isValid() && !regions.contains(startRegion.toString())) {
        throw std::invalid_argument("Start not in allowed regions");
    }
    if (endRegion.isValid() && !regions.contains(endRegion.toString())) {
        throw std::invalid_argument("End not in allowed regions");
    }

    // Create subgraph for allowed regions
    GraphImpl subgraph;
    for (const QString& node : m_graph->getNodes()) {
        QVariant region = m_graph->getNodeAttribute(node, "region");
        if (!region.isValid() || regions.contains(region.toString())) {
            subgraph.adjacencyList[node] = QList<GraphImpl::InternalEdge>();
            if (m_graph->nodeAttributes.contains(node)) {
                subgraph.nodeAttributes[node] = m_graph->nodeAttributes[node];
            }
        }
    }

    for (auto it = subgraph.adjacencyList.begin();
         it != subgraph.adjacencyList.end(); ++it) {
        if (m_graph->adjacencyList.contains(it.key())) {
            for (const GraphImpl::InternalEdge& edge : m_graph->adjacencyList[it.key()]) {
                if (subgraph.adjacencyList.contains(edge.to)) {
                    it.value().append(edge);
                }
            }
        }
    }

    // Dijkstra's on subgraph
    QHash<QString, double> distance;
    QHash<QString, QString> previous;
    QHash<QString, TransportationMode> edgeMode;
    QHash<QString, QVariantMap> edgeAttrs;

    QStringList nodes = subgraph.getNodes();
    for (const QString& node : nodes) {
        distance[node] = std::numeric_limits<double>::infinity();
        previous[node] = QString();
    }
    distance[startCanonical] = 0.0;

    QSet<QString> unvisited(nodes.begin(), nodes.end());
    while (!unvisited.isEmpty()) {
        QString current;
        double minDist = std::numeric_limits<double>::infinity();
        for (const QString& node : unvisited) {
            if (distance[node] < minDist) {
                minDist = distance[node];
                current = node;
            }
        }

        if (current.isEmpty() || minDist == std::numeric_limits<double>::infinity()) {
            break;
        }

        if (current == endCanonical) {
            break;
        }

        unvisited.remove(current);
        const QList<GraphImpl::InternalEdge>& edges = subgraph.adjacencyList[current];
        for (const GraphImpl::InternalEdge& edge : edges) {
            if (mode != TransportationMode::Truck && edge.mode != mode) {
                continue;
            }

            QString neighbor = edge.to;
            if (!unvisited.contains(neighbor)) {
                continue;
            }

            Terminal* currTerm = m_terminals[current];
            Terminal* nextTerm = m_terminals[neighbor];
            double delay = currTerm->estimateContainerHandlingTime() +
                           nextTerm->estimateContainerHandlingTime();
            double cost = currTerm->estimateContainerCost() +
                          nextTerm->estimateContainerCost();

            QVariantMap params = edge.attributes;
            params["terminal_delay"] = delay;
            params["terminal_cost"] = cost;

            double totalCost = computeCost(params, m_costFunctionParametersWeights, edge.mode);
            double alt = distance[current] + totalCost;
            if (alt < distance[neighbor]) {
                distance[neighbor] = alt;
                previous[neighbor] = current;
                edgeMode[neighbor] = edge.mode;
                edgeAttrs[neighbor] = edge.attributes;
            }
        }
    }

    // Reconstruct path
    QList<PathSegment> path;
    if (previous[endCanonical].isEmpty() && startCanonical != endCanonical) {
        throw std::runtime_error("No path in regions");
    }

    QString current = endCanonical;
    while (!previous[current].isEmpty()) {
        PathSegment seg;
        seg.from = previous[current];
        seg.to = current;
        seg.mode = edgeMode[current];
        seg.weight = distance[current] - distance[seg.from];
        seg.fromTerminalId = seg.from;
        seg.toTerminalId = seg.to;
        seg.attributes = edgeAttrs[current];
        path.prepend(seg);
        current = seg.from;
    }
    return path;
}

/**
 * @brief Finds top N shortest paths
 * @param start Starting terminal name
 * @param end Ending terminal name
 * @param n Number of paths to find
 * @param mode Transport mode (default: Truck)
 * @param skipDelays Skip same-mode delays if true
 * @return List of paths
 *
 * Uses Yen's algorithm to find k-shortest paths.
 * Avoids deadlock by pre-collecting data.
 */
QList<Path>
TerminalGraph::findTopNShortestPaths(const QString& start,
                                     const QString& end,
                                     int n,
                                     TransportationMode mode,
                                     bool skipDelays) const
{
    // Initial lock to gather graph data
    QMutexLocker locker(&m_mutex);

    // Resolve canonical names
    QString startCanonical = getCanonicalName(start);
    QString endCanonical = getCanonicalName(end);

    // Validate terminal existence
    if (!m_terminals.contains(startCanonical) ||
        !m_terminals.contains(endCanonical)) {
        throw std::invalid_argument("Terminal not found");
    }

    // Copy graph and terminal data
    QHash<QString, QList<GraphImpl::InternalEdge>> adjList =
        m_graph->adjacencyList;
    QHash<QString, Terminal*> termPointers = m_terminals;

    // Unlock before path finding
    locker.unlock();

    QList<Path> result; // Store final paths

    // Get the shortest path
    QList<PathSegment> shortestPath;
    try {
        shortestPath = findShortestPath(startCanonical,
                                        endCanonical, mode);
    } catch (const std::exception& e) {
        qWarning() << "No path found:" << e.what();
        return result; // Return empty if no path
    }

    if (shortestPath.isEmpty()) {
        return result; // No path available
    }

    // Pre-collect terminal info
    QSet<QString> termNames;
    termNames.insert(startCanonical);
    for (const PathSegment& seg : shortestPath) {
        termNames.insert(seg.from);
        termNames.insert(seg.to);
    }

    struct TermInfo {
        double handlingTime;
        double cost;
    };
    QMap<QString, TermInfo> termInfoMap;

    // Gather terminal data without lock
    for (const QString& name : termNames) {
        Terminal* term = termPointers[name];
        double time = term->estimateContainerHandlingTime();
        double cost = term->estimateContainerCost();
        termInfoMap[name] = {time, cost};
    }

    // Build first path
    double totalPathCost = 0.0;
    double totalEdgeCosts = 0.0;
    double totalTermCosts = 0.0;
    QList<QVariantMap> terminalsInPath;

    // Add start terminal
    QVariantMap startInfo;
    startInfo["terminal"] = startCanonical;
    startInfo["handling_time"] = termInfoMap[startCanonical].handlingTime;
    startInfo["cost"] = termInfoMap[startCanonical].cost;
    startInfo["costs_skipped"] = false;
    terminalsInPath.append(startInfo);

    // Process segments
    for (const PathSegment& seg : shortestPath) {
        totalEdgeCosts += seg.weight;

        // Add terminal info
        QVariantMap termInfo;
        termInfo["terminal"] = seg.to;
        termInfo["handling_time"] = termInfoMap[seg.to].handlingTime;
        termInfo["cost"] = termInfoMap[seg.to].cost;

        // Check if costs should be skipped
        bool skip = false;
        if (skipDelays && terminalsInPath.size() > 1) {
            int prevIdx = terminalsInPath.size() - 2;
            if (shortestPath[prevIdx].mode == seg.mode) {
                skip = true;
            }
        }
        termInfo["costs_skipped"] = skip;
        terminalsInPath.append(termInfo);
    }

    // Calculate total costs
    totalPathCost = totalEdgeCosts;
    for (const QVariantMap& info : terminalsInPath) {
        if (!info["costs_skipped"].toBool()) {
            totalTermCosts += info["cost"].toDouble();
        }
    }
    totalPathCost += totalTermCosts;

    // Add first path to result
    Path firstPath;
    firstPath.pathId = 1;
    firstPath.totalPathCost = totalPathCost;
    firstPath.totalEdgeCosts = totalEdgeCosts;
    firstPath.totalTerminalCosts = totalTermCosts;
    firstPath.terminalsInPath = terminalsInPath;
    firstPath.segments = shortestPath;
    result.append(firstPath);

    if (n <= 1) {
        return result; // Only one path requested
    }

    // Yen's algorithm setup
    struct PotentialPath {
        double cost;
        QList<QString> nodes;
        QList<PathSegment> segments;

        bool operator<(const PotentialPath& other) const {
            return cost < other.cost;
        }
    };

    QList<PotentialPath> potentialPaths;
    QSet<QStringList> pathsFound;

    // Add first path to found set
    QStringList firstNodes;
    firstNodes.append(startCanonical);
    for (const PathSegment& seg : shortestPath) {
        firstNodes.append(seg.to);
    }
    pathsFound.insert(firstNodes);

    // Yen's algorithm main loop
    for (int k = 1; k < n; ++k) {
        const Path& prevPath = result[k - 1];
        const QList<PathSegment>& prevSegs = prevPath.segments;

        // Iterate over spur nodes
        for (int i = 0; i < prevSegs.size(); ++i) {
            QString spurNode = prevSegs[i].from;

            // Build root path
            QList<PathSegment> rootPath;
            for (int j = 0; j < i; ++j) {
                rootPath.append(prevSegs[j]);
            }

            // Get root path nodes
            QStringList rootNodes;
            if (rootPath.isEmpty()) {
                rootNodes.append(spurNode);
            } else {
                rootNodes.append(rootPath.first().from);
                for (const PathSegment& seg : rootPath) {
                    rootNodes.append(seg.to);
                }
            }

            // Set exclusions
            QSet<QPair<QString, QString>> edgesToExclude;
            QSet<QString> nodesToExclude;

            // Exclude root path edges
            for (int j = 0; j < rootNodes.size() - 1; ++j) {
                edgesToExclude.insert({rootNodes[j], rootNodes[j + 1]});
                edgesToExclude.insert({rootNodes[j + 1], rootNodes[j]});
            }

            // Exclude root path nodes except spur
            for (int j = 0; j < rootNodes.size() - 1; ++j) {
                if (rootNodes[j] != spurNode) {
                    nodesToExclude.insert(rootNodes[j]);
                }
            }

            // Exclude edges from previous paths
            for (const Path& path : result) {
                bool matches = true;
                for (int j = 0; j < rootPath.size(); ++j) {
                    if (j >= path.segments.size() ||
                        path.segments[j].from != rootPath[j].from ||
                        path.segments[j].to != rootPath[j].to) {
                        matches = false;
                        break;
                    }
                }
                if (matches && rootPath.size() < path.segments.size()) {
                    edgesToExclude.insert(
                        {path.segments[rootPath.size()].from,
                         path.segments[rootPath.size()].to});
                }
            }

            // Find spur path without lock
            QList<PathSegment> spurPath;
            try {
                spurPath = findShortestPathWithExclusions(
                    spurNode, endCanonical, mode,
                    edgesToExclude, nodesToExclude);
            } catch (const std::exception&) {
                continue; // No spur path, try next
            }

            // Combine paths
            QList<PathSegment> totalPath = rootPath;
            totalPath.append(spurPath);

            // Build node list
            QStringList totalNodes;
            totalNodes.append(totalPath.first().from);
            for (const PathSegment& seg : totalPath) {
                totalNodes.append(seg.to);
            }

            // Check for duplicates or cycles
            if (pathsFound.contains(totalNodes)) {
                continue; // Skip duplicate path
            }
            QSet<QString> uniqueNodes;
            for (const QString& node : totalNodes) {
                if (uniqueNodes.contains(node)) {
                    continue; // Skip cyclic path
                }
                uniqueNodes.insert(node);
            }

            // Calculate path cost
            double pathCost = 0.0;
            for (const PathSegment& seg : totalPath) {
                pathCost += seg.weight;
            }

            // Add to potential paths
            PotentialPath potPath;
            potPath.cost = pathCost;
            potPath.nodes = totalNodes;
            potPath.segments = totalPath;
            potentialPaths.append(potPath);
        }

        if (potentialPaths.isEmpty()) {
            break; // No more paths available
        }

        // Sort and select best path
        std::sort(potentialPaths.begin(), potentialPaths.end());
        PotentialPath bestPath = potentialPaths.takeFirst();

        // Build path details
        double edgeCosts = 0.0;
        double termCosts = 0.0;
        QList<QVariantMap> termsInPath;

        // Add start terminal
        QVariantMap startTerm;
        startTerm["terminal"] = startCanonical;
        startTerm["handling_time"] = termInfoMap[startCanonical].handlingTime;
        startTerm["cost"] = termInfoMap[startCanonical].cost;
        startTerm["costs_skipped"] = false;
        termsInPath.append(startTerm);

        // Process segments
        for (const PathSegment& seg : bestPath.segments) {
            edgeCosts += seg.weight;

            // Add terminal info
            QVariantMap termInfo;
            termInfo["terminal"] = seg.to;
            termInfo["handling_time"] = termInfoMap[seg.to].handlingTime;
            termInfo["cost"] = termInfoMap[seg.to].cost;

            bool skip = false;
            if (skipDelays && termsInPath.size() > 1) {
                int prevIdx = termsInPath.size() - 2;
                if (bestPath.segments[prevIdx].mode == seg.mode) {
                    skip = true;
                }
            }
            termInfo["costs_skipped"] = skip;
            termsInPath.append(termInfo);
        }

        // Compute total cost
        double totalCost = edgeCosts;
        for (const QVariantMap& info : termsInPath) {
            if (!info["costs_skipped"].toBool()) {
                termCosts += info["cost"].toDouble();
            }
        }
        totalCost += termCosts;

        // Create and add path
        Path nextPath;
        nextPath.pathId = k + 1;
        nextPath.totalPathCost = totalCost;
        nextPath.totalEdgeCosts = edgeCosts;
        nextPath.totalTerminalCosts = termCosts;
        nextPath.terminalsInPath = termsInPath;
        nextPath.segments = bestPath.segments;
        result.append(nextPath);

        pathsFound.insert(bestPath.nodes);
    }

    qDebug() << "Found" << result.size() << "shortest paths";
    return result;
}

/**
 * @brief Serializes the graph to JSON
 * @return JSON object representing the graph
 *
 * Includes terminals, edges, and aliases.
 */
QJsonObject TerminalGraph::serializeGraph() const
{
    QMutexLocker locker(&m_mutex); // Lock for thread safety
    QJsonObject graphData;

    // Serialize terminals
    QJsonObject terminalsJson;
    for (auto it = m_terminals.begin();
         it != m_terminals.end(); ++it) {
        QJsonObject termData;
        termData["config"] = it.value()->toJson();

        // Add node attributes
        QJsonObject nodeData;
        if (m_graph->nodeAttributes.contains(it.key())) {
            for (auto attrIt = m_graph->nodeAttributes[it.key()].begin();
                 attrIt != m_graph->nodeAttributes[it.key()].end(); ++attrIt) {
                nodeData[attrIt.key()] = QJsonValue::fromVariant(attrIt.value());
            }
        }
        termData["node_data"] = nodeData;
        terminalsJson[it.key()] = termData;
    }
    graphData["terminals"] = terminalsJson;

    // Serialize edges
    QJsonArray edgesJson;
    for (auto it = m_graph->adjacencyList.begin();
         it != m_graph->adjacencyList.end(); ++it) {
        for (const GraphImpl::InternalEdge& edge : it.value()) {
            QJsonObject edgeData;
            edgeData["from"] = it.key();
            edgeData["to"] = edge.to;
            edgeData["route_id"] = edge.routeId;
            edgeData["mode"] = static_cast<int>(edge.mode);
            QJsonObject attrs;
            for (auto attrIt = edge.attributes.begin();
                 attrIt != edge.attributes.end(); ++attrIt) {
                attrs[attrIt.key()] = QJsonValue::fromVariant(attrIt.value());
            }
            edgeData["attributes"] = attrs;
            edgesJson.append(edgeData);
        }
    }
    graphData["edges"] = edgesJson;

    // Serialize terminal aliases
    QJsonObject aliasesJson;
    for (auto it = m_terminalAliases.begin();
         it != m_terminalAliases.end(); ++it) {
        aliasesJson[it.key()] = it.value();
    }
    graphData["terminal_aliases"] = aliasesJson;

    // Serialize canonical-to-aliases mapping
    QJsonObject canonJson;
    for (auto it = m_canonicalToAliases.begin();
         it != m_canonicalToAliases.end(); ++it) {
        QJsonArray aliasesArray;
        for (const QString& alias : it.value()) {
            aliasesArray.append(alias);
        }
        canonJson[it.key()] = aliasesArray;
    }
    graphData["canonical_to_aliases"] = canonJson;

    // Serialize cost function weights
    QJsonObject weightsJson;
    for (auto it = m_costFunctionParametersWeights.begin();
         it != m_costFunctionParametersWeights.end(); ++it) {
        QJsonObject paramsJson;
        QVariantMap params = it.value().toMap();
        for (auto pIt = params.begin(); pIt != params.end(); ++pIt) {
            paramsJson[pIt.key()] = QJsonValue::fromVariant(pIt.value());
        }
        weightsJson[it.key()] = paramsJson;
    }
    graphData["cost_function_weights"] = weightsJson;

    // Serialize default link attributes
    QJsonObject attrsJson;
    for (auto it = m_defaultLinkAttributes.begin();
         it != m_defaultLinkAttributes.end(); ++it) {
        attrsJson[it.key()] = QJsonValue::fromVariant(it.value());
    }
    graphData["default_link_attributes"] = attrsJson;

    qDebug() << "Serialized graph with"
             << m_terminals.size() << "terminals";
    return graphData;
}

/**
 * @brief Deserializes a graph from JSON
 * @param data JSON data to deserialize
 * @param dir Directory path for terminal storage
 * @return Pointer to new TerminalGraph instance
 *
 * Reconstructs graph with terminals and aliases.
 */
TerminalGraph* TerminalGraph::deserializeGraph(const QJsonObject& data,
                                               const QString& dir)
{
    TerminalGraph* graph = new TerminalGraph(dir);
    try {
        // Restore cost function weights
        if (data.contains("cost_function_weights")) {
            QJsonObject weightsJson =
                data["cost_function_weights"].toObject();
            for (auto it = weightsJson.begin();
                 it != weightsJson.end(); ++it) {
                if (it.value().isObject()) {
                    QJsonObject paramsJson =
                        it.value().toObject();
                    QVariantMap paramsMap;
                    for (auto pIt = paramsJson.begin();
                         pIt != paramsJson.end(); ++pIt) {
                        paramsMap[pIt.key()] = pIt.value().toVariant();
                    }
                    graph->m_costFunctionParametersWeights[it.key()] =
                        paramsMap;
                }
            }
        }

        // Restore default link attributes
        if (data.contains("default_link_attributes")) {
            QJsonObject attrsJson =
                data["default_link_attributes"].toObject();
            for (auto it = attrsJson.begin();
                 it != attrsJson.end(); ++it) {
                graph->m_defaultLinkAttributes[it.key()] =
                    it.value().toVariant();
            }
        }

        // Restore terminal aliases
        if (data.contains("terminal_aliases")) {
            QJsonObject aliasesJson =
                data["terminal_aliases"].toObject();
            for (auto it = aliasesJson.begin();
                 it != aliasesJson.end(); ++it) {
                if (it.value().isString()) {
                    graph->m_terminalAliases[it.key()] =
                        it.value().toString();
                }
            }
        }

        // Restore canonical-to-aliases mapping
        if (data.contains("canonical_to_aliases")) {
            QJsonObject canonJson = data["canonical_to_aliases"].toObject();
            for (auto it = canonJson.begin(); it != canonJson.end(); ++it) {
                if (it.value().isArray()) {
                    QJsonArray aliasesArray =
                        it.value().toArray();
                    QSet<QString> aliases;
                    for (const QJsonValue& val : aliasesArray) {
                        if (val.isString()) {
                            aliases.insert(val.toString());
                        }
                    }
                    graph->m_canonicalToAliases[it.key()] = aliases;
                }
            }
        }

        // Restore terminals
        if (data.contains("terminals")) {
            QJsonObject termsJson = data["terminals"].toObject();
            for (auto it = termsJson.begin(); it != termsJson.end(); ++it) {
                QString termName = it.key();
                if (!it.value().isObject()) {
                    continue;
                }
                QJsonObject termData = it.value().toObject();
                if (!termData.contains("config") ||
                    !termData["config"].isObject()) {
                    continue;
                }
                QJsonObject config = termData["config"].toObject();
                config["terminal_name"] = termName;
                Terminal* term = Terminal::fromJson(config, dir);
                if (!term) {
                    qWarning() << "Failed to create terminal:" << termName;
                    continue;
                }
                graph->m_terminals[termName] = term;
                graph->m_graph->adjacencyList[termName] =
                    QList<GraphImpl::InternalEdge>();
                if (termData.contains("node_data") &&
                    termData["node_data"].isObject()) {
                    QJsonObject nodeData = termData["node_data"].toObject();
                    for (auto nIt = nodeData.begin();
                         nIt != nodeData.end(); ++nIt) {
                        graph->m_graph->setNodeAttribute(
                            termName,
                            nIt.key(),
                            nIt.value().toVariant());
                    }
                }
            }
        }

        // Restore edges
        if (data.contains("edges") && data["edges"].isArray()) {
            QJsonArray edgesJson = data["edges"].toArray();
            for (const QJsonValue& val : edgesJson) {
                if (!val.isObject()) {
                    continue;
                }
                QJsonObject edgeData = val.toObject();
                if (!edgeData.contains("from") ||
                    !edgeData.contains("to") ||
                    !edgeData.contains("mode") ||
                    !edgeData.contains("attributes")) {
                    continue;
                }
                QString from = edgeData["from"].toString();
                QString to = edgeData["to"].toString();
                int modeInt = edgeData["mode"].toInt();
                QString routeId =
                    edgeData.contains("route_id") ?
                        edgeData["route_id"].toString() : QString();
                TransportationMode mode =
                    static_cast<TransportationMode>(modeInt);
                QVariantMap attrs;
                QJsonObject attrsJson =
                    edgeData["attributes"].toObject();
                for (auto aIt = attrsJson.begin();
                     aIt != attrsJson.end(); ++aIt) {
                    attrs[aIt.key()] = aIt.value().toVariant();
                }
                graph->m_graph->addEdge(from, to, routeId, mode, attrs);
            }
        }

        qDebug() << "Graph deserialized with"
                 << graph->m_terminals.size() << "terminals";
        return graph;

    } catch (const std::exception& e) {
        qWarning() << "Deserialization failed:" << e.what();
        delete graph;
        throw;
    }
}

/**
 * @brief Saves the graph to a file
 * @param filepath Path to save the file
 */
void TerminalGraph::saveToFile(const QString& filepath) const
{
    QJsonObject graphData = serializeGraph();
    QFile file(filepath);
    if (!file.open(QIODevice::WriteOnly)) {
        throw std::runtime_error("Cannot open file");
    }

    QJsonDocument doc(graphData);
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
    qInfo() << "Graph saved to" << filepath;
}

/**
 * @brief Loads a graph from a file
 * @param filepath Path to load from
 * @param dir Directory path for terminal storage
 * @return Pointer to loaded TerminalGraph
 */
TerminalGraph* TerminalGraph::loadFromFile(const QString& filepath,
                                           const QString& dir)
{
    QFile file(filepath);
    if (!file.open(QIODevice::ReadOnly)) {
        throw std::runtime_error("Cannot open file");
    }

    QByteArray data = file.readAll();
    file.close();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    return deserializeGraph(doc.object(), dir);
}

/**
 * @brief Resolves canonical name from alias
 * @param name Terminal name or alias
 * @return Canonical name
 */
QString TerminalGraph::getCanonicalName(const QString& name) const
{
    return m_terminalAliases.value(name, name);
}

/**
 * @brief Computes cost for a path segment
 * @param params Parameters for cost calculation
 * @param weights Weights for cost factors
 * @param mode Transportation mode
 * @return Computed cost
 */
double TerminalGraph::computeCost(const QVariantMap& params,
                                  const QVariantMap& weights,
                                  TransportationMode mode) const
{
    double cost = 0.0;
    QString modeStr = QString::number(static_cast<int>(mode));
    QVariantMap modeWeights = weights.value(modeStr, weights.value("default")).toMap();

    for (auto it = params.begin(); it != params.end(); ++it) {
        bool ok;
        double value = it.value().toDouble(&ok);
        if (!ok) continue;

        double weight = modeWeights.value(it.key(), 1.0).toDouble();
        cost += weight * value;
    }
    return cost;
}

/**
 * @brief Finds shortest path with exclusions
 * @param start Starting terminal name
 * @param end Ending terminal name
 * @param mode Transportation mode
 * @param edges Edges to exclude
 * @param nodes Nodes to exclude
 * @return List of path segments
 *
 * Uses Dijkstra's algorithm with edge and node exclusions.
 * Computes costs including terminal delays.
 */
QList<PathSegment>
TerminalGraph::findShortestPathWithExclusions(const QString& start,
                                              const QString& end,
                                              TransportationMode mode,
                                              const QSet<QPair<QString, QString>>& edges,
                                              const QSet<QString>& nodes) const
{
    QMutexLocker locker(&m_mutex); // Lock for thread safety
    QString startCanonical = getCanonicalName(start);
    QString endCanonical = getCanonicalName(end);

    // Validate terminal existence
    if (!m_terminals.contains(startCanonical) ||
        !m_terminals.contains(endCanonical)) {
        throw std::invalid_argument("Terminal not found");
    }

    // Check if start or end is excluded
    if (nodes.contains(startCanonical) ||
        nodes.contains(endCanonical)) {
        throw std::invalid_argument("Start/end excluded");
    }

    // Dijkstra's setup with exclusions
    QHash<QString, double> distance;
    QHash<QString, QString> previous;
    QHash<QString, TransportationMode> edgeMode;
    QHash<QString, QVariantMap> edgeAttrs;

    // Initialize distances for all nodes
    QStringList allNodes = m_graph->getNodes();
    for (const QString& node : allNodes) {
        if (nodes.contains(node)) {
            continue; // Skip excluded nodes
        }
        distance[node] = std::numeric_limits<double>::infinity();
        previous[node] = QString(); // No predecessor yet
    }
    distance[startCanonical] = 0.0; // Start at zero

    // Priority queue simulation with unvisited set
    QSet<QString> unvisited;
    for (const QString& node : allNodes) {
        if (!nodes.contains(node)) {
            unvisited.insert(node);
        }
    }

    // Main Dijkstra's loop
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

        // Break if no reachable node found
        if (current.isEmpty() ||
            minDist == std::numeric_limits<double>::infinity()) {
            break; // No path possible
        }

        // Stop if destination reached
        if (current == endCanonical) {
            break;
        }

        unvisited.remove(current); // Mark as visited

        // Process neighbors
        const QList<GraphImpl::InternalEdge>& edgeList =
            m_graph->adjacencyList[current];
        for (const GraphImpl::InternalEdge& edge : edgeList) {
            QString neighbor = edge.to;

            // Skip if excluded or wrong mode
            if (nodes.contains(neighbor) ||
                (mode != TransportationMode::Truck && edge.mode != mode)) {
                continue;
            }

            // Skip if edge is excluded
            QPair<QString, QString> edgePair(current, neighbor);
            if (edges.contains(edgePair)) {
                continue; // Edge excluded
            }

            // Skip if already visited
            if (!unvisited.contains(neighbor)) {
                continue;
            }

            // Calculate cost with terminal delays
            Terminal* currTerm = m_terminals[current];
            Terminal* nextTerm = m_terminals[neighbor];
            double delay = currTerm->estimateContainerHandlingTime() +
                           nextTerm->estimateContainerHandlingTime();
            double termCost = currTerm->estimateContainerCost() +
                              nextTerm->estimateContainerCost();

            // Prepare cost parameters
            QVariantMap params = edge.attributes;
            params["terminal_delay"] = delay;
            params["terminal_cost"] = termCost;

            // Compute total cost for this edge
            double totalCost = computeCost(params,
                                           m_costFunctionParametersWeights,
                                           edge.mode);

            // Update distance if shorter path found
            double alt = distance[current] + totalCost;
            if (alt < distance[neighbor]) {
                distance[neighbor] = alt;
                previous[neighbor] = current;
                edgeMode[neighbor] = edge.mode;
                edgeAttrs[neighbor] = edge.attributes;
            }
        }
    }

    // Reconstruct the path
    QList<PathSegment> path;

    // Check if path exists
    if (previous[endCanonical].isEmpty() &&
        startCanonical != endCanonical) {
        throw std::runtime_error("No path found with exclusions");
    }

    // Build path from end to start
    QString current = endCanonical;
    while (!previous[current].isEmpty()) {
        QString from = previous[current];
        PathSegment seg;

        // Populate segment details
        seg.from = from;
        seg.to = current;
        seg.mode = edgeMode[current];
        seg.weight = distance[current] - distance[from];
        seg.fromTerminalId = from;
        seg.toTerminalId = current;
        seg.attributes = edgeAttrs[current];

        // Add segment to path (in reverse order)
        path.prepend(seg);
        current = from; // Move to previous node
    }

    qDebug() << "Path found with" << path.size() << "segments";
    return path;
}

} // namespace TerminalSim
