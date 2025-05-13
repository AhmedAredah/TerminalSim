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

TerminalGraph::TerminalGraph(const QString &dir)
    : QObject(nullptr)
    , m_pathToTerminalsDirectory(dir)
{
    // Initialize default cost function parameters
    m_costFunctionParametersWeights = {
        {"default", QVariantMap{{"cost", 1.0},
                                {"travelTime", 1.0},
                                {"distance", 1.0},
                                {"carbonEmissions", 1.0},
                                {"risk", 1.0},
                                {"energyConsumption", 1.0},
                                {"terminal_delay", 1.0},
                                {"terminal_cost", 1.0}}},
        {QString::number(static_cast<int>(TransportationMode::Ship)),
         QVariantMap{{"cost", 1.0},
                     {"travelTime", 1.0},
                     {"distance", 1.0},
                     {"carbonEmissions", 1.0},
                     {"risk", 1.0},
                     {"energyConsumption", 1.0},
                     {"terminal_delay", 1.0},
                     {"terminal_cost", 1.0}}},
        {QString::number(static_cast<int>(TransportationMode::Train)),
         QVariantMap{{"cost", 1.0},
                     {"travelTime", 1.0},
                     {"distance", 1.0},
                     {"carbonEmissions", 1.0},
                     {"risk", 1.0},
                     {"energyConsumption", 1.0},
                     {"terminal_delay", 1.0},
                     {"terminal_cost", 1.0}}},
        {QString::number(static_cast<int>(TransportationMode::Truck)),
         QVariantMap{{"cost", 1.0},
                     {"travelTime", 1.0},
                     {"distance", 1.0},
                     {"carbonEmissions", 1.0},
                     {"risk", 1.0},
                     {"energyConsumption", 1.0},
                     {"terminal_delay", 1.0},
                     {"terminal_cost", 1.0}}}};

    // Set default link attributes
    m_defaultLinkAttributes = {{"cost", 1.0},     {"travelTime", 1.0},
                               {"distance", 1.0}, {"carbonEmissions", 1.0},
                               {"risk", 1.0},     {"energyConsumption", 1.0}};

    qInfo() << "Graph initialized with dir:" << (dir.isEmpty() ? "None" : dir);
}

TerminalGraph::~TerminalGraph()
{
    // Copy terminals to a local list while holding the lock
    QList<Terminal *> terminalsToDelete;

    {
        QMutexLocker locker(&m_mutex);
        for (auto it = m_terminals.begin(); it != m_terminals.end(); ++it)
        {
            terminalsToDelete.append(it.value());
        }
        m_terminals.clear();
    }

    // Now delete all terminals without holding the lock
    for (Terminal *term : terminalsToDelete)
    {
        delete term;
    }

    qDebug() << "Graph destroyed";
}

void TerminalGraph::setCostFunctionParameters(const QVariantMap &params)
{
    QMutexLocker locker(&m_mutex);
    m_costFunctionParametersWeights = params;
}

void TerminalGraph::setLinkDefaultAttributes(const QVariantMap &attrs)
{
    QMutexLocker locker(&m_mutex);   // Ensure thread safety
    m_defaultLinkAttributes = attrs; // Update attributes
}

Terminal *TerminalGraph::addTerminalInternal(const QVariantMap &terminalData)
{
    // Extract terminal names (assuming validation has been done)
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

    QString     canonical    = terminalNames.first();
    QString     displayName  = terminalData["display_name"].toString();
    QVariantMap customConfig = terminalData["custom_config"].toMap();
    QString     region = terminalData.value("region", QString()).toString();

    // Parse interfaces
    QVariantMap interfacesMap = terminalData["terminal_interfaces"].toMap();
    QMap<TerminalInterface, QSet<TransportationMode>> interfaces;

    for (auto it = interfacesMap.constBegin(); it != interfacesMap.constEnd();
         ++it)
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
                mode =
                    EnumUtils::stringToTransportationMode(modeVar.toString());
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

    // Create terminal
    Terminal *term = new Terminal(
        canonical, displayName, interfaces,
        QMap<QPair<TransportationMode, QString>, QString>(),
        customConfig.value("capacity").toMap(),
        customConfig.value("dwell_time").toMap(),
        customConfig.value("customs").toMap(),
        customConfig.value("cost").toMap(), m_pathToTerminalsDirectory);

    // Add vertex to graph
    m_graph.addVertex(canonical);

    // Store node attributes
    if (!region.isEmpty())
    {
        if (!m_nodeAttributes.contains(canonical))
        {
            m_nodeAttributes[canonical] = QVariantMap();
        }
        m_nodeAttributes[canonical]["region"] = region;
    }

    // Store terminal and aliases
    m_terminals[canonical] = term;
    m_canonicalToAliases[canonical] =
        QSet<QString>(terminalNames.begin(), terminalNames.end());
    for (const QString &alias : terminalNames)
    {
        m_terminalAliases[alias] = canonical;
    }

    // retrieve terminal details
    m_terminalData[canonical] = TerminalDetails{
        term->estimateContainerHandlingTime(), term->estimateContainerCost()};

    qDebug() << "Added terminal" << canonical << "with"
             << (terminalNames.size() - 1) << "aliases";

    return term;
}

Terminal *TerminalGraph::addTerminal(const QVariantMap &terminalData)
{
    QMutexLocker locker(&m_mutex);

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
        if (m_terminalAliases.contains(name)
            && m_terminalAliases[name] != canonical)
        {
            throw std::invalid_argument("Duplicate terminal name: "
                                        + name.toStdString());
        }
    }

    // Validate terminal interfaces
    QVariantMap interfacesMap = terminalData["terminal_interfaces"].toMap();
    if (interfacesMap.isEmpty())
    {
        throw std::invalid_argument(
            "At least one terminal interface must be provided");
    }

    return addTerminalInternal(terminalData);
}

QMap<QString, Terminal *>
TerminalGraph::addTerminals(const QList<QVariantMap> &terminalsList)
{
    QMutexLocker              locker(&m_mutex);
    QMap<QString, Terminal *> addedTerminals;
    QSet<QString>             allNames;

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

        // Check for name conflicts with existing terminals or other terminals
        // in the list
        for (const QString &name : terminalNames)
        {
            if (allNames.contains(name))
            {
                throw std::invalid_argument("Duplicate terminal name: "
                                            + name.toStdString());
            }
            allNames.insert(name);
        }

        // Validate terminal interfaces
        QVariantMap interfacesMap = terminalData["terminal_interfaces"].toMap();
        if (interfacesMap.isEmpty())
        {
            throw std::invalid_argument(
                "At least one terminal interface must be provided");
        }
    }

    // Add all terminals after validation
    for (const QVariantMap &terminalData : terminalsList)
    {
        Terminal *term            = addTerminalInternal(terminalData);
        QString   canonical       = term->getTerminalName();
        addedTerminals[canonical] = term;
    }

    return addedTerminals;
}

void TerminalGraph::addAliasToTerminal(const QString &name,
                                       const QString &alias)
{
    QMutexLocker locker(&m_mutex);
    QString      canonical = getCanonicalName(name);
    if (!m_terminals.contains(canonical))
    {
        throw std::invalid_argument("Terminal not found: "
                                    + name.toStdString());
    }

    m_terminalAliases[alias] = canonical;
    m_canonicalToAliases[canonical].insert(alias);
    qDebug() << "Added alias" << alias << "to" << canonical;
}

QStringList TerminalGraph::getAliasesOfTerminal(const QString &name) const
{
    QMutexLocker locker(&m_mutex);
    QString      canonical = getCanonicalName(name);
    return m_canonicalToAliases.value(canonical).values();
}

QPair<QString, QString>
TerminalGraph::addRouteInternal(const QString &id, const QString &start,
                                const QString &end, TransportationMode mode,
                                const QVariantMap &attrs)
{
    QString startCanonical = m_terminalAliases.value(start, start);
    QString endCanonical   = m_terminalAliases.value(end, end);

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

    // Create edge data with the same ID for both directions
    EdgeData edgeData = {id, mode, routeAttrs};

    // Add forward direction (start -> end)
    EdgeIdentifier forwardEdgeKey(startCanonical, endCanonical, mode);
    if (!m_edgeData.contains(forwardEdgeKey))
    {
        m_edgeData[forwardEdgeKey] = QList<EdgeData>();
    }

    // Add or update the forward edge
    bool             found        = false;
    QList<EdgeData> &forwardEdges = m_edgeData[forwardEdgeKey];
    for (int i = 0; i < forwardEdges.size(); ++i)
    {
        if (forwardEdges[i].mode == mode)
        {
            forwardEdges[i] = edgeData;
            found           = true;
            break;
        }
    }

    if (!found)
    {
        forwardEdges.append(edgeData);
    }

    // Add backward direction (end -> start) with the SAME edge data
    EdgeIdentifier backwardEdgeKey(endCanonical, startCanonical, mode);
    if (!m_edgeData.contains(backwardEdgeKey))
    {
        m_edgeData[backwardEdgeKey] = QList<EdgeData>();
    }

    // Add or update the backward edge
    found                          = false;
    QList<EdgeData> &backwardEdges = m_edgeData[backwardEdgeKey];
    for (int i = 0; i < backwardEdges.size(); ++i)
    {
        if (backwardEdges[i].mode == mode)
        {
            backwardEdges[i] = edgeData; // Using the same edgeData with same ID
            found            = true;
            break;
        }
    }

    if (!found)
    {
        backwardEdges.append(edgeData); // Using the same edgeData with same ID
    }

    // Calculate terminal costs
    double delay = m_terminalData[startCanonical].handlingTime
                   + m_terminalData[endCanonical].handlingTime;
    double terminalCost = m_terminalData[startCanonical].handlingCost
                          + m_terminalData[endCanonical].handlingCost;

    // Prepare parameters for cost function
    QVariantMap params       = routeAttrs;
    params["terminal_delay"] = delay;
    params["terminal_cost"]  = terminalCost;

    // Compute total cost
    double cost = computeCost(params, m_costFunctionParametersWeights, mode);

    // Add edges to the graph (both directions)
    m_graph.addEdge(startCanonical, endCanonical, cost, mode);
    m_graph.addEdge(endCanonical, startCanonical, cost, mode);

    qDebug() << "Added bidirectional route" << id << "between" << startCanonical
             << "and" << endCanonical << "with mode" << static_cast<int>(mode);
    return {startCanonical, endCanonical};
}

QPair<QString, QString> TerminalGraph::addRoute(const QString     &id,
                                                const QString     &start,
                                                const QString     &end,
                                                TransportationMode mode,
                                                const QVariantMap &attrs)
{
    QMutexLocker locker(&m_mutex);
    return addRouteInternal(id, start, end, mode, attrs);
}

QList<QPair<QString, QString>>
TerminalGraph::addRoutes(const QList<QVariantMap> &routesList)
{
    QMutexLocker                   locker(&m_mutex);
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

        // Add the route
        QPair<QString, QString> route =
            addRouteInternal(id, start, end, mode, attrs);
        addedRoutes.append(route);
    }

    return addedRoutes;
}

Terminal *TerminalGraph::getTerminal(const QString &name) const
{
    QMutexLocker locker(&m_mutex);
    QString      canonical = getCanonicalName(name);
    if (!m_terminals.contains(canonical))
    {
        throw std::invalid_argument("Terminal not found: "
                                    + name.toStdString());
    }
    // Return a pointer to the terminal but with no guarantee about its lifetime
    return m_terminals[canonical];
}

bool TerminalGraph::terminalExists(const QString &name) const
{
    QMutexLocker locker(&m_mutex);
    QString      canonical = getCanonicalName(name);
    return m_terminals.contains(canonical);
}

bool TerminalGraph::removeTerminal(const QString &name)
{
    Terminal *termToDelete = nullptr;
    bool      success      = false;

    {
        QMutexLocker locker(&m_mutex);
        QString      canonical = getCanonicalName(name);
        if (!m_terminals.contains(canonical))
        {
            return false; // Terminal not found
        }

        // Get the terminal to delete
        termToDelete = m_terminals[canonical];

        // Remove aliases
        QSet<QString> aliases = m_canonicalToAliases.value(canonical);
        for (const QString &alias : aliases)
        {
            m_terminalAliases.remove(alias);
        }
        m_canonicalToAliases.remove(canonical);

        // Remove edges connected to this terminal from the edge data
        QList<EdgeIdentifier> edgesToRemove;
        for (auto it = m_edgeData.begin(); it != m_edgeData.end(); ++it)
        {
            if (it.key().from == canonical || it.key().to == canonical)
            {
                edgesToRemove.append(it.key());
            }
        }

        for (const auto &edge : edgesToRemove)
        {
            m_edgeData.remove(edge);
        }

        // Terminal from graph
        // Note: If the vertex doesn't exist, addVertex simply returns false
        // but doesn't throw an exception.
        m_graph.addVertex(canonical);

        // Remove terminal from map (but don't delete yet)
        m_terminals.remove(canonical);

        // Remove node attributes
        m_nodeAttributes.remove(canonical);

        success = true;
    }

    // Delete the terminal after releasing the lock
    delete termToDelete;

    qDebug() << "Removed terminal" << name;
    return success;
}

int TerminalGraph::getTerminalCount() const
{
    QMutexLocker locker(&m_mutex);
    return m_terminals.size();
}

QMap<QString, QStringList>
TerminalGraph::getAllTerminalNames(bool includeAliases) const
{
    QMutexLocker               locker(&m_mutex);
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

void TerminalGraph::clear()
{
    // First collect all terminals that need to be deleted
    QList<Terminal *> terminalsToDelete;

    {
        QMutexLocker locker(&m_mutex);

        // Copy terminals to delete later
        for (auto it = m_terminals.begin(); it != m_terminals.end(); ++it)
        {
            terminalsToDelete.append(it.value());
        }

        // Clear all containers
        m_terminals.clear();
        m_terminalAliases.clear();
        m_canonicalToAliases.clear();
        m_nodeAttributes.clear();
        m_edgeData.clear();

        // Clear the graph
        m_graph = GraphType();
    }

    // Now delete all terminals without holding the lock
    for (Terminal *term : terminalsToDelete)
    {
        delete term;
    }

    qDebug() << "Graph cleared";
}

QVariantMap TerminalGraph::getTerminalStatus(const QString &name) const
{
    if (!name.isEmpty())
    {
        Terminal   *term = nullptr;
        QVariant    region;
        QStringList aliases;

        {
            QMutexLocker locker(&m_mutex);
            QString      canonical = getCanonicalName(name);
            if (!m_terminals.contains(canonical))
            {
                throw std::invalid_argument("Terminal not found");
            }

            term = m_terminals[canonical];

            if (m_nodeAttributes.contains(canonical)
                && m_nodeAttributes[canonical].contains("region"))
            {
                region = m_nodeAttributes[canonical]["region"];
            }

            aliases = m_canonicalToAliases[canonical].values();
        }

        // Call terminal methods without holding the lock
        QVariantMap status;
        status["container_count"]    = term->getContainerCount();
        status["available_capacity"] = term->getAvailableCapacity();
        status["max_capacity"]       = term->getMaxCapacity();
        status["region"]             = region;
        status["aliases"]            = QVariant(aliases);

        return status;
    }
    else
    {
        // Copy data while locked
        QMap<QString, Terminal *>  termsCopy;
        QMap<QString, QStringList> aliasesCopy;
        QMap<QString, QVariant>    regionsCopy;

        {
            QMutexLocker locker(&m_mutex);

            // Create copies of the data we need
            for (auto it = m_terminals.begin(); it != m_terminals.end(); ++it)
            {
                QString canonical    = it.key();
                termsCopy[canonical] = it.value();

                if (m_nodeAttributes.contains(canonical)
                    && m_nodeAttributes[canonical].contains("region"))
                {
                    regionsCopy[canonical] =
                        m_nodeAttributes[canonical]["region"];
                }

                aliasesCopy[canonical] =
                    m_canonicalToAliases[canonical].values();
            }
        }

        // Process without holding the lock
        QVariantMap result;
        for (auto it = termsCopy.begin(); it != termsCopy.end(); ++it)
        {
            QString   canonical = it.key();
            Terminal *term      = it.value();

            QVariantMap status;
            status["container_count"]    = term->getContainerCount();
            status["available_capacity"] = term->getAvailableCapacity();
            status["max_capacity"]       = term->getMaxCapacity();
            status["region"]             = regionsCopy.value(canonical);
            status["aliases"]            = QVariant(aliasesCopy[canonical]);

            result[canonical] = status;
        }

        return result;
    }
}

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

        double weight     = modeWeights.value(it.key(), 1.0).toDouble();
        double factorCost = weight * value;
        cost += factorCost;
    }

    return cost;
}

void TerminalGraph::buildPathSegment(PathSegment &segment, bool isStart,
                                     bool isEnd, bool skipStartTerminal,
                                     bool skipEndTerminal, const QString &from,
                                     const QString &to, TransportationMode mode,
                                     const QVariantMap &attributes) const
{
    segment.from           = from;
    segment.to             = to;
    segment.fromTerminalId = from;
    segment.toTerminalId   = to;
    segment.mode           = mode;
    // segment.attributes     = attributes;

    // Get terminal data safely
    double fromHandlingTime = 0.0;
    double toHandlingTime   = 0.0;
    double fromCost         = 0.0;
    double toCost           = 0.0;

    {
        QMutexLocker locker(&m_mutex);
        fromHandlingTime = m_terminalData[from].handlingTime;
        toHandlingTime   = m_terminalData[to].handlingTime;
        fromCost         = m_terminalData[from].handlingCost;
        toCost           = m_terminalData[to].handlingCost;
    }

    // Store estimated raw values
    segment.estimatedValues                          = attributes;
    segment.estimatedValues["previousTerminalDelay"] = fromHandlingTime;
    segment.estimatedValues["previousTerminalCost"]  = fromCost;
    segment.estimatedValues["nextTerminalDelay"]     = toHandlingTime;
    segment.estimatedValues["nextTerminalCost"]      = toCost;

    // Prepare complete params for cost computation
    QVariantMap params       = attributes;
    params["terminal_delay"] = fromHandlingTime + toHandlingTime;
    params["terminal_cost"]  = fromCost + toCost;

    // Get cost function weights safely
    QVariantMap costFunctionWeights;
    {
        QMutexLocker locker(&m_mutex);
        costFunctionWeights = m_costFunctionParametersWeights;
    }

    // Compute total cost
    double totalWeight = 0.0;
    // segment.weight =
    //     computeCost(params, costFunctionWeights, mode);

    // Calculate detailed cost breakdown
    double carbonEmissions =
        computeCost({{"carbonEmissions", params["carbonEmissions"]}},
                    costFunctionWeights, mode);
    segment.estimatedCost["carbonEmissions"] = carbonEmissions;
    totalWeight += carbonEmissions;

    double directCost =
        computeCost({{"cost", params["cost"]}}, costFunctionWeights, mode);
    segment.estimatedCost["cost"] = directCost;
    totalWeight += directCost;

    double distance = computeCost({{"distance", params["distance"]}},
                                  costFunctionWeights, mode);
    segment.estimatedCost["distance"] = distance;
    totalWeight += distance;

    double energyConsumption =
        computeCost({{"energyConsumption", params["energyConsumption"]}},
                    costFunctionWeights, mode);
    segment.estimatedCost["energyConsumption"] = energyConsumption;
    totalWeight += energyConsumption;

    double risk =
        computeCost({{"risk", params["risk"]}}, costFunctionWeights, mode);
    segment.estimatedCost["risk"] = risk;
    totalWeight += risk;

    double travelTime = computeCost({{"travelTime", params["travelTime"]}},
                                    costFunctionWeights, mode);
    segment.estimatedCost["travelTime"] = travelTime;
    totalWeight += travelTime;

    double previousTerminalDelay = 0.0;
    if (!skipStartTerminal)
    {
        previousTerminalDelay = computeCost(
            {{"terminal_delay", fromHandlingTime}}, costFunctionWeights, mode);
    }
    previousTerminalDelay =
        isStart ? previousTerminalDelay : previousTerminalDelay / 2;
    totalWeight += previousTerminalDelay;
    segment.estimatedCost["previousTerminalDelay"] = previousTerminalDelay;

    double previousTerminalCost = 0.0;
    if (!skipStartTerminal)
    {
        previousTerminalCost = computeCost({{"terminal_cost", fromCost}},
                                           costFunctionWeights, mode);
    }
    previousTerminalCost =
        isStart ? previousTerminalCost : previousTerminalCost / 2;
    segment.estimatedCost["previousTerminalCost"] = previousTerminalCost;
    totalWeight += previousTerminalCost;

    double nextTerminalDelay = 0.0;
    if (!skipEndTerminal)
    {
        nextTerminalDelay = computeCost({{"terminal_delay", toHandlingTime}},
                                        costFunctionWeights, mode);
    }
    nextTerminalDelay = isEnd ? nextTerminalDelay : nextTerminalDelay / 2;
    segment.estimatedCost["nextTerminalDelay"] = nextTerminalDelay;
    totalWeight += nextTerminalDelay;

    double nextTerminalCost = 0.0;
    if (!skipEndTerminal)
    {
        nextTerminalCost =
            computeCost({{"terminal_cost", toCost}}, costFunctionWeights, mode);
    }
    nextTerminalCost = isEnd ? nextTerminalCost : nextTerminalCost / 2;
    segment.estimatedCost["nextTerminalCost"] = nextTerminalCost;
    totalWeight += nextTerminalCost;

    segment.weight = totalWeight;
}

void TerminalGraph::updateGraph(TransportationMode requestedMode)
{
    // Create a new graph
    GraphType newGraph;

    // Copy necessary data while holding the lock
    QHash<QString, Terminal *>                      terminalsCopy;
    QHash<EdgeIdentifier, QList<EdgeData>>          edgeDataCopy;
    QHash<QString, TerminalDetails>                 terminalData;
    QVariantMap costFunctionParamsWeightsCopy;

    {
        QMutexLocker locker(&m_mutex);

        // Make copies of all needed data
        terminalsCopy                 = m_terminals;
        edgeDataCopy                  = m_edgeData;
        terminalData                  = m_terminalData;
        costFunctionParamsWeightsCopy = m_costFunctionParametersWeights;
    }

    // Now build the graph without holding the lock

    // First step - add all vertices
    for (auto it = terminalsCopy.begin(); it != terminalsCopy.end(); ++it)
    {
        newGraph.addVertex(it.key());
    }

    // Second step - add all edges
    for (auto it = edgeDataCopy.begin(); it != edgeDataCopy.end(); ++it)
    {
        QString startName = it.key().from;
        QString endName   = it.key().to;

        // Get all edges between these vertices
        const QList<EdgeData> &edges = it.value();

        for (const EdgeData &edgeData : edges)
        {
            // Skip edges that don't match the requested mode
            if (requestedMode != TransportationMode::Any
                && edgeData.mode != requestedMode)
            {
                continue;
            }

            // Calculate terminal costs without holding locks
            double delay = terminalData[startName].handlingTime
                           + terminalData[endName].handlingTime;
            double terminalCost = terminalData[startName].handlingCost
                                  + terminalData[endName].handlingCost;

            // Prepare parameters for cost function
            QVariantMap params       = edgeData.attributes;
            params["terminal_delay"] = delay;
            params["terminal_cost"]  = terminalCost;

            // Compute total cost
            double cost = computeCost(params, costFunctionParamsWeightsCopy,
                                      edgeData.mode);

            // Add edge to the graph
            newGraph.addEdge(startName, endName, cost, edgeData.mode);
        }
    }

    // Update the graph pointer under lock
    {
        QMutexLocker locker(&m_mutex);
        m_graph = newGraph;
    }
}

Path TerminalGraph::convertEdgePathToTerminalPath(
    const EdgePathInfoType &pathInfo, int pathId,
    TransportationMode requestedMode, bool skipDelays) const
{
    Path path;
    path.pathId             = pathId;
    path.totalEdgeCosts     = 0;
    path.totalTerminalCosts = 0;
    path.totalPathCost      = 0;
    path.costBreakdown      = QVariantMap();

    // Initialize cost breakdown categories
    QStringList costCategories = {
        "carbonEmissions",   "cost",         "distance",
        "energyConsumption", "risk",         "travelTime",
        "terminal_delay",    "terminal_cost"};
    for (const QString &category : costCategories)
    {
        path.costBreakdown[category] = 0.0;
    }

    // Extract edges from path
    const auto &edges = pathInfo.first;

    // Skip if path is empty
    if (edges.empty())
    {
        return path;
    }

    // Copy necessary data while holding the lock
    QHash<EdgeIdentifier, QList<EdgeData>>          edgeDataCopy;
    QHash<QString, Terminal *>                      terminalsCopy;
    QHash<QString, TerminalDetails>                 terminalData;

    {
        QMutexLocker locker(&m_mutex);
        edgeDataCopy  = m_edgeData;
        terminalsCopy = m_terminals;
        terminalData  = m_terminalData;
    }

    // Process segments without the lock
    QList<QVariantMap> terminalsInPath;

    // Add first terminal (source of first edge)
    QString firstTerminalName = edges[0].source();

    double firstHandlingTime = terminalData[firstTerminalName].handlingTime;
    double firstCost         = terminalData[firstTerminalName].handlingCost;

    QVariantMap terminalInfo;
    terminalInfo["terminal"]      = firstTerminalName;
    terminalInfo["handling_time"] = firstHandlingTime;
    terminalInfo["cost"]          = firstCost;
    terminalInfo["costs_skipped"] =
        true; // Origin terminal costs skipped by default

    terminalsInPath.append(terminalInfo);

    // Process each edge in the path
    for (size_t i = 0; i < edges.size(); ++i)
    {
        const auto &edge     = edges[i];
        QString     fromName = edge.source();
        QString     toName   = edge.target();
        TransportationMode mode     = edge.mode();

        bool skipNextTerminalCost     = false;
        bool skipPreviousTerminalCost = false;

        if (i < edges.size() - 1)
        {
            const auto        &nextEdge = edges[i + 1];
            TransportationMode nextMode = nextEdge.mode();
            skipNextTerminalCost        = (mode == nextMode);
        }
        if (i > 0)
        {
            const auto        &prevEdge = edges[i + 1];
            TransportationMode prevMode = prevEdge.mode();
            skipPreviousTerminalCost    = (mode == prevMode);
        }

        // Find the edge data
        EdgeIdentifier edgeKey(fromName, toName, mode);

        if (!edgeDataCopy.contains(edgeKey))
        {
            qWarning() << "Edge data not found for path segment" << fromName
                       << "->" << toName;
            continue;
        }

        // Find the matching edge (based on requested mode)
        const QList<EdgeData> &edgesData = edgeDataCopy[edgeKey];
        EdgeData               edgeData;
        bool                   found = false;

        for (const EdgeData &data : edgesData)
        {
            if (requestedMode == TransportationMode::Any
                || data.mode == requestedMode)
            {
                edgeData = data;
                found    = true;
                break;
            }
        }

        if (!found)
        {
            qWarning() << "No matching edge found for mode"
                       << static_cast<int>(requestedMode);
            continue;
        }

        bool isStart = (i == 0);
        bool isEnd   = (i == edges.size() - 1);
        if (isStart)
        {
            skipPreviousTerminalCost = true;
        }
        if (isEnd)
        {
            skipNextTerminalCost = true;
        }
        // Create path segment
        PathSegment segment;
        buildPathSegment(segment, isStart, isEnd, skipPreviousTerminalCost,
                         skipNextTerminalCost, fromName, toName, edgeData.mode,
                         edgeData.attributes);
        path.segments.append(segment);

        // Add to total costs
        path.totalEdgeCosts += segment.weight;

        // Add cost breakdown
        for (auto it = segment.estimatedCost.begin();
             it != segment.estimatedCost.end(); ++it)
        {
            QString category = it.key();
            double  value    = it.value().toDouble();

            if (path.costBreakdown.contains(category))
            {
                path.costBreakdown[category] =
                    path.costBreakdown[category].toDouble() + value;
            }
            else
            {
                path.costBreakdown[category] = value;
            }
        }

        // Add destination terminal info
        double handlingTime = terminalData[toName].handlingTime;
        double cost         = terminalData[toName].handlingCost;

        QVariantMap terminalInfo;
        terminalInfo["terminal"]      = toName;
        terminalInfo["handling_time"] = handlingTime;
        terminalInfo["cost"]          = cost;

        // Determine if costs should be skipped
        bool skipCosts = false;
        if (skipDelays && i > 0)
        {
            // Skip costs if consecutive segments use the same mode
            skipCosts = (path.segments[i].mode == path.segments[i - 1].mode);
        }

        terminalInfo["costs_skipped"] = skipCosts;
        terminalsInPath.append(terminalInfo);

        // Add terminal costs if not skipped
        if (!skipCosts)
        {
            path.totalTerminalCosts += cost;
        }
    }

    path.terminalsInPath = terminalsInPath;
    path.totalPathCost   = path.totalEdgeCosts; // + path.totalTerminalCosts;

    return path;
}

QList<PathSegment> TerminalGraph::findShortestPath(const QString     &start,
                                                   const QString     &end,
                                                   TransportationMode mode)
{
    QString startCanonical;
    QString endCanonical;

    {
        QMutexLocker locker(&m_mutex);
        startCanonical = getCanonicalName(start);
        endCanonical   = getCanonicalName(end);

        if (!m_terminals.contains(startCanonical)
            || !m_terminals.contains(endCanonical))
        {
            throw std::invalid_argument("Terminal not found");
        }
    }

    // Update graph with the current network and mode
    updateGraph(mode);

    // Use the GraphAlgorithms to find shortest path
    auto shortestPathOpt = GraphAlgorithmsType::dijkstraShortestPath(
        m_graph, startCanonical, endCanonical, mode);

    // Check if path exists
    if (!shortestPathOpt.has_value())
    {
        throw std::runtime_error("No path found");
    }

    // Convert to TerminalSim Path
    Path terminalPath =
        convertEdgePathToTerminalPath(shortestPathOpt.value(), 1, mode, false);

    return terminalPath.segments;
}

QList<Path> TerminalGraph::findTopNShortestPaths(const QString &start,
                                                 const QString &end, int n,
                                                 TransportationMode mode,
                                                 bool               skipDelays)
{
    // Return early for invalid input
    if (n <= 0)
    {
        qDebug() << "Invalid request: n must be positive";
        return QList<Path>();
    }

    QString startCanonical;
    QString endCanonical;

    {
        QMutexLocker locker(&m_mutex);
        startCanonical = getCanonicalName(start);
        endCanonical   = getCanonicalName(end);

        if (!m_terminals.contains(startCanonical)
            || !m_terminals.contains(endCanonical))
        {
            qDebug() << "Terminal not found: start=" << startCanonical
                     << " end=" << endCanonical;
            return QList<Path>();
        }
    }

    // Update graph with the current network and mode
    updateGraph(mode);

    // Use the GraphAlgorithms to find k shortest paths
    auto kPaths = GraphAlgorithmsType::kShortestPathsModified(
        m_graph, startCanonical, endCanonical, n, mode);

    // Convert paths to TerminalSim Paths
    QVector<Path> result;
    QSet<QString> uniquePathSignatures;

    for (size_t i = 0; i < kPaths.size(); ++i)
    {
        Path path =
            convertEdgePathToTerminalPath(kPaths[i], i + 1, mode, skipDelays);

        // Create a signature for this path
        QString pathSignature;
        for (const PathSegment &segment : path.segments)
        {
            pathSignature += segment.from + "->" + segment.to + ":"
                             + QString::number(static_cast<int>(segment.mode))
                             + "|";
        }

        // Only add if this path signature is unique
        if (!uniquePathSignatures.contains(pathSignature))
        {
            uniquePathSignatures.insert(pathSignature);
            result.append(path);
        }
    }

    // Resort and reassign path IDs
    std::sort(result.begin(), result.end(), [](const Path &a, const Path &b) {
        return a.totalPathCost < b.totalPathCost;
    });

    for (int i = 0; i < result.size(); i++)
    {
        result[i].pathId = i + 1;
    }

    qDebug() << "Found" << result.size() << "paths from" << startCanonical
             << "to" << endCanonical;
    return result.toList();
}

QString TerminalGraph::getCanonicalName(const QString &name) const
{
    return m_terminalAliases.value(name, name);
}

} // namespace TerminalSim
