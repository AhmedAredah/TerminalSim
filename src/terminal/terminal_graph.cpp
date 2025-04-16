/**
 * @file terminal_graph.cpp
 * @brief Implementation of the TerminalGraph class
 * @author Ahmed Aredah
 * @date 2025-03-21
 *
 * This file contains the implementation of the TerminalGraph class and its
 * methods. It provides functionality for managing terminals, routes, regions,
 * and finding paths in a transportation network simulation.
 */

#include "terminal_graph.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPair>
#include <QQueue>
#include <QRandomGenerator>
#include <QSet>
#include <QThread>
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace TerminalSim
{

/**
 * @brief Constructs a TerminalGraph instance
 * @param dir Directory path for terminal storage
 *
 * Initializes graph with default cost weights and
 * link attributes. Sets up the cost function parameters
 * for different transportation modes and initializes
 * default link attributes.
 */
TerminalGraph::TerminalGraph(const QString &dir)
    : QObject(nullptr)
    , m_graph(new GraphImpl())
    , m_pathToTerminalsDirectory(dir)
{
    // Initialize default cost function parameters
    m_costFunctionParametersWeights = {
        {"default", QVariantMap{{"cost", 1.0},
                                {"travellTime", 1.0},
                                {"distance", 1.0},
                                {"carbonEmissions", 1.0},
                                {"risk", 1.0},
                                {"energyConsumption", 1.0},
                                {"terminal_delay", 1.0},
                                {"terminal_cost", 1.0}}},
        {QString::number(static_cast<int>(TransportationMode::Ship)),
         QVariantMap{{"cost", 1.0},
                     {"travellTime", 1.0},
                     {"distance", 1.0},
                     {"carbonEmissions", 1.0},
                     {"risk", 1.0},
                     {"energyConsumption", 1.0},
                     {"terminal_delay", 1.0},
                     {"terminal_cost", 1.0}}},
        {QString::number(static_cast<int>(TransportationMode::Train)),
         QVariantMap{{"cost", 1.0},
                     {"travellTime", 1.0},
                     {"distance", 1.0},
                     {"carbonEmissions", 1.0},
                     {"risk", 1.0},
                     {"energyConsumption", 1.0},
                     {"terminal_delay", 1.0},
                     {"terminal_cost", 1.0}}},
        {QString::number(static_cast<int>(TransportationMode::Truck)),
         QVariantMap{{"cost", 1.0},
                     {"travellTime", 1.0},
                     {"distance", 1.0},
                     {"carbonEmissions", 1.0},
                     {"risk", 1.0},
                     {"energyConsumption", 1.0},
                     {"terminal_delay", 1.0},
                     {"terminal_cost", 1.0}}}};

    // Set default link attributes
    m_defaultLinkAttributes = {{"cost", 1.0},     {"travellTime", 1.0},
                               {"distance", 1.0}, {"carbonEmissions", 1.0},
                               {"risk", 1.0},     {"energyConsumption", 1.0}};

    // Log initialization
    qInfo() << "Graph initialized with dir:" << (dir.isEmpty() ? "None" : dir);
}

/**
 * @brief Destructor, cleans up resources
 *
 * Deletes all terminal instances and the graph implementation.
 * Ensures thread-safe cleanup by locking the mutex during destruction.
 */
TerminalGraph::~TerminalGraph()
{
    QMutexLocker locker(&m_mutex); // Lock for thread safety
    for (auto it = m_terminals.begin(); it != m_terminals.end(); ++it)
    {
        delete it.value(); // Free each terminal
    }
    delete m_graph;                // Free graph implementation
    qDebug() << "Graph destroyed"; // Log destruction
}

/**
 * @brief Sets default attributes for links
 * @param attrs Attributes to set as default
 *
 * Updates default link attributes thread-safely.
 * These attributes will be applied to any new links
 * created in the graph unless overridden.
 */
void TerminalGraph::setLinkDefaultAttributes(const QVariantMap &attrs)
{
    QMutexLocker locker(&m_mutex);   // Ensure thread safety
    m_defaultLinkAttributes = attrs; // Update attributes
}

/**
 * @brief Sets cost function parameters
 * @param params Weights for cost computation
 *
 * Updates cost weights thread-safely. These weights determine
 * how different factors (travel time, distance, cost, etc.)
 * are prioritized when computing path costs.
 */
void TerminalGraph::setCostFunctionParameters(const QVariantMap &params)
{
    QMutexLocker locker(&m_mutex);            // Ensure thread safety
    m_costFunctionParametersWeights = params; // Update weights
}

/**
 * @brief Adds a terminal to the graph
 * @param names List of names (first is canonical)
 * @param terminalDisplayName Display name for the terminal
 * @param config Custom config for terminal
 * @param interfaces Interfaces and modes
 * @param region Region name (optional)
 * @return Pointer to the newly created Terminal
 * @throws std::invalid_argument If no names provided or terminal already exists
 *
 * Creates and adds a terminal with aliases. The first name in the list
 * is used as the canonical name, and all others are treated as aliases.
 * Thread-safety is ensured by locking during the operation.
 */
Terminal *TerminalGraph::addTerminal(
    const QStringList &names, const QString &terminalDisplayName,
    const QVariantMap                                       &config,
    const QMap<TerminalInterface, QSet<TransportationMode>> &interfaces,
    const QString                                           &region)
{
    QMutexLocker locker(&m_mutex); // Lock for thread safety
    if (names.isEmpty())
    {
        throw std::invalid_argument("No terminal names provided");
    }

    QString canonical = names.first(); // First name is canonical
    if (m_terminals.contains(canonical))
    {
        throw std::invalid_argument("Terminal exists: "
                                    + canonical.toStdString());
    }

    // Create new terminal instance
    Terminal *term = new Terminal(
        canonical, terminalDisplayName, interfaces,
        QMap<QPair<TransportationMode, QString>, QString>(),
        config.value("capacity").toMap(), config.value("dwell_time").toMap(),
        config.value("customs").toMap(), config.value("cost").toMap(),
        m_pathToTerminalsDirectory);

    // Add to graph structure
    m_graph->adjacencyList[canonical] = QList<GraphImpl::InternalEdge>();
    if (!region.isEmpty())
    {
        m_graph->setNodeAttribute(canonical, "region", region);
    }

    // Store terminal and aliases
    m_terminals[canonical]          = term;
    m_canonicalToAliases[canonical] = QSet<QString>(names.begin(), names.end());
    for (const QString &alias : names)
    {
        m_terminalAliases[alias] = canonical;
    }

    qDebug() << "Added terminal" << canonical << "with" << (names.size() - 1)
             << "aliases";

    return term; // Return terminal instance
}

/**
 * @brief Adds multiple terminals to the graph simultaneously
 * @param terminalsList List of terminal configurations
 * @return QMap of canonical names to Terminal pointers
 * @throws std::invalid_argument If required fields are missing or terminals
 * conflict
 *
 * Creates and adds multiple terminals in one operation. This method
 * first validates all terminals to ensure consistency, then adds them
 * to the graph. Each terminal configuration should include names,
 * display name, interfaces, and custom configuration.
 */
QMap<QString, Terminal *>
TerminalGraph::addTerminals(const QList<QVariantMap> &terminalsList)
{
    QMutexLocker              locker(&m_mutex); // Lock for thread safety
    QMap<QString, Terminal *> addedTerminals;
    QSet<QString>
        allNames; // Track all names across terminals to check for duplicates

    // First validate all terminals
    for (const QVariantMap &terminalData : terminalsList)
    {
        // Validate required fields
        if (!terminalData.contains("terminal_names")
            || !terminalData.contains("display_name")
            || !terminalData.contains("terminal_interfaces")
            || !terminalData.contains("custom_config"))
        {
            throw std::invalid_argument("Missing required fields for terminal");
        }

        // Extract terminal names for validation
        QStringList terminalNames;
        QVariant    namesVar = terminalData["terminal_names"];
        if (namesVar.canConvert<QString>())
        {
            terminalNames << namesVar.toString();
        }
        else if (namesVar.canConvert<QStringList>())
        {
            terminalNames = namesVar.toStringList();
        }
        else
        {
            throw std::invalid_argument(
                "terminal_names must be a string or list of strings");
        }

        if (terminalNames.isEmpty())
        {
            throw std::invalid_argument(
                "At least one terminal name must be provided");
        }

        QString canonical = terminalNames.first();

        // Check if terminal already exists
        if (m_terminals.contains(canonical))
        {
            throw std::invalid_argument("Terminal exists: "
                                        + canonical.toStdString());
        }

        // Check for name conflicts with existing terminals
        for (const QString &name : terminalNames)
        {
            if (allNames.contains(name))
            {
                throw std::invalid_argument("Duplicate terminal name: "
                                            + name.toStdString());
            }
            allNames.insert(name);
        }
    }

    // Add all terminals
    for (const QVariantMap &terminalData : terminalsList)
    {
        QStringList terminalNames;
        QVariant    namesVar = terminalData["terminal_names"];
        if (namesVar.canConvert<QString>())
        {
            terminalNames << namesVar.toString();
        }
        else
        {
            terminalNames = namesVar.toStringList();
        }

        QString     displayName  = terminalData["display_name"].toString();
        QVariantMap customConfig = terminalData["custom_config"].toMap();
        QString     region = terminalData.value("region", QString()).toString();

        // Parse interfaces
        QVariantMap interfacesMap = terminalData["terminal_interfaces"].toMap();
        QMap<TerminalInterface, QSet<TransportationMode>> interfaces;

        for (auto it = interfacesMap.constBegin();
             it != interfacesMap.constEnd(); ++it)
        {
            TerminalInterface interface;
            bool              ok = false;

            int interfaceInt = it.key().toInt(&ok);
            if (ok)
            {
                interface = static_cast<TerminalInterface>(interfaceInt);
            }
            else
            {
                interface = EnumUtils::stringToTerminalInterface(it.key());
            }

            QSet<TransportationMode> modes;
            QVariantList             modesList = it.value().toList();

            for (const QVariant &modeVar : modesList)
            {
                TransportationMode mode;

                if (modeVar.canConvert<int>())
                {
                    mode = static_cast<TransportationMode>(modeVar.toInt());
                }
                else if (modeVar.canConvert<QString>())
                {
                    mode = EnumUtils::stringToTransportationMode(
                        modeVar.toString());
                }
                else
                {
                    continue;
                }

                modes.insert(mode);
            }

            if (!modes.isEmpty())
            {
                interfaces[interface] = modes;
            }
        }

        if (interfaces.isEmpty())
        {
            throw std::invalid_argument(
                "At least one terminal interface with modes must be provided");
        }

        // Create terminal
        QString   canonical = terminalNames.first();
        Terminal *term      = new Terminal(
            canonical, displayName, interfaces,
            QMap<QPair<TransportationMode, QString>, QString>(),
            customConfig.value("capacity").toMap(),
            customConfig.value("dwell_time").toMap(),
            customConfig.value("customs").toMap(),
            customConfig.value("cost").toMap(), m_pathToTerminalsDirectory);

        // Add to graph
        m_graph->adjacencyList[canonical] = QList<GraphImpl::InternalEdge>();
        if (!region.isEmpty())
        {
            m_graph->setNodeAttribute(canonical, "region", region);
        }

        // Store terminal and aliases
        m_terminals[canonical] = term;
        m_canonicalToAliases[canonical] =
            QSet<QString>(terminalNames.begin(), terminalNames.end());
        for (const QString &alias : terminalNames)
        {
            m_terminalAliases[alias] = canonical;
        }

        addedTerminals[canonical] = term;

        qDebug() << "Added terminal" << canonical << "with"
                 << (terminalNames.size() - 1) << "aliases";
    }

    return addedTerminals;
}

/**
 * @brief Adds an alias to a terminal
 * @param name Terminal name to alias
 * @param alias New alias to add
 * @throws std::invalid_argument If terminal not found
 *
 * Associates an alias with a canonical name. The terminal can be
 * identified by either its canonical name or an existing alias.
 */
void TerminalGraph::addAliasToTerminal(const QString &name,
                                       const QString &alias)
{
    QMutexLocker locker(&m_mutex);                   // Lock for thread safety
    QString      canonical = getCanonicalName(name); // Resolve name
    if (!m_terminals.contains(canonical))
    {
        throw std::invalid_argument("Terminal not found: "
                                    + name.toStdString());
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
 * Returns all aliases for a given terminal, identified by either
 * its canonical name or an existing alias.
 */
QStringList TerminalGraph::getAliasesOfTerminal(const QString &name) const
{
    QMutexLocker locker(&m_mutex);                   // Lock for thread safety
    QString      canonical = getCanonicalName(name); // Resolve name
    return m_canonicalToAliases.value(canonical).values();
}

/**
 * @brief Adds a route between terminals
 * @param id Unique route identifier
 * @param start Starting terminal name
 * @param end Ending terminal name
 * @param mode Transportation mode
 * @param attrs Route attributes (optional)
 * @return Pair of canonical names for start and end terminals
 * @throws std::invalid_argument If terminal not found
 *
 * Adds a route with attributes to the graph. Default attributes
 * are merged with the provided attributes. Terminals can be
 * identified by either canonical names or aliases.
 */
QPair<QString, QString> TerminalGraph::addRoute(const QString     &id,
                                                const QString     &start,
                                                const QString     &end,
                                                TransportationMode mode,
                                                const QVariantMap &attrs)
{
    QMutexLocker locker(&m_mutex); // Lock for thread safety
    QString      startCanonical = getCanonicalName(start);
    QString      endCanonical   = getCanonicalName(end);

    // Validate terminals exist
    if (!m_terminals.contains(startCanonical)
        || !m_terminals.contains(endCanonical))
    {
        throw std::invalid_argument("Terminal not found");
    }

    // Merge default and provided attributes
    QVariantMap routeAttrs = m_defaultLinkAttributes;
    for (auto it = attrs.begin(); it != attrs.end(); ++it)
    {
        routeAttrs[it.key()] = it.value();
    }

    // Add edge to graph
    m_graph->addEdge(startCanonical, endCanonical, id, mode, routeAttrs);
    qDebug() << "Added route" << id << "from" << startCanonical << "to"
             << endCanonical;

    return {startCanonical, endCanonical};
}

/**
 * @brief Adds multiple routes between terminals simultaneously
 * @param routesList List of route configurations
 * @return QList of pairs with start and end terminal canonical names
 * @throws std::invalid_argument If required fields are missing or terminals not
 * found
 *
 * Adds multiple routes with attributes to the graph in one operation.
 * This method first validates all routes to ensure consistency, then
 * adds them to the graph. Each route configuration should include
 * route ID, start terminal, end terminal, mode, and optionally attributes.
 */
QList<QPair<QString, QString>>
TerminalGraph::addRoutes(const QList<QVariantMap> &routesList)
{
    QMutexLocker                   locker(&m_mutex); // Lock for thread safety
    QList<QPair<QString, QString>> addedRoutes;

    // First validate all routes
    for (const QVariantMap &routeData : routesList)
    {
        // Validate required fields
        if (!routeData.contains("route_id")
            || !routeData.contains("start_terminal")
            || !routeData.contains("end_terminal")
            || !routeData.contains("mode"))
        {
            throw std::invalid_argument("Missing required fields for route");
        }

        // Validate terminals exist
        QString start = routeData["start_terminal"].toString();
        QString end   = routeData["end_terminal"].toString();
        QString id    = routeData["route_id"].toString();

        QString startCanonical = getCanonicalName(start);
        QString endCanonical   = getCanonicalName(end);

        if (!m_terminals.contains(startCanonical)
            || !m_terminals.contains(endCanonical))
        {
            throw std::invalid_argument("Terminal not found for route ID: "
                                        + id.toStdString());
        }
    }

    // Add all routes
    for (const QVariantMap &routeData : routesList)
    {
        QString id    = routeData["route_id"].toString();
        QString start = routeData["start_terminal"].toString();
        QString end   = routeData["end_terminal"].toString();

        // Parse mode
        TransportationMode mode;
        QVariant           modeVar = routeData["mode"];

        if (modeVar.canConvert<int>())
        {
            mode = static_cast<TransportationMode>(modeVar.toInt());
        }
        else if (modeVar.canConvert<QString>())
        {
            mode = EnumUtils::stringToTransportationMode(modeVar.toString());
        }
        else
        {
            throw std::invalid_argument("Invalid mode parameter for route ID: "
                                        + id.toStdString());
        }

        // Get attributes
        QVariantMap attrs;
        if (routeData.contains("attributes")
            && routeData["attributes"].canConvert<QVariantMap>())
        {
            attrs = routeData["attributes"].toMap();
        }

        // Process canonical names
        QString startCanonical = getCanonicalName(start);
        QString endCanonical   = getCanonicalName(end);

        // Merge default and provided attributes
        QVariantMap routeAttrs = m_defaultLinkAttributes;
        for (auto it = attrs.begin(); it != attrs.end(); ++it)
        {
            routeAttrs[it.key()] = it.value();
        }

        // Add edge to graph
        m_graph->addEdge(startCanonical, endCanonical, id, mode, routeAttrs);

        addedRoutes.append(qMakePair(startCanonical, endCanonical));

        qDebug() << "Added route" << id << "from" << startCanonical << "to"
                 << endCanonical;
    }

    return addedRoutes;
}

/**
 * @brief Gets edge attributes by mode
 * @param start Starting terminal name
 * @param end Ending terminal name
 * @param mode Transportation mode
 * @return Edge attributes, or empty if not found
 *
 * Retrieves edge details for a specific route between two terminals
 * with the given transportation mode. Returns an empty map if no
 * matching edge is found or terminals don't exist.
 */
QVariantMap TerminalGraph::getEdgeByMode(const QString     &start,
                                         const QString     &end,
                                         TransportationMode mode) const
{
    QMutexLocker locker(&m_mutex); // Lock for thread safety
    QString      startCanonical = getCanonicalName(start);
    QString      endCanonical   = getCanonicalName(end);

    // Check terminal existence
    if (!m_terminals.contains(startCanonical)
        || !m_terminals.contains(endCanonical))
    {
        return QVariantMap(); // Return empty if not found
    }

    // Find edge by mode
    GraphImpl::InternalEdge *edge =
        m_graph->findEdge(startCanonical, endCanonical, mode);
    if (!edge)
    {
        return QVariantMap(); // No edge found
    }

    // Construct result map
    QVariantMap result = edge->attributes;
    result["mode"]     = static_cast<int>(edge->mode);
    result["route_id"] = edge->routeId;
    return result;
}

/**
 * @brief Gets terminals in a region
 * @param region Region name
 * @return List of terminal names
 *
 * Returns all terminals (canonical names) in the specified region.
 */
QStringList TerminalGraph::getTerminalsByRegion(const QString &region) const
{
    QMutexLocker locker(&m_mutex); // Lock for thread safety
    QStringList  result;
    for (const QString &node : m_graph->getNodes())
    {
        QVariant nodeRegion = m_graph->getNodeAttribute(node, "region");
        if (nodeRegion.isValid() && nodeRegion.toString() == region)
        {
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
 * Finds all routes connecting two regions. Each route in the returned
 * list is a QVariantMap containing details about start, end, route ID,
 * mode, and attributes.
 */
QList<QVariantMap>
TerminalGraph::getRoutesBetweenRegions(const QString &regionA,
                                       const QString &regionB) const
{
    QMutexLocker locker(&m_mutex); // Lock for thread safety

    // Get terminals for region A
    QStringList regionATerminals;
    for (const QString &node : m_graph->getNodes())
    {
        QVariant nodeRegion = m_graph->getNodeAttribute(node, "region");
        if (nodeRegion.isValid() && nodeRegion.toString() == regionA)
        {
            regionATerminals.append(node);
        }
    }

    // Get terminals for region B
    QStringList regionBTerminals;
    for (const QString &node : m_graph->getNodes())
    {
        QVariant nodeRegion = m_graph->getNodeAttribute(node, "region");
        if (nodeRegion.isValid() && nodeRegion.toString() == regionB)
        {
            regionBTerminals.append(node);
        }
    }

    // Collect routes between regions
    QList<QVariantMap> routes;
    for (const QString &termA : regionATerminals)
    {
        for (const QString &termB : regionBTerminals)
        {
            if (termA == termB)
            {
                continue; // Skip self-loops
            }

            // Get edges between terminals
            QList<GraphImpl::InternalEdge> edges =
                m_graph->getEdges(termA, termB);
            for (const GraphImpl::InternalEdge &edge : edges)
            {
                QVariantMap route;
                route["start"]      = termA;
                route["end"]        = termB;
                route["route_id"]   = edge.routeId;
                route["mode"]       = static_cast<int>(edge.mode);
                route["attributes"] = edge.attributes;
                routes.append(route);
            }
        }
    }

    // Log for debugging
    qDebug() << "Found" << routes.size() << "routes between" << regionA << "and"
             << regionB;
    return routes;
}

/**
 * @brief Connects terminals by interface modes
 *
 * Creates bidirectional routes between terminals that share common
 * interface modes. This is useful for quickly setting up a fully
 * connected network of terminals.
 *
 * The method first collects all potential routes under a mutex lock,
 * then adds them outside the lock to avoid deadlock with nested calls
 * to addRoute().
 */
void TerminalGraph::connectTerminalsByInterfaceModes()
{
    QMutexLocker locker(&m_mutex); // Lock for thread safety
    QStringList  terminals    = m_terminals.keys();
    int          routeCounter = 0;

    // Map to store routes for both directions
    QMap<QPair<QString, QString>, QList<QPair<TransportationMode, QString>>>
        routes;

    // Iterate all terminal pairs
    for (int i = 0; i < terminals.size(); ++i)
    {
        const QString &termA         = terminals[i];
        Terminal      *termAInstance = m_terminals[termA];
        auto           interfacesA   = termAInstance->getInterfaces();

        for (int j = i + 1; j < terminals.size(); ++j)
        {
            const QString &termB         = terminals[j];
            Terminal      *termBInstance = m_terminals[termB];
            auto           interfacesB   = termBInstance->getInterfaces();

            // Find common interfaces
            QSet<TerminalInterface> commonInterfaces;
            for (auto it = interfacesA.begin(); it != interfacesA.end(); ++it)
            {
                if (interfacesB.contains(it.key()))
                {
                    commonInterfaces.insert(it.key());
                }
            }

            // Collect common modes
            for (TerminalInterface intf : commonInterfaces)
            {
                QSet<TransportationMode> modesA      = interfacesA[intf];
                QSet<TransportationMode> modesB      = interfacesB[intf];
                QSet<TransportationMode> commonModes = modesA & modesB;

                if (!commonModes.isEmpty())
                {
                    QPair<QString, QString> pairAB(termA, termB);
                    QPair<QString, QString> pairBA(termB, termA);

                    // Add routes in both directions
                    for (TransportationMode mode : commonModes)
                    {
                        QString routeId =
                            QString("auto_%1").arg(++routeCounter);
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
    for (auto it = routes.begin(); it != routes.end(); ++it)
    {
        const QPair<QString, QString> &pair = it.key();
        for (const auto &modeRoute : it.value())
        {
            addRoute(modeRoute.second, pair.first, pair.second, modeRoute.first,
                     m_defaultLinkAttributes);
        }
    }
    qDebug() << "Connected terminals, added" << routeCounter << "routes";
}

/**
 * @brief Connects terminals in a region by mode
 * @param region Region name to connect within
 * @throws std::invalid_argument If too few terminals in region
 *
 * Adds bidirectional routes between all terminals in the specified region
 * that share common transportation modes. This is useful for quickly
 * setting up a fully connected network within a region.
 */
void TerminalGraph::connectTerminalsInRegionByMode(const QString &region)
{
    QMutexLocker locker(&m_mutex); // Lock for thread safety

    // Get terminals in region
    QStringList terminals;
    for (const QString &node : m_graph->getNodes())
    {
        QVariant nodeRegion = m_graph->getNodeAttribute(node, "region");
        if (nodeRegion.isValid() && nodeRegion.toString() == region)
        {
            terminals.append(node);
        }
    }

    // Validate terminal count
    if (terminals.size() < 2)
    {
        throw std::invalid_argument("Too few terminals in region");
    }

    int                                              routeCounter = 0;
    QMap<QString, QMap<TransportationMode, QString>> routesToAdd;

    // Collect routes in both directions
    for (int i = 0; i < terminals.size(); ++i)
    {
        const QString &termA         = terminals[i];
        Terminal      *termAInstance = m_terminals[termA];

        for (int j = i + 1; j < terminals.size(); ++j)
        {
            const QString &termB         = terminals[j];
            Terminal      *termBInstance = m_terminals[termB];

            // Gather modes for terminal A
            QSet<TransportationMode> modesA;
            for (auto it = termAInstance->getInterfaces().begin();
                 it != termAInstance->getInterfaces().end(); ++it)
            {
                modesA.unite(it.value());
            }

            // Gather modes for terminal B
            QSet<TransportationMode> modesB;
            for (auto it = termBInstance->getInterfaces().begin();
                 it != termBInstance->getInterfaces().end(); ++it)
            {
                modesB.unite(it.value());
            }

            // Find shared modes
            QSet<TransportationMode> sharedModes = modesA & modesB;

            // Store routes for A to B
            QMap<TransportationMode, QString> routesAB;
            for (TransportationMode mode : sharedModes)
            {
                QString routeId =
                    QString("region_%1_%2").arg(region).arg(++routeCounter);
                routesAB[mode] = routeId;
            }
            if (!routesAB.isEmpty())
            {
                routesToAdd[termA + "-" + termB] = routesAB;
            }

            // Store routes for B to A
            QMap<TransportationMode, QString> routesBA;
            for (TransportationMode mode : sharedModes)
            {
                QString routeId =
                    QString("region_%1_%2").arg(region).arg(++routeCounter);
                routesBA[mode] = routeId;
            }
            if (!routesBA.isEmpty())
            {
                routesToAdd[termB + "-" + termA] = routesBA;
            }
        }
    }

    // Unlock before adding routes
    locker.unlock();

    // Add routes outside lock
    for (int i = 0; i < terminals.size(); ++i)
    {
        for (int j = 0; j < terminals.size(); ++j)
        {
            if (i == j)
                continue; // Skip self-loops

            const QString &termA = terminals[i];
            const QString &termB = terminals[j];
            QString        key   = termA + "-" + termB;

            if (routesToAdd.contains(key))
            {
                for (auto it = routesToAdd[key].begin();
                     it != routesToAdd[key].end(); ++it)
                {
                    addRoute(it.value(), termA, termB, it.key(),
                             m_defaultLinkAttributes);
                    qDebug() << "Added route" << it.value() << "from" << termA
                             << "to" << termB;
                }
            }
        }
    }

    qDebug() << "Connected region" << region << "with" << routeCounter
             << "routes";
}

/**
 * @brief Connects regions by a specific mode
 * @param mode Transportation mode
 *
 * Links terminals across different regions bidirectionally using the
 * specified transportation mode. This creates inter-region connections
 * between all terminals that support the given mode.
 *
 * The method first collects all potential routes under a mutex lock,
 * then adds them outside the lock to avoid deadlock.
 */
void TerminalGraph::connectRegionsByMode(TransportationMode mode)
{
    // Struct for routes to add
    struct RouteToAdd
    {
        QString            routeId;
        QString            terminalA;
        QString            terminalB;
        TransportationMode mode;
    };
    QList<RouteToAdd> routesToAdd;

    // First phase: gather routes under lock
    {
        QMutexLocker locker(&m_mutex); // Lock for thread safety

        // Get terminals supporting the mode
        QStringList terminalsWithMode;
        for (auto it = m_terminals.constBegin(); it != m_terminals.constEnd();
             ++it)
        {
            Terminal *terminal     = it.value();
            bool      supportsMode = false;

            for (auto intfIt = terminal->getInterfaces().constBegin();
                 intfIt != terminal->getInterfaces().constEnd(); ++intfIt)
            {
                if (intfIt.value().contains(mode))
                {
                    supportsMode = true;
                    break;
                }
            }

            if (supportsMode)
            {
                terminalsWithMode.append(it.key());
            }
        }

        int routeCounter = 0;

        // Identify cross-region routes in both directions
        for (int i = 0; i < terminalsWithMode.size(); ++i)
        {
            const QString &terminalA = terminalsWithMode[i];
            QVariant regionA = m_graph->getNodeAttribute(terminalA, "region");

            for (int j = 0; j < terminalsWithMode.size(); ++j)
            {
                if (i == j)
                    continue; // Skip self-loops

                const QString &terminalB = terminalsWithMode[j];
                QVariant       regionB =
                    m_graph->getNodeAttribute(terminalB, "region");

                // Skip if same region or invalid
                if (!regionA.isValid() || !regionB.isValid()
                    || regionA.toString() == regionB.toString())
                {
                    continue;
                }

                // Add route from A to B
                QString routeId =
                    QString("inter_region_route_%1").arg(++routeCounter);
                routesToAdd.append({routeId, terminalA, terminalB, mode});
            }
        }

        qDebug() << "Identified" << routesToAdd.size()
                 << "routes to create between regions for mode"
                 << static_cast<int>(mode);
    }

    // Second phase: add routes outside lock
    for (const RouteToAdd &route : routesToAdd)
    {
        addRoute(route.routeId, route.terminalA, route.terminalB, route.mode,
                 m_defaultLinkAttributes);
        qDebug() << "Added route" << route.routeId << "from" << route.terminalA
                 << "to" << route.terminalB;
    }

    qDebug() << "Connected regions by mode" << static_cast<int>(mode)
             << ". Created" << routesToAdd.size() << "routes";
}

/**
 * @brief Changes weight of a route
 * @param start Starting terminal name
 * @param end Ending terminal name
 * @param mode Transportation mode
 * @param attrs New attributes to apply
 * @throws std::invalid_argument If terminal or route not found
 *
 * Updates route attributes thread-safely. The attributes determine
 * the cost of the route for path finding purposes. Only the specified
 * attributes are updated; other attributes remain unchanged.
 */
void TerminalGraph::changeRouteWeight(const QString &start, const QString &end,
                                      TransportationMode mode,
                                      const QVariantMap &attrs)
{
    QMutexLocker locker(&m_mutex); // Lock for thread safety
    QString      startCanonical = getCanonicalName(start);
    QString      endCanonical   = getCanonicalName(end);

    if (!m_terminals.contains(startCanonical)
        || !m_terminals.contains(endCanonical))
    {
        throw std::invalid_argument("Terminal not found");
    }

    GraphImpl::InternalEdge *edge =
        m_graph->findEdge(startCanonical, endCanonical, mode);
    if (!edge)
    {
        throw std::invalid_argument("Route not found");
    }

    // Update edge attributes
    for (auto it = attrs.begin(); it != attrs.end(); ++it)
    {
        edge->attributes[it.key()] = it.value();
    }
    qDebug() << "Updated route weight from" << startCanonical << "to"
             << endCanonical;
}

/**
 * @brief Gets a terminal by name
 * @param name Terminal name or alias
 * @return Pointer to terminal
 * @throws std::invalid_argument If terminal not found
 *
 * Retrieves a terminal by its name or alias. Thread-safety is
 * ensured by locking during the operation.
 */
Terminal *TerminalGraph::getTerminal(const QString &name) const
{
    QMutexLocker locker(&m_mutex); // Lock for thread safety
    QString      canonical = getCanonicalName(name);
    if (!m_terminals.contains(canonical))
    {
        throw std::invalid_argument("Terminal not found: "
                                    + name.toStdString());
    }
    return m_terminals[canonical];
}

/**
 * @brief Checks if a terminal exists
 * @param name Terminal name or alias
 * @return True if exists, false otherwise
 *
 * Checks if a terminal with the given name or alias exists in the graph.
 */
bool TerminalGraph::terminalExists(const QString &name) const
{
    QMutexLocker locker(&m_mutex); // Lock for thread safety
    QString      canonical = getCanonicalName(name);
    return m_terminals.contains(canonical);
}

/**
 * @brief Removes a terminal from the graph
 * @param name Terminal name or alias
 * @return True if removed, false if not found
 *
 * Removes a terminal and all its associated routes from the graph.
 * All aliases associated with the terminal are also removed.
 */
bool TerminalGraph::removeTerminal(const QString &name)
{
    QMutexLocker locker(&m_mutex); // Lock for thread safety
    QString      canonical = getCanonicalName(name);
    if (!m_terminals.contains(canonical))
    {
        return false; // Terminal not found
    }

    // Remove aliases
    QSet<QString> aliases = m_canonicalToAliases.value(canonical);
    for (const QString &alias : aliases)
    {
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
 *
 * Returns the number of terminals in the graph.
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
 *
 * Returns a map of all terminal canonical names to their alias lists.
 * If includeAliases is false, the alias lists will be empty.
 */
QMap<QString, QStringList>
TerminalGraph::getAllTerminalNames(bool includeAliases) const
{
    QMutexLocker               locker(&m_mutex); // Lock for thread safety
    QMap<QString, QStringList> result;

    if (includeAliases)
    {
        for (auto it = m_canonicalToAliases.begin();
             it != m_canonicalToAliases.end(); ++it)
        {
            result[it.key()] = it.value().values();
        }
    }
    else
    {
        for (auto it = m_terminals.begin(); it != m_terminals.end(); ++it)
        {
            result[it.key()] = QStringList();
        }
    }
    return result;
}

/**
 * @brief Clears the graph
 *
 * Deletes all terminals and resets graph state. All terminals,
 * routes, aliases, and graph structures are cleared.
 */
void TerminalGraph::clear()
{
    QMutexLocker locker(&m_mutex); // Lock for thread safety
    for (auto it = m_terminals.begin(); it != m_terminals.end(); ++it)
    {
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
 * @param name Terminal name or alias, empty for all
 * @return Status details
 * @throws std::invalid_argument If specified terminal not found
 *
 * Returns status info for one or all terminals. The status includes
 * container count, available capacity, max capacity, region, and aliases.
 *
 * This method is carefully implemented to avoid deadlock by releasing
 * the mutex when calling terminal methods that might acquire their own locks.
 */
QVariantMap TerminalGraph::getTerminalStatus(const QString &name) const
{
    QMutexLocker locker(&m_mutex); // Lock for thread safety
    if (!name.isEmpty())
    {
        QString canonical = getCanonicalName(name);
        if (!m_terminals.contains(canonical))
        {
            throw std::invalid_argument("Terminal not found");
        }

        Terminal *term   = m_terminals[canonical];
        QVariant  region = m_graph->getNodeAttribute(canonical, "region");

        // Unlock before calling terminal methods
        locker.unlock();
        QVariantMap status;
        status["container_count"]    = term->getContainerCount();
        status["available_capacity"] = term->getAvailableCapacity();
        status["max_capacity"]       = term->getMaxCapacity();

        // Relock to access graph data
        locker.relock();
        status["region"]  = region;
        status["aliases"] = QVariant(m_canonicalToAliases[canonical].values());
        return status;
    }
    else
    {
        // Copy data while locked
        QHash<QString, Terminal *>    termsCopy   = m_terminals;
        QHash<QString, QSet<QString>> aliasesCopy = m_canonicalToAliases;
        QMap<QString, QVariant>       regions;
        for (auto it = termsCopy.begin(); it != termsCopy.end(); ++it)
        {
            regions[it.key()] = m_graph->getNodeAttribute(it.key(), "region");
        }

        // Unlock for terminal queries
        locker.unlock();
        QVariantMap result;
        for (auto it = termsCopy.begin(); it != termsCopy.end(); ++it)
        {
            QString   canonical = it.key();
            Terminal *term      = it.value();

            QVariantMap status;
            status["container_count"]    = term->getContainerCount();
            status["available_capacity"] = term->getAvailableCapacity();
            status["max_capacity"]       = term->getMaxCapacity();
            status["region"]             = regions[canonical];
            status["aliases"] = QVariant(aliasesCopy[canonical].values());
            result[canonical] = status;
        }
        return result;
    }
}

/**
 * @brief Gets the terminal directory path
 * @return Directory path
 *
 * Returns the directory path used for terminal storage.
 * This method doesn't require locking since the path is immutable.
 */
const QString &TerminalGraph::getPathToTerminalsDirectory() const
{
    return m_pathToTerminalsDirectory; // No lock needed for const
}

/**
 * @brief Finds the shortest path between terminals
 * @param start Starting terminal name or alias
 * @param end Ending terminal name or alias
 * @param mode Transportation mode (optional)
 * @return List of path segments
 * @throws std::invalid_argument If terminals not found
 * @throws std::runtime_error If no path exists
 *
 * Uses Dijkstra's algorithm to find the shortest path between terminals.
 * If a specific mode is provided, only edges with that mode are considered.
 * Otherwise, all edges are considered regardless of mode.
 *
 * The method computes path costs based on edge attributes and terminal
 * handling time and cost.
 */
QList<PathSegment>
TerminalGraph::findShortestPath(const QString &start, const QString &end,
                                TransportationMode mode) const
{
    QMutexLocker locker(&m_mutex);
    QString      startCanonical = getCanonicalName(start);
    QString      endCanonical   = getCanonicalName(end);

    if (!m_terminals.contains(startCanonical)
        || !m_terminals.contains(endCanonical))
    {
        throw std::invalid_argument("Terminal not found");
    }

    // Setup for Dijkstra's algorithm
    QHash<QString, double>             distance;
    QHash<QString, QString>            previous;
    QHash<QString, TransportationMode> edgeMode;
    QHash<QString, QVariantMap>        edgeAttrs;

    // Initialize distances
    QStringList nodes = m_graph->getNodes();
    for (const QString &node : nodes)
    {
        distance[node] = std::numeric_limits<double>::infinity();
        previous[node] = QString();
    }
    distance[startCanonical] = 0.0;

    // Use a priority queue for O(log n) node selection
    // Pair format: <distance, node>
    typedef QPair<double, QString> NodeDist;
    auto cmp = [](const NodeDist &a, const NodeDist &b) {
        return a.first > b.first;
    };
    std::priority_queue<NodeDist, std::vector<NodeDist>, decltype(cmp)> pq(cmp);

    pq.push({0.0, startCanonical});
    QSet<QString> processed;

    while (!pq.empty())
    {
        NodeDist current = pq.top();
        pq.pop();

        QString currentNode = current.second;
        double  currentDist = current.first;

        // Skip if already processed (may be duplicates in queue with different
        // distances)
        if (processed.contains(currentNode))
        {
            continue;
        }

        // We can terminate when we reach the destination
        if (currentNode == endCanonical)
        {
            break;
        }

        processed.insert(currentNode);

        // Process all adjacent nodes
        for (const GraphImpl::InternalEdge &edge :
             m_graph->adjacencyList[currentNode])
        {
            // Skip if mode doesn't match and a specific mode is requested
            if (mode != TransportationMode::Any && edge.mode != mode)
            {
                continue;
            }

            QString neighbor = edge.to;
            if (processed.contains(neighbor))
            {
                continue;
            }

            // Calculate terminal costs
            Terminal *currTerm = m_terminals[currentNode];
            Terminal *nextTerm = m_terminals[neighbor];
            double    delay    = currTerm->estimateContainerHandlingTime()
                           + nextTerm->estimateContainerHandlingTime();
            double cost = currTerm->estimateContainerCost()
                          + nextTerm->estimateContainerCost();

            // Prepare parameters for cost function
            QVariantMap params       = edge.attributes;
            params["terminal_delay"] = delay;
            params["terminal_cost"]  = cost;

            // Compute total cost using the cost function
            double edgeCost =
                computeCost(params, m_costFunctionParametersWeights, edge.mode);
            double newDist = currentDist + edgeCost;

            if (newDist < distance[neighbor])
            {
                distance[neighbor]  = newDist;
                previous[neighbor]  = currentNode;
                edgeMode[neighbor]  = edge.mode;
                edgeAttrs[neighbor] = edge.attributes;

                // Add to queue with new distance
                pq.push({newDist, neighbor});
            }
        }
    }

    // Reconstruct the path
    QList<PathSegment> path;

    // Check if path exists
    if (previous[endCanonical].isEmpty() && startCanonical != endCanonical)
    {
        throw std::runtime_error("No path found");
    }

    // Build path from end to start, then reverse
    QString current = endCanonical;
    while (!previous[current].isEmpty())
    {
        PathSegment seg;
        seg.from           = previous[current];
        seg.to             = current;
        seg.mode           = edgeMode[current];
        seg.weight         = distance[current] - distance[seg.from];
        seg.fromTerminalId = seg.from;
        seg.toTerminalId   = seg.to;
        seg.attributes     = edgeAttrs[current];

        path.prepend(seg);
        current = seg.from;
    }

    return path;
}

/**
 * @brief Finds shortest path within regions
 * @param start Starting terminal name or alias
 * @param end Ending terminal name or alias
 * @param regions Allowed regions
 * @param mode Transportation mode (optional)
 * @return List of path segments
 * @throws std::invalid_argument If terminals not found or not in allowed
 * regions
 * @throws std::runtime_error If no path exists within allowed regions
 *
 * Finds the shortest path between terminals that stays within the
 * specified regions. Creates a subgraph with only nodes in the allowed
 * regions, then applies Dijkstra's algorithm on this subgraph.
 */
QList<PathSegment> TerminalGraph::findShortestPathWithinRegions(
    const QString &start, const QString &end, const QStringList &regions,
    TransportationMode mode) const
{
    QMutexLocker locker(&m_mutex); // Lock for thread safety
    QString      startCanonical = getCanonicalName(start);
    QString      endCanonical   = getCanonicalName(end);

    if (!m_terminals.contains(startCanonical)
        || !m_terminals.contains(endCanonical))
    {
        throw std::invalid_argument("Terminal not found");
    }

    // Validate regions
    QVariant startRegion = m_graph->getNodeAttribute(startCanonical, "region");
    QVariant endRegion   = m_graph->getNodeAttribute(endCanonical, "region");
    if (startRegion.isValid() && !regions.contains(startRegion.toString()))
    {
        throw std::invalid_argument("Start not in allowed regions");
    }
    if (endRegion.isValid() && !regions.contains(endRegion.toString()))
    {
        throw std::invalid_argument("End not in allowed regions");
    }

    // Create subgraph for allowed regions
    GraphImpl subgraph;
    for (const QString &node : m_graph->getNodes())
    {
        QVariant region = m_graph->getNodeAttribute(node, "region");
        if (!region.isValid() || regions.contains(region.toString()))
        {
            subgraph.adjacencyList[node] = QList<GraphImpl::InternalEdge>();
            if (m_graph->nodeAttributes.contains(node))
            {
                subgraph.nodeAttributes[node] = m_graph->nodeAttributes[node];
            }
        }
    }

    for (auto it = subgraph.adjacencyList.begin();
         it != subgraph.adjacencyList.end(); ++it)
    {
        if (m_graph->adjacencyList.contains(it.key()))
        {
            for (const GraphImpl::InternalEdge &edge :
                 m_graph->adjacencyList[it.key()])
            {
                if (subgraph.adjacencyList.contains(edge.to))
                {
                    it.value().append(edge);
                }
            }
        }
    }

    // Dijkstra's on subgraph
    QHash<QString, double>             distance;
    QHash<QString, QString>            previous;
    QHash<QString, TransportationMode> edgeMode;
    QHash<QString, QVariantMap>        edgeAttrs;

    QStringList nodes = subgraph.getNodes();
    for (const QString &node : nodes)
    {
        distance[node] = std::numeric_limits<double>::infinity();
        previous[node] = QString();
    }
    distance[startCanonical] = 0.0;

    QSet<QString> unvisited(nodes.begin(), nodes.end());
    while (!unvisited.isEmpty())
    {
        QString current;
        double  minDist = std::numeric_limits<double>::infinity();
        for (const QString &node : unvisited)
        {
            if (distance[node] < minDist)
            {
                minDist = distance[node];
                current = node;
            }
        }

        if (current.isEmpty()
            || minDist == std::numeric_limits<double>::infinity())
        {
            break;
        }

        if (current == endCanonical)
        {
            break;
        }

        unvisited.remove(current);
        const QList<GraphImpl::InternalEdge> &edges =
            subgraph.adjacencyList[current];
        for (const GraphImpl::InternalEdge &edge : edges)
        {
            // Skip if mode doesn't match and a specific mode is requested
            if (mode != TransportationMode::Any && edge.mode != mode)
            {
                continue;
            }

            QString neighbor = edge.to;
            if (!unvisited.contains(neighbor))
            {
                continue;
            }

            Terminal *currTerm = m_terminals[current];
            Terminal *nextTerm = m_terminals[neighbor];
            double    delay    = currTerm->estimateContainerHandlingTime()
                           + nextTerm->estimateContainerHandlingTime();
            double cost = currTerm->estimateContainerCost()
                          + nextTerm->estimateContainerCost();

            QVariantMap params       = edge.attributes;
            params["terminal_delay"] = delay;
            params["terminal_cost"]  = cost;

            double totalCost =
                computeCost(params, m_costFunctionParametersWeights, edge.mode);
            double alt = distance[current] + totalCost;
            if (alt < distance[neighbor])
            {
                distance[neighbor]  = alt;
                previous[neighbor]  = current;
                edgeMode[neighbor]  = edge.mode;
                edgeAttrs[neighbor] = edge.attributes;
            }
        }
    }

    // Reconstruct path
    QList<PathSegment> path;
    if (previous[endCanonical].isEmpty() && startCanonical != endCanonical)
    {
        throw std::runtime_error("No path in regions");
    }

    QString current = endCanonical;
    while (!previous[current].isEmpty())
    {
        PathSegment seg;
        seg.from           = previous[current];
        seg.to             = current;
        seg.mode           = edgeMode[current];
        seg.weight         = distance[current] - distance[seg.from];
        seg.fromTerminalId = seg.from;
        seg.toTerminalId   = seg.to;
        seg.attributes     = edgeAttrs[current];
        path.prepend(seg);
        current = seg.from;
    }
    return path;
}

/**
 * @brief Finds top N shortest paths
 * @param start Starting terminal name or alias
 * @param end Ending terminal name or alias
 * @param n Number of paths to find (default: 5)
 * @param mode Transportation mode (default: Any)
 * @param skipDelays Skip same-mode delays if true (default: true)
 * @return List of paths
 *
 * Finds the top N shortest paths between two terminals. The algorithm
 * has several phases:
 * 1. Initialization - Prepare context data for path finding
 * 2. Direct Paths - Find direct routes between terminals
 * 3. Shortest Path - Find the shortest overall path if needed
 * 4. Additional Paths - Find alternative paths using either:
 *    a. Edge exclusion - For multi-segment paths
 *    b. Intermediate terminals - For single-segment paths
 * 5. Finalization - Sort and finalize the paths
 *
 * This method uses a custom approach rather than Yen's k-shortest paths
 * algorithm to better handle transportation networks with specific
 * constraints and optimization goals.
 */
QList<Path> TerminalGraph::findTopNShortestPaths(const QString &start,
                                                 const QString &end, int n,
                                                 TransportationMode mode,
                                                 bool skipDelays) const
{
    // --- Initialization Phase ---
    // Return early for invalid input
    if (n <= 0)
    {
        qDebug() << "Invalid request: n must be positive";
        return QList<Path>();
    }

    // Initialize with locked graph access
    PathFindingContext context =
        initializePathFindingContext(start, end, mode, skipDelays);
    if (!context.isValid)
    {
        return QList<Path>(); // Return empty if context invalid (terminals not
                              // found)
    }

    // --- Direct Paths Collection Phase ---
    QList<Path> result = findDirectPaths(context, n);
    if (result.size() >= n)
    {
        return sortAndFinalizePaths(
            result, n); // Return if we already have enough paths
    }

    // --- Shortest Path Finding Phase ---
    findAndAddShortestPath(context, result);
    if (result.size() >= n)
    {
        return sortAndFinalizePaths(result, n);
    }

    // --- Additional Paths Finding Phase ---
    if (hasMultiSegmentPaths(result))
    {
        findAdditionalPathsByEdgeExclusion(context, result, n);
    }
    else
    {
        findAdditionalPathsViaIntermediates(context, result, n);
    }

    // --- Finalization Phase ---
    return sortAndFinalizePaths(result, n);
}

/**
 * @brief Initialize path finding context with proper locking
 * @param start Starting terminal name or alias
 * @param end Ending terminal name or alias
 * @param mode Transportation mode
 * @param skipDelays Whether to skip same-mode delays
 * @return Initialized PathFindingContext
 *
 * Creates and initializes a PathFindingContext object with the given
 * parameters. This helper method safely extracts and copies necessary data from
 * the graph under a mutex lock, allowing subsequent path-finding operations to
 * proceed without holding the lock.
 */
PathFindingContext TerminalGraph::initializePathFindingContext(
    const QString &start, const QString &end, TransportationMode mode,
    bool skipDelays) const
{
    PathFindingContext context;
    context.mode       = mode;
    context.skipDelays = skipDelays;
    context.isValid    = true;

    QMutexLocker locker(&m_mutex);

    // Resolve canonical names
    context.startCanonical = getCanonicalName(start);
    context.endCanonical   = getCanonicalName(end);

    // Validate terminal existence
    if (!m_terminals.contains(context.startCanonical)
        || !m_terminals.contains(context.endCanonical))
    {
        qWarning() << "Terminal not found: start=" << context.startCanonical
                   << " end=" << context.endCanonical;
        context.isValid = false;
        return context;
    }

    // Create copy of terminal pointers to avoid locking during computation
    context.termPointers = m_terminals;

    return context;
}

/**
 * @brief Generate a deterministic path signature
 * @param segments Path segments
 * @return String signature uniquely identifying the path
 *
 * Generates a unique string signature for a path based on its segments.
 * This is used to detect duplicate paths during the path finding process.
 * The signature includes the sequence of terminals and transportation modes.
 */
QString
TerminalGraph::generatePathSignature(const QList<PathSegment> &segments) const
{
    QString signature;
    if (!segments.isEmpty())
    {
        signature = segments.first().from;
        for (const PathSegment &seg : segments)
        {
            signature += "->" + seg.to + ":"
                         + QString::number(static_cast<int>(seg.mode));
        }
    }
    return signature;
}

/**
 * @brief Helper to get terminal info with caching
 * @param context Path finding context
 * @param name Terminal name
 * @return Reference to cached terminal info
 *
 * Retrieves or calculates terminal information (handling time and cost)
 * for a specific terminal, using a cache to avoid redundant calculations.
 * This improves performance during path finding operations.
 */
const PathFindingContext::TermInfo &
TerminalGraph::getTermInfo(PathFindingContext &context,
                           const QString      &name) const
{

    auto it = context.termInfoCache.find(name);
    if (it == context.termInfoCache.end())
    {
        Terminal *term = context.termPointers[name];
        it             = context.termInfoCache.insert(
            name, {term->estimateContainerHandlingTime(),
                               term->estimateContainerCost()});
    }
    return it.value();
}

/**
 * @brief Determine if terminal costs should be skipped
 * @param context Path finding context
 * @param terminalIndex Index of terminal in path
 * @param segments Path segments
 * @return True if costs should be skipped
 *
 * Determines whether terminal handling costs should be skipped for a
 * specific terminal in a path. Costs are skipped for consecutive
 * segments with the same transportation mode if skipDelays is enabled.
 * This simulates seamless transfers between segments using the same mode.
 */
bool TerminalGraph::shouldSkipCosts(PathFindingContext       &context,
                                    int                       terminalIndex,
                                    const QList<PathSegment> &segments) const
{

    // Skip costs for the first and last terminals (origin and
    // destination)
    if (terminalIndex == 0 || terminalIndex >= segments.size())
        return true;

    // Check if there is a mode change at this terminal
    bool modeChanges =
        segments[terminalIndex - 1].mode != segments[terminalIndex].mode;

    // If skipDelays is true: skip terminal costs only when modes match
    // If skipDelays is false: do not skip terminal costs
    return context.skipDelays ? !modeChanges : false;
}

/**
 * @brief Build complete path details
 * @param context Path finding context
 * @param segments Path segments
 * @param pathId Path identifier
 * @return Complete path with details
 *
 * Builds a complete Path object from a list of path segments, including
 * calculating terminal information, costs, and other details. The method
 * accounts for skipping costs at terminals with same-mode transfers if
 * skipDelays is enabled.
 */
Path TerminalGraph::buildPathDetails(PathFindingContext       &context,
                                     const QList<PathSegment> &segments,
                                     int                       pathId) const
{
    double             totalEdgeCosts     = 0.0;
    double             totalTerminalCosts = 0.0;
    QList<QVariantMap> terminalsInPath;

    // Process segments
    for (int i = 0; i < segments.size(); i++)
    {
        const PathSegment &seg = segments[i];

        // For edge costs, exclude the terminal costs that were included during
        // pathfinding We'll recalculate terminal costs separately with the
        // correct logic
        QVariantMap edgeAttrs = seg.attributes;
        edgeAttrs.remove("terminal_delay");
        edgeAttrs.remove("terminal_cost");

        // Recalculate the edge weight without terminal costs
        double edgeWeight =
            computeCost(edgeAttrs, m_costFunctionParametersWeights, seg.mode);
        totalEdgeCosts += edgeWeight;

        // Add start terminal info (only for first segment)
        if (i == 0)
        {
            const PathFindingContext::TermInfo &startInfo =
                getTermInfo(context, seg.from);
            QVariantMap startTerm;
            startTerm["terminal"]      = seg.from;
            startTerm["handling_time"] = startInfo.handlingTime;
            startTerm["cost"]          = startInfo.cost;

            // Apply the skip logic for origin terminal
            bool skipOrigin            = context.skipDelays;
            startTerm["costs_skipped"] = skipOrigin;

            terminalsInPath.append(startTerm);

            if (!skipOrigin)
            {
                totalTerminalCosts += startInfo.cost;
            }
        }

        // Add destination terminal info
        const PathFindingContext::TermInfo &termInfo =
            getTermInfo(context, seg.to);

        // Add terminal info to path
        QVariantMap termEntry;
        termEntry["terminal"]      = seg.to;
        termEntry["handling_time"] = termInfo.handlingTime;
        termEntry["cost"]          = termInfo.cost;

        // Terminal index is i+1 (0 is the start terminal)
        bool skip                  = shouldSkipCosts(context, i + 1, segments);
        termEntry["costs_skipped"] = skip;
        terminalsInPath.append(termEntry);

        // Add terminal cost if not skipped
        if (!skip)
        {
            totalTerminalCosts += termInfo.cost;
        }
    }

    // Build complete path
    Path path;
    path.pathId             = pathId;
    path.totalEdgeCosts     = totalEdgeCosts;
    path.totalTerminalCosts = totalTerminalCosts;
    path.totalPathCost      = totalEdgeCosts + totalTerminalCosts;
    path.terminalsInPath    = terminalsInPath;
    path.segments           = segments;

    return path;
}

/**
 * @brief Find direct paths between terminals
 * @param context Path finding context
 * @param maxPaths Maximum number of paths to find
 * @return List of direct paths
 *
 * Finds direct paths (single-segment paths) between the start and end
 * terminals. These are routes that connect the terminals directly without
 * intermediate stops. The method sorts routes by cost for deterministic
 * behavior and filters out duplicate paths.
 */
QList<Path> TerminalGraph::findDirectPaths(PathFindingContext &context,
                                           int                 maxPaths) const
{

    QList<Path>  result;
    QMutexLocker locker(&m_mutex);

    // Collect direct edges between terminals
    QList<GraphImpl::InternalEdge> directEdges;
    if (context.mode == TransportationMode::Any)
    {
        directEdges =
            m_graph->getEdges(context.startCanonical, context.endCanonical);
    }
    else
    {
        GraphImpl::InternalEdge *specificEdge = m_graph->findEdge(
            context.startCanonical, context.endCanonical, context.mode);
        if (specificEdge)
        {
            directEdges.append(*specificEdge);
        }
    }
    locker.unlock();

    if (directEdges.isEmpty())
    {
        return result;
    }

    qDebug() << "Found" << directEdges.size() << "direct routes between"
             << context.startCanonical << "and" << context.endCanonical;

    // Sort direct edges by cost for deterministic behavior
    std::sort(
        directEdges.begin(), directEdges.end(),
        [&](const GraphImpl::InternalEdge &a,
            const GraphImpl::InternalEdge &b) {
            // Prepare parameters for cost function
            QVariantMap paramsA = a.attributes;
            QVariantMap paramsB = b.attributes;

            // Get terminal costs (same for both paths)
            Terminal *startTerm = context.termPointers[context.startCanonical];
            Terminal *endTerm   = context.termPointers[context.endCanonical];
            double    delay     = startTerm->estimateContainerHandlingTime()
                           + endTerm->estimateContainerHandlingTime();
            double cost = startTerm->estimateContainerCost()
                          + endTerm->estimateContainerCost();

            paramsA["terminal_delay"] = delay;
            paramsA["terminal_cost"]  = cost;
            paramsB["terminal_delay"] = delay;
            paramsB["terminal_cost"]  = cost;

            // Compute costs
            double costA =
                computeCost(paramsA, m_costFunctionParametersWeights, a.mode);
            double costB =
                computeCost(paramsB, m_costFunctionParametersWeights, b.mode);

            return costA < costB;
        });

    // Add direct paths to results
    for (const GraphImpl::InternalEdge &edge : directEdges)
    {
        // Skip if we've already found enough paths
        if (result.size() >= maxPaths)
        {
            break;
        }

        // Create a path segment
        PathSegment segment;
        segment.from           = context.startCanonical;
        segment.to             = context.endCanonical;
        segment.mode           = edge.mode;
        segment.fromTerminalId = context.startCanonical;
        segment.toTerminalId   = context.endCanonical;
        segment.attributes     = edge.attributes;

        // Calculate edge weight
        Terminal *startTerm = context.termPointers[context.startCanonical];
        Terminal *endTerm   = context.termPointers[context.endCanonical];
        double    delay     = startTerm->estimateContainerHandlingTime()
                       + endTerm->estimateContainerHandlingTime();
        double cost = startTerm->estimateContainerCost()
                      + endTerm->estimateContainerCost();

        QVariantMap params       = edge.attributes;
        params["terminal_delay"] = delay;
        params["terminal_cost"]  = cost;
        segment.weight =
            computeCost(params, m_costFunctionParametersWeights, edge.mode);

        // Create a new path
        QList<PathSegment> directPath;
        directPath.append(segment);

        // Check if we've already found this path
        QString pathSignature = generatePathSignature(directPath);
        if (context.foundPathSignatures.contains(pathSignature))
        {
            continue; // Skip duplicate
        }

        // Add to results
        context.foundPathSignatures.insert(pathSignature);
        Path path = buildPathDetails(context, directPath, result.size() + 1);
        result.append(path);

        qDebug() << "Added direct path with mode" << static_cast<int>(edge.mode)
                 << "and cost" << path.totalPathCost;
    }

    return result;
}

/**
 * @brief Find and add the shortest path to the results
 * @param context Path finding context
 * @param result List of paths to append to
 *
 * Finds the shortest path between the start and end terminals and adds it
 * to the result list if it's not already there. This method excludes direct
 * paths already found to ensure diversity in the results.
 */
void TerminalGraph::findAndAddShortestPath(PathFindingContext &context,
                                           QList<Path>        &result) const
{

    // Create exclusions for direct paths we've already found
    QSet<EdgeIdentifier> directPathsToExclude;
    for (const Path &path : result)
    {
        if (path.segments.size() == 1)
        {
            const PathSegment &seg        = path.segments.first();
            EdgeIdentifier     directEdge = {seg.from, seg.to, seg.mode};
            directPathsToExclude.insert(directEdge);
        }
    }

    // Find shortest path
    QList<PathSegment> shortestPath;
    try
    {
        if (directPathsToExclude.isEmpty())
        {
            shortestPath = findShortestPath(context.startCanonical,
                                            context.endCanonical, context.mode);
        }
        else
        {
            shortestPath = findShortestPathWithExclusions(
                context.startCanonical, context.endCanonical, context.mode,
                directPathsToExclude);
        }

        // Check if this path is already in our results
        QString shortestPathSignature = generatePathSignature(shortestPath);
        if (!context.foundPathSignatures.contains(shortestPathSignature))
        {
            context.foundPathSignatures.insert(shortestPathSignature);
            Path path =
                buildPathDetails(context, shortestPath, result.size() + 1);
            result.append(path);

            qDebug() << "Added shortest path with" << shortestPath.size()
                     << "segments and cost" << path.totalPathCost;
        }
    }
    catch (const std::exception &e)
    {
        qWarning() << "No additional path found:" << e.what();
    }
}

/**
 * @brief Check if the result has any multi-segment paths
 * @param paths List of paths
 * @return True if any path has multiple segments
 *
 * Checks if the list of paths contains any multi-segment paths.
 * This is used to determine which strategy to use for finding
 * additional paths in the findTopNShortestPaths method.
 */
bool TerminalGraph::hasMultiSegmentPaths(const QList<Path> &paths) const
{
    for (const Path &path : paths)
    {
        if (path.segments.size() > 1)
        {
            return true;
        }
    }
    return false;
}

/**
 * @brief Find additional paths using edge exclusion method
 * @param context Path finding context
 * @param result List of paths to append to
 * @param n Target number of paths
 *
 * Finds additional paths by excluding edges from existing paths and
 * finding alternative routes. This method is used when multi-segment
 * paths exist, as it's more effective at finding diverse alternatives
 * in complex networks.
 *
 * The algorithm tries single-edge exclusions first, then moves to
 * edge pairs if needed to find more diverse paths.
 */
void TerminalGraph::findAdditionalPathsByEdgeExclusion(
    PathFindingContext &context, QList<Path> &result, int n) const
{

    // Create a set of key edges from all non-direct paths
    QSet<QPair<QString, QPair<QString, int>>> keyEdges; // From, To, Mode
    for (const Path &path : result)
    {
        if (path.segments.size() > 1)
        { // Only consider multi-segment paths
            for (const PathSegment &segment : path.segments)
            {
                keyEdges.insert(qMakePair(
                    segment.from,
                    qMakePair(segment.to, static_cast<int>(segment.mode))));
            }
        }
    }

    // Convert to a deterministically ordered list
    QList<QPair<QString, QPair<QString, int>>> orderedEdges = keyEdges.values();
    std::sort(orderedEdges.begin(), orderedEdges.end());

    // Try removing each key edge and find alternative paths
    for (int i = result.size(); i < n; i++)
    {
        bool foundNewPath = false;

        // Try single-edge exclusion
        for (const auto &edgeTriple : orderedEdges)
        {
            QString            from = edgeTriple.first;
            QString            to   = edgeTriple.second.first;
            TransportationMode edgeMode =
                static_cast<TransportationMode>(edgeTriple.second.second);

            QSet<EdgeIdentifier> edgesToExclude;
            EdgeIdentifier       edgeId = {from, to, edgeMode};
            edgesToExclude.insert(edgeId);

            try
            {
                QList<PathSegment> alternativePath =
                    findShortestPathWithExclusions(
                        context.startCanonical, context.endCanonical,
                        context.mode, edgesToExclude);

                QString pathSignature = generatePathSignature(alternativePath);
                if (context.foundPathSignatures.contains(pathSignature))
                {
                    continue; // Skip duplicate path
                }

                // Add new path
                Path path = buildPathDetails(context, alternativePath,
                                             result.size() + 1);
                result.append(path);
                context.foundPathSignatures.insert(pathSignature);
                foundNewPath = true;

                qDebug() << "Found new path by excluding edge" << from << "->"
                         << to << "with mode" << static_cast<int>(edgeMode);

                // Add any new edges for future exclusion
                for (const PathSegment &segment : alternativePath)
                {
                    QPair<QString, QPair<QString, int>> newEdge = qMakePair(
                        segment.from,
                        qMakePair(segment.to, static_cast<int>(segment.mode)));

                    if (!keyEdges.contains(newEdge))
                    {
                        keyEdges.insert(newEdge);
                        orderedEdges.append(newEdge);
                    }
                }

                // Resort the edges list
                std::sort(orderedEdges.begin(), orderedEdges.end());
                break; // Move to next iteration
            }
            catch (const std::exception &)
            {
                continue; // Try next edge
            }
        }

        // If single-edge exclusion failed, try edge pairs
        if (!foundNewPath && i < n - 1 && orderedEdges.size() >= 2)
        {
            qDebug() << "Trying edge combinations...";

            for (int j = 0; j < orderedEdges.size(); j++)
            {
                if (foundNewPath)
                    break;

                for (int k = j + 1; k < orderedEdges.size(); k++)
                {
                    auto edge1 = orderedEdges[j];
                    auto edge2 = orderedEdges[k];

                    QSet<EdgeIdentifier> edgesToExclude;
                    EdgeIdentifier       edgeId1 = {
                        edge1.first, edge1.second.first,
                        static_cast<TransportationMode>(edge1.second.second)};

                    EdgeIdentifier edgeId2 = {
                        edge2.first, edge2.second.first,
                        static_cast<TransportationMode>(edge2.second.second)};

                    edgesToExclude.insert(edgeId1);
                    edgesToExclude.insert(edgeId2);

                    try
                    {
                        QList<PathSegment> alternativePath =
                            findShortestPathWithExclusions(
                                context.startCanonical, context.endCanonical,
                                context.mode, edgesToExclude);

                        QString pathSignature =
                            generatePathSignature(alternativePath);
                        if (context.foundPathSignatures.contains(pathSignature))
                        {
                            continue; // Skip duplicate path
                        }

                        Path path = buildPathDetails(context, alternativePath,
                                                     result.size() + 1);
                        result.append(path);
                        context.foundPathSignatures.insert(pathSignature);
                        foundNewPath = true;

                        qDebug() << "Found new path by excluding edge pair";
                        break; // Found a new path
                    }
                    catch (const std::exception &)
                    {
                        continue; // Try next pair
                    }
                }
            }
        }

        // If we still couldn't find a new path, we're done
        if (!foundNewPath)
        {
            qDebug() << "No more unique paths found after trying all exclusion "
                        "strategies";
            break;
        }
    }
}

/**
 * @brief Find additional paths by routing through intermediate terminals
 * @param context Path finding context
 * @param result List of paths to append to
 * @param n Target number of paths
 *
 * Finds additional paths by routing through intermediate terminals.
 * This method is used when no multi-segment paths exist, as it's effective
 * at finding diverse alternatives in simpler networks where the key is
 * to introduce intermediate stops.
 *
 * The algorithm tries each potential intermediate terminal to find paths
 * that go through it, avoiding cycles and duplicates.
 */
void TerminalGraph::findAdditionalPathsViaIntermediates(
    PathFindingContext &context, QList<Path> &result, int n) const
{

    // Collect potential intermediate nodes (exclude start and end)
    QSet<QString> potentialIntermediates;
    QMutexLocker  locker(&m_mutex);
    QStringList   allNodes = m_graph->getNodes();
    locker.unlock();

    for (const QString &node : allNodes)
    {
        if (node != context.startCanonical && node != context.endCanonical)
        {
            potentialIntermediates.insert(node);
        }
    }

    // Create a list and sort it deterministically
    QList<QString> sortedIntermediates = potentialIntermediates.values();
    std::sort(sortedIntermediates.begin(), sortedIntermediates.end());

    // Try each intermediate node
    for (const QString &intermediate : sortedIntermediates)
    {
        if (result.size() >= n)
        {
            break; // Found enough paths
        }

        QList<PathSegment> fullPath;

        // Try to find a path via this intermediate node
        try
        {
            // Find path from start to intermediate
            QList<PathSegment> firstLeg = findShortestPath(
                context.startCanonical, intermediate, context.mode);

            // Find path from intermediate to end
            QList<PathSegment> secondLeg = findShortestPath(
                intermediate, context.endCanonical, context.mode);

            // Combine the legs
            fullPath = firstLeg;
            fullPath.append(secondLeg);

            // Check for directness
            if (fullPath.size() == 1
                && fullPath[0].from == context.startCanonical
                && fullPath[0].to == context.endCanonical)
            {
                continue; // Skip - this is just the direct path again
            }

            // Check for cycles
            QSet<QString> pathNodes;
            bool          hasCycle = false;

            pathNodes.insert(context.startCanonical);
            for (const PathSegment &seg : fullPath)
            {
                if (pathNodes.contains(seg.to))
                {
                    hasCycle = true;
                    break;
                }
                pathNodes.insert(seg.to);
            }

            if (hasCycle)
            {
                continue; // Skip paths with cycles
            }

            // Check for duplicates
            QString pathSignature = generatePathSignature(fullPath);
            if (context.foundPathSignatures.contains(pathSignature))
            {
                continue; // Skip duplicate path
            }

            // Add this path to results
            Path newPath =
                buildPathDetails(context, fullPath, result.size() + 1);
            result.append(newPath);
            context.foundPathSignatures.insert(pathSignature);

            qDebug() << "Found alternative path via intermediate"
                     << intermediate << "with" << fullPath.size() << "segments";
        }
        catch (const std::exception &)
        {
            continue; // Try next intermediate
        }
    }
}

/**
 * @brief Sort and finalize paths
 * @param result List of paths to sort and finalize
 * @param n Target number of paths
 * @return Sorted and finalized list of paths
 *
 * Sorts paths by total cost, truncates to the target number of paths,
 * and updates path IDs to match the sorted order. This ensures a
 * consistent and deterministic result.
 */
QList<Path> TerminalGraph::sortAndFinalizePaths(QList<Path> &result,
                                                int          n) const
{
    // Sort paths by total cost to ensure deterministic order
    std::sort(result.begin(), result.end(), [](const Path &a, const Path &b) {
        return a.totalPathCost < b.totalPathCost;
    });

    // Truncate to n paths if we have more
    if (result.size() > n)
    {
        result = result.mid(0, n);
    }

    // Update path IDs to match sorted order
    for (int i = 0; i < result.size(); i++)
    {
        result[i].pathId = i + 1;
    }

    qDebug() << "Returning" << result.size() << "paths in total";
    return result;
}

/**
 * @brief Sort and finalize paths
 * @param result List of paths to sort and finalize
 * @param n Target number of paths
 * @return Sorted and finalized list of paths
 *
 * Sorts paths by total cost, truncates to the target number of paths,
 * and updates path IDs to match the sorted order. This ensures a
 * consistent and deterministic result.
 */
QJsonObject TerminalGraph::serializeGraph() const
{
    QMutexLocker locker(&m_mutex); // Lock for thread safety
    QJsonObject  graphData;

    // Serialize terminals
    QJsonObject terminalsJson;
    for (auto it = m_terminals.begin(); it != m_terminals.end(); ++it)
    {
        QJsonObject termData;
        termData["config"] = it.value()->toJson();

        // Add node attributes
        QJsonObject nodeData;
        if (m_graph->nodeAttributes.contains(it.key()))
        {
            for (auto attrIt = m_graph->nodeAttributes[it.key()].begin();
                 attrIt != m_graph->nodeAttributes[it.key()].end(); ++attrIt)
            {
                nodeData[attrIt.key()] =
                    QJsonValue::fromVariant(attrIt.value());
            }
        }
        termData["node_data"]   = nodeData;
        terminalsJson[it.key()] = termData;
    }
    graphData["terminals"] = terminalsJson;

    // Serialize edges
    QJsonArray edgesJson;
    for (auto it = m_graph->adjacencyList.begin();
         it != m_graph->adjacencyList.end(); ++it)
    {
        for (const GraphImpl::InternalEdge &edge : it.value())
        {
            QJsonObject edgeData;
            edgeData["from"]     = it.key();
            edgeData["to"]       = edge.to;
            edgeData["route_id"] = edge.routeId;
            edgeData["mode"]     = static_cast<int>(edge.mode);
            QJsonObject attrs;
            for (auto attrIt = edge.attributes.begin();
                 attrIt != edge.attributes.end(); ++attrIt)
            {
                attrs[attrIt.key()] = QJsonValue::fromVariant(attrIt.value());
            }
            edgeData["attributes"] = attrs;
            edgesJson.append(edgeData);
        }
    }
    graphData["edges"] = edgesJson;

    // Serialize terminal aliases
    QJsonObject aliasesJson;
    for (auto it = m_terminalAliases.begin(); it != m_terminalAliases.end();
         ++it)
    {
        aliasesJson[it.key()] = it.value();
    }
    graphData["terminal_aliases"] = aliasesJson;

    // Serialize canonical-to-aliases mapping
    QJsonObject canonJson;
    for (auto it = m_canonicalToAliases.begin();
         it != m_canonicalToAliases.end(); ++it)
    {
        QJsonArray aliasesArray;
        for (const QString &alias : it.value())
        {
            aliasesArray.append(alias);
        }
        canonJson[it.key()] = aliasesArray;
    }
    graphData["canonical_to_aliases"] = canonJson;

    // Serialize cost function weights
    QJsonObject weightsJson;
    for (auto it = m_costFunctionParametersWeights.begin();
         it != m_costFunctionParametersWeights.end(); ++it)
    {
        QJsonObject paramsJson;
        QVariantMap params = it.value().toMap();
        for (auto pIt = params.begin(); pIt != params.end(); ++pIt)
        {
            paramsJson[pIt.key()] = QJsonValue::fromVariant(pIt.value());
        }
        weightsJson[it.key()] = paramsJson;
    }
    graphData["cost_function_weights"] = weightsJson;

    // Serialize default link attributes
    QJsonObject attrsJson;
    for (auto it = m_defaultLinkAttributes.begin();
         it != m_defaultLinkAttributes.end(); ++it)
    {
        attrsJson[it.key()] = QJsonValue::fromVariant(it.value());
    }
    graphData["default_link_attributes"] = attrsJson;

    qDebug() << "Serialized graph with" << m_terminals.size() << "terminals";
    return graphData;
}

/**
 * @brief Deserializes a graph from JSON
 * @param data JSON data to deserialize
 * @param dir Directory path for terminal storage (optional)
 * @return Pointer to new TerminalGraph instance
 * @throws std::exception If deserialization fails
 *
 * Reconstructs a graph from a JSON object previously created by
 * serializeGraph(). Creates terminals, edges, aliases, and restores all graph
 * attributes.
 */
TerminalGraph *TerminalGraph::deserializeGraph(const QJsonObject &data,
                                               const QString     &dir)
{
    TerminalGraph *graph = new TerminalGraph(dir);
    try
    {
        // Restore cost function weights
        if (data.contains("cost_function_weights"))
        {
            QJsonObject weightsJson = data["cost_function_weights"].toObject();
            for (auto it = weightsJson.begin(); it != weightsJson.end(); ++it)
            {
                if (it.value().isObject())
                {
                    QJsonObject paramsJson = it.value().toObject();
                    QVariantMap paramsMap;
                    for (auto pIt = paramsJson.begin(); pIt != paramsJson.end();
                         ++pIt)
                    {
                        paramsMap[pIt.key()] = pIt.value().toVariant();
                    }
                    graph->m_costFunctionParametersWeights[it.key()] =
                        paramsMap;
                }
            }
        }

        // Restore default link attributes
        if (data.contains("default_link_attributes"))
        {
            QJsonObject attrsJson = data["default_link_attributes"].toObject();
            for (auto it = attrsJson.begin(); it != attrsJson.end(); ++it)
            {
                graph->m_defaultLinkAttributes[it.key()] =
                    it.value().toVariant();
            }
        }

        // Restore terminal aliases
        if (data.contains("terminal_aliases"))
        {
            QJsonObject aliasesJson = data["terminal_aliases"].toObject();
            for (auto it = aliasesJson.begin(); it != aliasesJson.end(); ++it)
            {
                if (it.value().isString())
                {
                    graph->m_terminalAliases[it.key()] = it.value().toString();
                }
            }
        }

        // Restore canonical-to-aliases mapping
        if (data.contains("canonical_to_aliases"))
        {
            QJsonObject canonJson = data["canonical_to_aliases"].toObject();
            for (auto it = canonJson.begin(); it != canonJson.end(); ++it)
            {
                if (it.value().isArray())
                {
                    QJsonArray    aliasesArray = it.value().toArray();
                    QSet<QString> aliases;
                    for (const QJsonValue &val : aliasesArray)
                    {
                        if (val.isString())
                        {
                            aliases.insert(val.toString());
                        }
                    }
                    graph->m_canonicalToAliases[it.key()] = aliases;
                }
            }
        }

        // Restore terminals
        if (data.contains("terminals"))
        {
            QJsonObject termsJson = data["terminals"].toObject();
            for (auto it = termsJson.begin(); it != termsJson.end(); ++it)
            {
                QString termName = it.key();
                if (!it.value().isObject())
                {
                    continue;
                }
                QJsonObject termData = it.value().toObject();
                if (!termData.contains("config")
                    || !termData["config"].isObject())
                {
                    continue;
                }
                QJsonObject config      = termData["config"].toObject();
                config["terminal_name"] = termName;
                Terminal *term          = Terminal::fromJson(config, dir);
                if (!term)
                {
                    qWarning() << "Failed to create terminal:" << termName;
                    continue;
                }
                graph->m_terminals[termName] = term;
                graph->m_graph->adjacencyList[termName] =
                    QList<GraphImpl::InternalEdge>();
                if (termData.contains("node_data")
                    && termData["node_data"].isObject())
                {
                    QJsonObject nodeData = termData["node_data"].toObject();
                    for (auto nIt = nodeData.begin(); nIt != nodeData.end();
                         ++nIt)
                    {
                        graph->m_graph->setNodeAttribute(
                            termName, nIt.key(), nIt.value().toVariant());
                    }
                }
            }
        }

        // Restore edges
        if (data.contains("edges") && data["edges"].isArray())
        {
            QJsonArray edgesJson = data["edges"].toArray();
            for (const QJsonValue &val : edgesJson)
            {
                if (!val.isObject())
                {
                    continue;
                }
                QJsonObject edgeData = val.toObject();
                if (!edgeData.contains("from") || !edgeData.contains("to")
                    || !edgeData.contains("mode")
                    || !edgeData.contains("attributes"))
                {
                    continue;
                }
                QString            from    = edgeData["from"].toString();
                QString            to      = edgeData["to"].toString();
                int                modeInt = edgeData["mode"].toInt();
                QString            routeId = edgeData.contains("route_id")
                                                 ? edgeData["route_id"].toString()
                                                 : QString();
                TransportationMode mode =
                    static_cast<TransportationMode>(modeInt);
                QVariantMap attrs;
                QJsonObject attrsJson = edgeData["attributes"].toObject();
                for (auto aIt = attrsJson.begin(); aIt != attrsJson.end();
                     ++aIt)
                {
                    attrs[aIt.key()] = aIt.value().toVariant();
                }
                graph->m_graph->addEdge(from, to, routeId, mode, attrs);
            }
        }

        qDebug() << "Graph deserialized with" << graph->m_terminals.size()
                 << "terminals";
        return graph;
    }
    catch (const std::exception &e)
    {
        qWarning() << "Deserialization failed:" << e.what();
        delete graph;
        throw;
    }
}

/**
 * @brief Saves the graph to a file
 * @param filepath Path to save the file
 * @throws std::runtime_error If file cannot be opened
 *
 * Serializes the graph to JSON and saves it to a file at the specified path.
 * Uses indented JSON format for better readability.
 */
void TerminalGraph::saveToFile(const QString &filepath) const
{
    QJsonObject graphData = serializeGraph();
    QFile       file(filepath);
    if (!file.open(QIODevice::WriteOnly))
    {
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
 * @param dir Directory path for terminal storage (optional)
 * @return Pointer to loaded TerminalGraph
 * @throws std::runtime_error If file cannot be opened
 * @throws std::exception If deserialization fails
 *
 * Loads a graph from a JSON file at the specified path. If a directory
 * path is provided, it will be used for terminal storage.
 */
TerminalGraph *TerminalGraph::loadFromFile(const QString &filepath,
                                           const QString &dir)
{
    QFile file(filepath);
    if (!file.open(QIODevice::ReadOnly))
    {
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
QString TerminalGraph::getCanonicalName(const QString &name) const
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
double TerminalGraph::computeCost(const QVariantMap &params,
                                  const QVariantMap &weights,
                                  TransportationMode mode) const
{
    double      cost    = 0.0;
    QString     modeStr = QString::number(static_cast<int>(mode));
    QVariantMap modeWeights =
        weights.value(modeStr, weights.value("default")).toMap();

    for (auto it = params.begin(); it != params.end(); ++it)
    {
        bool   ok;
        double value = it.value().toDouble(&ok);
        if (!ok)
            continue;

        double weight = modeWeights.value(it.key(), 1.0).toDouble();
        cost += weight * value;
    }
    return cost;
}

/**
 * @brief Finds shortest path with exclusions
 * @param start Starting terminal name
 * @param end Ending terminal name
 * @param mode Transportation mode (use Any to consider all modes)
 * @param edges Edges to exclude
 * @param nodes Nodes to exclude
 * @return List of path segments
 *
 * Uses Dijkstra's algorithm with edge and node exclusions.
 * Computes costs including terminal delays.
 */
QList<PathSegment> TerminalGraph::findShortestPathWithExclusions(
    const QString &start, const QString &end, TransportationMode requestedMode,
    const QSet<EdgeIdentifier> &edgesToExclude,
    const QSet<QString>        &nodesToExclude) const
{
    QString startCanonical = getCanonicalName(start);
    QString endCanonical   = getCanonicalName(end);

    // Validate terminal existence
    if (!m_terminals.contains(startCanonical)
        || !m_terminals.contains(endCanonical))
    {
        throw std::invalid_argument("Terminal not found");
    }

    // Check if start or end is excluded
    if (nodesToExclude.contains(startCanonical)
        || nodesToExclude.contains(endCanonical))
    {
        throw std::invalid_argument("Start/end excluded");
    }

    // Dijkstra's setup with exclusions
    QHash<QString, double>             distance;
    QHash<QString, QString>            previous;
    QHash<QString, TransportationMode> edgeMode;
    QHash<QString, QVariantMap>        edgeAttrs;

    // Initialize distances for all nodes
    QStringList allNodes = m_graph->getNodes();
    for (const QString &node : allNodes)
    {
        if (nodesToExclude.contains(node))
        {
            continue; // Skip excluded nodes
        }
        distance[node] = std::numeric_limits<double>::infinity();
        previous[node] = QString(); // No predecessor yet
    }
    distance[startCanonical] = 0.0; // Start at zero

    // Priority queue simulation with unvisited set
    QSet<QString> unvisited;
    for (const QString &node : allNodes)
    {
        if (!nodesToExclude.contains(node))
        {
            unvisited.insert(node);
        }
    }

    // Main Dijkstra's loop
    while (!unvisited.isEmpty())
    {
        // Find node with minimum distance
        QString current;
        double  minDist = std::numeric_limits<double>::infinity();
        for (const QString &node : unvisited)
        {
            if (distance[node] < minDist)
            {
                minDist = distance[node];
                current = node;
            }
        }

        // Break if no reachable node found
        if (current.isEmpty()
            || minDist == std::numeric_limits<double>::infinity())
        {
            break; // No path possible
        }

        // Stop if destination reached
        if (current == endCanonical)
        {
            break;
        }

        unvisited.remove(current); // Mark as visited

        // Process neighbors
        const QList<GraphImpl::InternalEdge> &edgeList =
            m_graph->adjacencyList[current];
        for (const GraphImpl::InternalEdge &edge : edgeList)
        {
            QString neighbor = edge.to;

            // Skip if excluded node
            if (nodesToExclude.contains(neighbor))
            {
                continue;
            }

            // Skip if edge mode doesn't match requested mode (when a specific
            // mode is requested)
            if (requestedMode != TransportationMode::Any
                && edge.mode != requestedMode)
            {
                continue;
            }

            // Skip if edge is in excluded set
            // This is the key change - when using TransportationMode::Any, we
            // need to be more precise about which edges to exclude
            EdgeIdentifier edgeId = {current, neighbor, edge.mode};
            if (edgesToExclude.contains(edgeId))
            {
                // For TransportationMode::Any, only exclude this specific
                // edge-mode combination For specific modes, exclude all
                // edge-mode combinations
                if (requestedMode != TransportationMode::Any)
                {
                    continue;
                }

                // When using Any mode, check if we're excluding the specific
                // connection or just this mode on this connection
                EdgeIdentifier anyModeEdgeId = {current, neighbor,
                                                TransportationMode::Any};
                if (edgesToExclude.contains(anyModeEdgeId))
                {
                    continue; // Exclude all modes for this connection
                }

                // Otherwise, only exclude this specific mode on this connection
            }

            // Skip if already visited
            if (!unvisited.contains(neighbor))
            {
                continue;
            }

            // Calculate cost with terminal delays
            Terminal *currTerm = m_terminals[current];
            Terminal *nextTerm = m_terminals[neighbor];
            double    delay    = currTerm->estimateContainerHandlingTime()
                           + nextTerm->estimateContainerHandlingTime();
            double termCost = currTerm->estimateContainerCost()
                              + nextTerm->estimateContainerCost();

            // Prepare cost parameters
            QVariantMap params       = edge.attributes;
            params["terminal_delay"] = delay;
            params["terminal_cost"]  = termCost;

            // Compute total cost using the cost function
            double totalCost =
                computeCost(params, m_costFunctionParametersWeights, edge.mode);

            // Update distance if shorter path found
            double alt = distance[current] + totalCost;
            if (alt < distance[neighbor])
            {
                distance[neighbor]  = alt;
                previous[neighbor]  = current;
                edgeMode[neighbor]  = edge.mode;
                edgeAttrs[neighbor] = edge.attributes;
            }
        }
    }

    // Reconstruct the path
    QList<PathSegment> path;

    // Check if path exists
    if (previous[endCanonical].isEmpty() && startCanonical != endCanonical)
    {
        throw std::runtime_error("No path found with exclusions");
    }

    // Build path from end to start
    QString current = endCanonical;
    while (!previous[current].isEmpty())
    {
        QString     from = previous[current];
        PathSegment seg;

        // Populate segment details
        seg.from           = from;
        seg.to             = current;
        seg.mode           = edgeMode[current];
        seg.weight         = distance[current] - distance[from];
        seg.fromTerminalId = from;
        seg.toTerminalId   = current;
        seg.attributes     = edgeAttrs[current];

        // Add segment to path (in reverse order)
        path.prepend(seg);
        current = from; // Move to previous node
    }

    qDebug() << "Path found with" << path.size() << "segments";
    return path;
}

} // namespace TerminalSim
