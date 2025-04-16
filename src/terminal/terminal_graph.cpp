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
 * link attributes. Logs initialization details.
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
 * Deletes all terminal instances and the graph impl.
 * Ensures thread-safe cleanup.
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
 * Updates cost weights thread-safely.
 */
void TerminalGraph::setCostFunctionParameters(const QVariantMap &params)
{
    QMutexLocker locker(&m_mutex);            // Ensure thread safety
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
 *
 * Associates an alias with a canonical name.
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
 * Returns all aliases for a given terminal.
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
 * @param attrs Route attributes
 *
 * Adds a route with attributes to the graph.
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
 * Retrieves edge details for a specific mode.
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
 * Returns all terminals in the specified region.
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
 * Finds all routes connecting two regions.
 * Avoids deadlock by inlining terminal retrieval.
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
 * Creates bidirectional routes for common modes.
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
 *
 * Adds bidirectional routes for shared modes.
 * Ensures no deadlock by avoiding nested locks.
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
 * Links terminals across regions bidirectionally.
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
 *
 * Updates route attributes thread-safely.
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
 * @param name Terminal name
 * @return Pointer to terminal, or throws if not found
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
 * @param name Terminal name
 * @return True if exists, false otherwise
 */
bool TerminalGraph::terminalExists(const QString &name) const
{
    QMutexLocker locker(&m_mutex); // Lock for thread safety
    QString      canonical = getCanonicalName(name);
    return m_terminals.contains(canonical);
}

/**
 * @brief Removes a terminal from the graph
 * @param name Terminal name
 * @return True if removed, false if not found
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
 * Deletes all terminals and resets graph state.
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
 * @param name Terminal name, empty for all
 * @return Status details
 *
 * Returns status info for one or all terminals.
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
 */
const QString &TerminalGraph::getPathToTerminalsDirectory() const
{
    return m_pathToTerminalsDirectory; // No lock needed for const
}

/**
 * @brief Finds the shortest path between terminals
 * @param start Starting terminal name
 * @param end Ending terminal name
 * @param mode Transportation mode (optional, finds path regardless of mode if
 * not specified)
 * @return List of path segments
 *
 * Uses Dijkstra's algorithm to find shortest path.
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
 * @param start Starting terminal name
 * @param end Ending terminal name
 * @param regions Allowed regions
 * @param mode Transportation mode (optional, finds path regardless of mode if
 * not specified)
 * @return List of path segments
 *
 * Limits path finding to specified regions.
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
 * @param start Starting terminal name
 * @param end Ending terminal name
 * @param n Number of paths to find
 * @param mode Transport mode (default: Any - finds paths regardless of mode)
 * @param skipDelays Skip same-mode delays if true
 * @return List of paths
 *
 * Uses Yen's algorithm to find k-shortest paths.
 * Avoids deadlock by pre-collecting data.
 */
QList<Path> TerminalGraph::findTopNShortestPaths(const QString &start,
                                                 const QString &end, int n,
                                                 TransportationMode mode,
                                                 bool skipDelays) const
{
    // Initial lock to gather graph data
    QMutexLocker locker(&m_mutex);

    // Resolve canonical names
    QString startCanonical = getCanonicalName(start);
    QString endCanonical   = getCanonicalName(end);

    qDebug() << "Finding top" << n << "shortest paths from" << startCanonical
             << "to" << endCanonical << "with mode"
             << (mode == TransportationMode::Any
                     ? "Any"
                     : QString::number(static_cast<int>(mode)));

    // Validate terminal existence
    if (!m_terminals.contains(startCanonical)
        || !m_terminals.contains(endCanonical))
    {
        throw std::invalid_argument("Terminal not found");
    }

    // Copy necessary data to avoid deadlocks
    QHash<QString, Terminal *> termPointers = m_terminals;

    // For direct paths - collect all available direct routes between these
    // nodes
    QList<GraphImpl::InternalEdge> directEdges;

    // Only collect if the requested mode is "Any" or we need to check specific
    // modes
    if (mode == TransportationMode::Any)
    {
        // Get all edges between start and end
        directEdges = m_graph->getEdges(startCanonical, endCanonical);
    }
    else
    {
        // Find the specific mode edge
        GraphImpl::InternalEdge *specificEdge =
            m_graph->findEdge(startCanonical, endCanonical, mode);

        if (specificEdge)
        {
            directEdges.append(*specificEdge);
        }
    }

    // Unlock before path finding
    locker.unlock();

    QList<Path> result; // Store final paths

    // Structure to hold terminal cost info
    struct TermInfo
    {
        double handlingTime;
        double cost;
    };

    // Pre-collect terminal info with lazy initialization
    QHash<QString, TermInfo> termInfoCache;

    // Helper function to get terminal info with caching
    auto getTermInfo = [&termPointers, &termInfoCache](
                           const QString &name) -> const TermInfo & {
        auto it = termInfoCache.find(name);
        if (it == termInfoCache.end())
        {
            Terminal *term = termPointers[name];
            it             = termInfoCache.insert(name,
                                                  {term->estimateContainerHandlingTime(),
                                                   term->estimateContainerCost()});
        }
        return it.value();
    };

    // Helper function to determine if terminal costs should be skipped
    auto shouldSkipCosts =
        [&skipDelays](int                       terminalIndex,
                      const QList<PathSegment> &segments) -> bool {
        if (!skipDelays)
            return false;
        if (terminalIndex == 0 || terminalIndex >= segments.size())
            return false;
        return segments[terminalIndex - 1].mode == segments[terminalIndex].mode;
    };

    // Helper function to build path details
    auto buildPathDetails = [&](const QList<PathSegment> &segments,
                                int                       pathId) -> Path {
        double             totalEdgeCosts     = 0.0;
        double             totalTerminalCosts = 0.0;
        QList<QVariantMap> terminalsInPath;

        // Add start terminal info
        const TermInfo &startInfo = getTermInfo(startCanonical);
        QVariantMap     startTerm;
        startTerm["terminal"]      = startCanonical;
        startTerm["handling_time"] = startInfo.handlingTime;
        startTerm["cost"]          = startInfo.cost;
        startTerm["costs_skipped"] = false;
        terminalsInPath.append(startTerm);
        totalTerminalCosts += startInfo.cost;

        // Process segments
        for (int i = 0; i < segments.size(); ++i)
        {
            const PathSegment &seg = segments[i];
            totalEdgeCosts += seg.weight;

            // Get terminal info for destination node
            const TermInfo &termInfo = getTermInfo(seg.to);

            // Add terminal info to path
            QVariantMap termEntry;
            termEntry["terminal"]      = seg.to;
            termEntry["handling_time"] = termInfo.handlingTime;
            termEntry["cost"]          = termInfo.cost;

            // Terminal index is i+1 (0 is the start terminal)
            bool skip                  = shouldSkipCosts(i + 1, segments);
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
    };

    // Signature generation function - deterministic path identifier
    auto generatePathSignature = [](const QList<PathSegment> &segments) {
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
    };

    // Track found paths by signature
    QSet<QString> foundPathSignatures;

    // STEP 1: First check for direct paths between origin and destination
    // and add all of them as potential paths
    if (!directEdges.isEmpty())
    {
        qDebug() << "Found" << directEdges.size() << "direct routes between"
                 << startCanonical << "and" << endCanonical;

        // Sort the direct edges by cost for deterministic behavior
        std::sort(directEdges.begin(), directEdges.end(),
                  [&](const GraphImpl::InternalEdge &a,
                      const GraphImpl::InternalEdge &b) {
                      // Prepare parameters for cost function
                      QVariantMap paramsA = a.attributes;
                      QVariantMap paramsB = b.attributes;

                      // Get terminal costs (same for both paths)
                      Terminal *startTerm = termPointers[startCanonical];
                      Terminal *endTerm   = termPointers[endCanonical];
                      double delay = startTerm->estimateContainerHandlingTime()
                                     + endTerm->estimateContainerHandlingTime();
                      double cost = startTerm->estimateContainerCost()
                                    + endTerm->estimateContainerCost();

                      paramsA["terminal_delay"] = delay;
                      paramsA["terminal_cost"]  = cost;
                      paramsB["terminal_delay"] = delay;
                      paramsB["terminal_cost"]  = cost;

                      // Compute costs
                      double costA = computeCost(
                          paramsA, m_costFunctionParametersWeights, a.mode);
                      double costB = computeCost(
                          paramsB, m_costFunctionParametersWeights, b.mode);

                      return costA < costB;
                  });

        // Add direct paths to results
        for (const GraphImpl::InternalEdge &edge : directEdges)
        {
            // Skip if we've already found enough paths
            if (result.size() >= n)
            {
                break;
            }

            // Create a path segment
            PathSegment segment;
            segment.from           = startCanonical;
            segment.to             = endCanonical;
            segment.mode           = edge.mode;
            segment.fromTerminalId = startCanonical;
            segment.toTerminalId   = endCanonical;
            segment.attributes     = edge.attributes;

            // Calculate edge weight
            Terminal *startTerm = termPointers[startCanonical];
            Terminal *endTerm   = termPointers[endCanonical];
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
            if (foundPathSignatures.contains(pathSignature))
            {
                continue; // Skip duplicate
            }

            // Add to results
            foundPathSignatures.insert(pathSignature);
            Path path = buildPathDetails(directPath, result.size() + 1);
            result.append(path);

            qDebug() << "Added direct path with mode"
                     << static_cast<int>(edge.mode) << "and cost"
                     << path.totalPathCost;
        }
    }

    // If we still need more paths, find indirect routes
    if (result.size() < n)
    {
        // STEP 2: Find the shortest path if not already found
        QList<PathSegment> shortestPath;
        try
        {
            // Find shortest path, excluding direct paths we've already found
            QSet<EdgeIdentifier> directPathsToExclude;

            // If we've already found some direct paths, exclude them
            for (const Path &path : result)
            {
                if (path.segments.size() == 1)
                {
                    const PathSegment &seg    = path.segments.first();
                    EdgeIdentifier directEdge = {seg.from, seg.to, seg.mode};
                    directPathsToExclude.insert(directEdge);
                }
            }

            if (directPathsToExclude.isEmpty())
            {
                shortestPath =
                    findShortestPath(startCanonical, endCanonical, mode);
            }
            else
            {
                // Find path excluding the direct routes we've already added
                shortestPath = findShortestPathWithExclusions(
                    startCanonical, endCanonical, mode, directPathsToExclude);
            }
        }
        catch (const std::exception &e)
        {
            qWarning() << "No additional path found:" << e.what();

            // If we couldn't find additional paths but already have direct
            // ones, return those
            if (!result.isEmpty())
            {
                return result;
            }
            return QList<Path>(); // Empty result
        }

        if (shortestPath.isEmpty())
        {
            // If we couldn't find additional paths but already have direct
            // ones, return those
            if (!result.isEmpty())
            {
                return result;
            }
            return QList<Path>(); // Empty result
        }

        // Check if this path is already in our results
        QString shortestPathSignature = generatePathSignature(shortestPath);
        if (!foundPathSignatures.contains(shortestPathSignature))
        {
            foundPathSignatures.insert(shortestPathSignature);
            Path path = buildPathDetails(shortestPath, result.size() + 1);
            result.append(path);

            qDebug() << "Added shortest path with" << shortestPath.size()
                     << "segments and cost" << path.totalPathCost;
        }

        // If we still need more paths and have a multi-segment path,
        // use the edge exclusion approach for remaining paths
        if (result.size() < n)
        {
            // Create a set of key edges from all non-direct paths we've found
            QSet<QPair<QString, QPair<QString, int>>>
                keyEdges; // From, To, Mode

            for (const Path &path : result)
            {
                if (path.segments.size() > 1)
                { // Only consider non-direct paths
                    for (const PathSegment &segment : path.segments)
                    {
                        keyEdges.insert(qMakePair(
                            segment.from,
                            qMakePair(segment.to,
                                      static_cast<int>(segment.mode))));
                    }
                }
            }

            // If we have no multi-segment paths, try with intermediate nodes
            if (keyEdges.isEmpty())
            {
                // Collect potential intermediate nodes (exclude start and end)
                QSet<QString> potentialIntermediates;

                // Get all nodes from the graph
                locker.relock();
                QStringList allNodes = m_graph->getNodes();
                locker.unlock();

                for (const QString &node : allNodes)
                {
                    if (node != startCanonical && node != endCanonical)
                    {
                        potentialIntermediates.insert(node);
                    }
                }

                // Create a list and sort it deterministically
                QList<QString> sortedIntermediates =
                    potentialIntermediates.values();
                std::sort(sortedIntermediates.begin(),
                          sortedIntermediates.end());

                // Try each intermediate node to find alternative paths
                for (const QString &intermediate : sortedIntermediates)
                {
                    if (result.size() >= n)
                    {
                        break; // Found enough paths
                    }

                    // Find path from start to intermediate
                    QList<PathSegment> firstLeg;
                    try
                    {
                        firstLeg = findShortestPath(startCanonical,
                                                    intermediate, mode);
                    }
                    catch (const std::exception &)
                    {
                        continue; // No path to intermediate
                    }

                    // Find path from intermediate to end
                    QList<PathSegment> secondLeg;
                    try
                    {
                        secondLeg =
                            findShortestPath(intermediate, endCanonical, mode);
                    }
                    catch (const std::exception &)
                    {
                        continue; // No path from intermediate
                    }

                    // Combine the legs
                    QList<PathSegment> fullPath = firstLeg;
                    fullPath.append(secondLeg);

                    // Check for directness - if this is just the direct path
                    // again, skip it
                    bool isJustDirectPath = false;
                    if (fullPath.size() == 1)
                    {
                        if (fullPath[0].from == startCanonical
                            && fullPath[0].to == endCanonical)
                        {
                            isJustDirectPath = true;
                        }
                    }

                    if (isJustDirectPath)
                    {
                        continue; // Skip - this is just the direct path again
                    }

                    // Check if path contains cycles
                    QSet<QString> pathNodes;
                    bool          hasCycle = false;

                    pathNodes.insert(startCanonical);
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

                    // Check if we already found this path
                    QString pathSignature = generatePathSignature(fullPath);
                    if (foundPathSignatures.contains(pathSignature))
                    {
                        continue; // Skip duplicate path
                    }

                    // Add this path to results
                    Path newPath =
                        buildPathDetails(fullPath, result.size() + 1);
                    result.append(newPath);
                    foundPathSignatures.insert(pathSignature);

                    qDebug() << "Found alternative path via intermediate"
                             << intermediate << "with" << fullPath.size()
                             << "segments";
                }
            }
            else
            {
                // Convert to a deterministically ordered list
                QList<QPair<QString, QPair<QString, int>>> orderedEdges =
                    keyEdges.values();
                std::sort(orderedEdges.begin(), orderedEdges.end());

                // Try removing each key edge and find alternative paths
                for (int i = result.size(); i < n; i++)
                {
                    bool foundNewPath = false;

                    // Try removing each edge from the multi-segment paths
                    for (const auto &edgeTriple : orderedEdges)
                    {
                        // Create a deterministic exclusion set
                        QSet<EdgeIdentifier> edgesToExclude;

                        // Add the specific edge to exclude
                        QString            from = edgeTriple.first;
                        QString            to   = edgeTriple.second.first;
                        TransportationMode edgeMode =
                            static_cast<TransportationMode>(
                                edgeTriple.second.second);

                        EdgeIdentifier edgeId = {from, to, edgeMode};
                        edgesToExclude.insert(edgeId);

                        // Find alternative path with this edge excluded
                        QList<PathSegment> alternativePath;
                        try
                        {
                            alternativePath = findShortestPathWithExclusions(
                                startCanonical, endCanonical, mode,
                                edgesToExclude);

                            // Check if this is a path we already found
                            QString pathSignature =
                                generatePathSignature(alternativePath);
                            if (foundPathSignatures.contains(pathSignature))
                            {
                                continue; // Skip duplicate path
                            }

                            // Add this new path to our results
                            Path path = buildPathDetails(alternativePath,
                                                         result.size() + 1);
                            result.append(path);
                            foundPathSignatures.insert(pathSignature);
                            foundNewPath = true;

                            qDebug() << "Found new path by excluding edge"
                                     << from << "->" << to << "with mode"
                                     << static_cast<int>(edgeMode);

                            // Add any new key edges from this path for future
                            // exclusion
                            for (const PathSegment &segment : alternativePath)
                            {
                                QPair<QString, QPair<QString, int>> newEdge =
                                    qMakePair(segment.from,
                                              qMakePair(segment.to,
                                                        static_cast<int>(
                                                            segment.mode)));

                                if (!keyEdges.contains(newEdge))
                                {
                                    keyEdges.insert(newEdge);
                                    orderedEdges.append(newEdge);
                                }
                            }

                            // Resort the ordered edges list
                            std::sort(orderedEdges.begin(), orderedEdges.end());

                            break; // Found a new path, move to next iteration
                        }
                        catch (const std::exception &e)
                        {
                            qDebug()
                                << "No alternative path found when excluding"
                                << from << "->" << to << ":" << e.what();
                            continue; // Try the next edge
                        }
                    }

                    // If we couldn't find any new paths by single edge removal,
                    // try removing combinations of edges
                    if (!foundNewPath && i < n - 1 && orderedEdges.size() >= 2)
                    {
                        qDebug() << "Trying edge combinations...";

                        // Generate pairs of edges to exclude
                        for (int j = 0; j < orderedEdges.size(); j++)
                        {
                            if (foundNewPath || result.size() >= n)
                                break;

                            for (int k = j + 1; k < orderedEdges.size(); k++)
                            {
                                auto edge1 = orderedEdges[j];
                                auto edge2 = orderedEdges[k];

                                QSet<EdgeIdentifier> edgesToExclude;

                                EdgeIdentifier edgeId1 = {
                                    edge1.first, edge1.second.first,
                                    static_cast<TransportationMode>(
                                        edge1.second.second)};

                                EdgeIdentifier edgeId2 = {
                                    edge2.first, edge2.second.first,
                                    static_cast<TransportationMode>(
                                        edge2.second.second)};

                                edgesToExclude.insert(edgeId1);
                                edgesToExclude.insert(edgeId2);

                                try
                                {
                                    QList<PathSegment> alternativePath =
                                        findShortestPathWithExclusions(
                                            startCanonical, endCanonical, mode,
                                            edgesToExclude);

                                    QString pathSignature =
                                        generatePathSignature(alternativePath);
                                    if (foundPathSignatures.contains(
                                            pathSignature))
                                    {
                                        continue; // Skip duplicate path
                                    }

                                    Path path = buildPathDetails(
                                        alternativePath, result.size() + 1);
                                    result.append(path);
                                    foundPathSignatures.insert(pathSignature);
                                    foundNewPath = true;

                                    qDebug() << "Found new path by excluding "
                                                "edge pair";
                                    break; // Found a new path
                                }
                                catch (const std::exception &e)
                                {
                                    continue; // Try next pair
                                }
                            }
                        }
                    }

                    // If we still couldn't find a new path, we're done
                    if (!foundNewPath)
                    {
                        qDebug() << "No more unique paths found after trying "
                                    "all strategies";
                        break;
                    }

                    // If we've found enough paths, stop
                    if (result.size() >= n)
                    {
                        break;
                    }
                }
            }
        }
    }

    // Sort paths by total cost to ensure deterministic order
    std::sort(result.begin(), result.end(), [](const Path &a, const Path &b) {
        return a.totalPathCost < b.totalPathCost;
    });

    // Update path IDs to match sorted order
    for (int i = 0; i < result.size(); i++)
    {
        result[i].pathId = i + 1;
    }

    qDebug() << "Found" << result.size() << "unique paths in total";
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
 * @param dir Directory path for terminal storage
 * @return Pointer to new TerminalGraph instance
 *
 * Reconstructs graph with terminals and aliases.
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
 * @param dir Directory path for terminal storage
 * @return Pointer to loaded TerminalGraph
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
