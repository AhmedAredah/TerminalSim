#include "terminal_graph.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPair>
#include <QQueue>
#include <QRandomGenerator>
#include <QCryptographicHash>
#include <QSet>
#include <QThread>
#include <QUrl>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "common/LogCategories.h"

namespace TerminalSim
{

namespace {

QString percentEncode(const QString &value)
{
    return QString::fromUtf8(QUrl::toPercentEncoding(value));
}

QString canonicalSegmentSignature(const QList<PathSegment> &segments)
{
    QString signature;
    for (const PathSegment &segment : segments)
    {
        signature += segment.from + "->" + segment.to + ":"
                     + QString::number(static_cast<int>(segment.mode)) + "|";
    }
    return signature;
}

QStringList routeAttributeKeys()
{
    return {QStringLiteral("cost"),
            QStringLiteral("travelTime"),
            QStringLiteral("distance"),
            QStringLiteral("carbonEmissions"),
            QStringLiteral("risk"),
            QStringLiteral("energyConsumption")};
}

QStringList costAttributeKeys()
{
    QStringList keys = routeAttributeKeys();
    keys << QStringLiteral("terminal_delay")
         << QStringLiteral("terminal_cost");
    return keys;
}

QVariantMap defaultLinkAttributes()
{
    return {{QStringLiteral("cost"), 1.0},
            {QStringLiteral("travelTime"), 1.0},
            {QStringLiteral("distance"), 1.0},
            {QStringLiteral("carbonEmissions"), 1.0},
            {QStringLiteral("risk"), 1.0},
            {QStringLiteral("energyConsumption"), 1.0}};
}

QVariantMap defaultCostFunctionParameters()
{
    const QVariantMap weights{
        {QStringLiteral("cost"), 1.0},
        {QStringLiteral("travelTime"), 1.0},
        {QStringLiteral("distance"), 1.0},
        {QStringLiteral("carbonEmissions"), 1.0},
        {QStringLiteral("risk"), 1.0},
        {QStringLiteral("energyConsumption"), 1.0},
        {QStringLiteral("terminal_delay"), 1.0},
        {QStringLiteral("terminal_cost"), 1.0}};

    return {{QStringLiteral("default"), weights},
            {QString::number(static_cast<int>(TransportationMode::Ship)), weights},
            {QString::number(static_cast<int>(TransportationMode::Train)), weights},
            {QString::number(static_cast<int>(TransportationMode::Truck)), weights}};
}

double numericAttributeValue(const QVariant &value, const QString &context)
{
    bool ok = false;
    const double parsed = value.toDouble(&ok);
    if (!ok || !std::isfinite(parsed))
    {
        throw std::invalid_argument(
            QString("Invalid numeric value for %1").arg(context).toStdString());
    }
    return parsed;
}

void validateNonNegative(double value, const QString &context)
{
    if (value < 0.0)
    {
        throw std::invalid_argument(
            QString("Negative value is not allowed for %1")
                .arg(context)
                .toStdString());
    }
}

QVariantMap validatedAttributeMap(const QVariantMap &attrs,
                                  const QStringList &allowedKeys,
                                  const QStringList &requiredKeys,
                                  const QString     &context)
{
    const QSet<QString> allowed(allowedKeys.cbegin(), allowedKeys.cend());
    QVariantMap validated;

    for (auto it = attrs.constBegin(); it != attrs.constEnd(); ++it)
    {
        if (!allowed.contains(it.key()))
        {
            throw std::invalid_argument(
                QString("Unknown numeric attribute '%1' in %2")
                    .arg(it.key(), context)
                    .toStdString());
        }

        const double value =
            numericAttributeValue(it.value(), context + "." + it.key());
        validateNonNegative(value, context + "." + it.key());
        validated.insert(it.key(), value);
    }

    for (const QString &key : requiredKeys)
    {
        if (!validated.contains(key))
        {
            throw std::invalid_argument(
                QString("Missing required attribute '%1' in %2")
                    .arg(key, context)
                    .toStdString());
        }
    }

    return validated;
}

QVariantMap validatedRouteAttributes(const QVariantMap &attrs,
                                     const QVariantMap &defaults,
                                     const QString     &context)
{
    QVariantMap merged = validatedAttributeMap(
        defaults, routeAttributeKeys(), routeAttributeKeys(),
        context + QStringLiteral(".defaults"));

    const QVariantMap overrides = validatedAttributeMap(
        attrs, routeAttributeKeys(), {}, context);
    for (auto it = overrides.constBegin(); it != overrides.constEnd(); ++it)
        merged[it.key()] = it.value();

    return merged;
}

void validateCostFunctionParameters(const QVariantMap &params)
{
    const QStringList requiredModes = {
        QStringLiteral("default"),
        QString::number(static_cast<int>(TransportationMode::Ship)),
        QString::number(static_cast<int>(TransportationMode::Train)),
        QString::number(static_cast<int>(TransportationMode::Truck))};

    for (auto it = params.constBegin(); it != params.constEnd(); ++it)
    {
        if (!requiredModes.contains(it.key()))
        {
            throw std::invalid_argument(
                QString("Unknown cost-function mode key: %1")
                    .arg(it.key())
                    .toStdString());
        }
    }

    for (const QString &mode : requiredModes)
    {
        if (!params.contains(mode) || !params.value(mode).canConvert<QVariantMap>())
        {
            throw std::invalid_argument(
                QString("Missing cost-function parameters for mode: %1")
                    .arg(mode)
                    .toStdString());
        }

        validatedAttributeMap(params.value(mode).toMap(),
                              costAttributeKeys(),
                              costAttributeKeys(),
                              QStringLiteral("cost_function[%1]").arg(mode));
    }
}

bool isConcreteMode(TransportationMode mode)
{
    return mode == TransportationMode::Ship
        || mode == TransportationMode::Truck
        || mode == TransportationMode::Train;
}

TransportationMode parseModeVariant(const QVariant &value,
                                    const QString  &context,
                                    bool            allowAny)
{
    TransportationMode mode;
    if (value.typeId() == QMetaType::QString)
    {
        mode = EnumUtils::parseTransportationMode(value.toString());
    }
    else if (value.canConvert<int>())
    {
        mode = EnumUtils::parseTransportationMode(
            QString::number(value.toInt()));
    }
    else
    {
        throw std::invalid_argument(
            QString("Invalid transportation mode for %1")
                .arg(context)
                .toStdString());
    }

    if (!allowAny && !isConcreteMode(mode))
    {
        throw std::invalid_argument(
            QString("Concrete transportation mode required for %1")
                .arg(context)
                .toStdString());
    }
    return mode;
}

TerminalInterface parseInterfaceKey(const QString &key,
                                    const QString &context)
{
    try
    {
        return EnumUtils::parseTerminalInterface(key);
    }
    catch (const std::invalid_argument &)
    {
        throw std::invalid_argument(
            QString("Invalid terminal interface '%1' in %2")
                .arg(key, context)
                .toStdString());
    }
}

bool interfaceSupportsMode(TerminalInterface interface,
                           TransportationMode mode)
{
    switch (mode)
    {
        case TransportationMode::Truck:
        case TransportationMode::Train:
            return interface == TerminalInterface::LAND_SIDE;
        case TransportationMode::Ship:
            return interface == TerminalInterface::SEA_SIDE;
        case TransportationMode::Any:
            return false;
    }
    return false;
}

QMap<TerminalInterface, QSet<TransportationMode>>
parseTerminalInterfaces(const QVariantMap &interfacesMap,
                        const QString     &context)
{
    if (interfacesMap.isEmpty())
    {
        throw std::invalid_argument(
            QString("At least one terminal interface must be provided for %1")
                .arg(context)
                .toStdString());
    }

    QMap<TerminalInterface, QSet<TransportationMode>> interfaces;
    for (auto it = interfacesMap.constBegin(); it != interfacesMap.constEnd();
         ++it)
    {
        const TerminalInterface interface =
            parseInterfaceKey(it.key(), context);
        const QVariantList modesList = it.value().toList();
        if (modesList.isEmpty())
        {
            throw std::invalid_argument(
                QString("Interface %1 has no modes in %2")
                    .arg(it.key(), context)
                    .toStdString());
        }

        QSet<TransportationMode> modes;
        for (const QVariant &modeVar : modesList)
        {
            const TransportationMode mode =
                parseModeVariant(modeVar, context, false);
            if (!interfaceSupportsMode(interface, mode))
            {
                throw std::invalid_argument(
                    QString("Mode %1 is not compatible with interface %2 in %3")
                        .arg(EnumUtils::transportationModeToString(mode),
                             EnumUtils::terminalInterfaceToString(interface),
                             context)
                        .toStdString());
            }
            modes.insert(mode);
        }

        interfaces[interface] = modes;
    }

    return interfaces;
}

QStringList parseTerminalNames(const QVariant &namesVar,
                               const QString  &context)
{
    QStringList names;
    if (namesVar.typeId() == QMetaType::QString)
    {
        names << namesVar.toString();
    }
    else if (namesVar.canConvert<QStringList>())
    {
        names = namesVar.toStringList();
    }
    else if (namesVar.canConvert<QVariantList>())
    {
        for (const QVariant &nameVar : namesVar.toList())
        {
            const QString name = nameVar.toString().trimmed();
            if (!name.isEmpty())
                names << name;
        }
    }
    else
    {
        throw std::invalid_argument(
            QString("terminal_names must be a string or list in %1")
                .arg(context)
                .toStdString());
    }

    names.removeDuplicates();
    if (names.isEmpty())
    {
        throw std::invalid_argument(
            QString("At least one terminal name must be provided in %1")
                .arg(context)
                .toStdString());
    }
    return names;
}

bool terminalSupportsMode(const Terminal *terminal, TransportationMode mode)
{
    if (!terminal || !isConcreteMode(mode))
        return false;

    const auto interfaces = terminal->getInterfaces();
    for (auto it = interfaces.constBegin(); it != interfaces.constEnd(); ++it)
    {
        if (it.value().contains(mode) && interfaceSupportsMode(it.key(), mode))
            return true;
    }
    return false;
}

QString buildPathUid(const Path &path)
{
    const QString policySignature =
        path.skipSameModeTerminalDelaysAndCosts
            ? QStringLiteral("legacy-same-mode-skip:on")
            : QStringLiteral("legacy-same-mode-skip:off");
    const QByteArray signatureHash =
        QCryptographicHash::hash(
            (canonicalSegmentSignature(path.segments)
             + QStringLiteral("|policy=") + policySignature).toUtf8(),
            QCryptographicHash::Sha256)
            .toHex();

    return QStringLiteral(
               "pf|v2|start=%1|end=%2|mode=%3|policy=%4|sig=%5")
        .arg(percentEncode(path.startTerminal),
             percentEncode(path.endTerminal),
             QString::number(path.requestedMode),
             percentEncode(policySignature),
             QString::fromLatin1(signatureHash));
}

double estimatedCostValue(const PathSegment &segment, const char *key)
{
    return segment.estimatedCost.value(QString::fromLatin1(key)).toDouble();
}

} // namespace

TerminalGraph::TerminalGraph(const QString &dir)
    : QObject(nullptr)
    , m_pathToTerminalsDirectory(dir)
{
    m_costFunctionParametersWeights = defaultCostFunctionParameters();
    m_defaultLinkAttributes = defaultLinkAttributes();

    qCInfo(lcTerminalGraph) << "Graph initialized with dir:" << (dir.isEmpty() ? "None" : dir);
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

    qCDebug(lcTerminalGraph) << "Graph destroyed";
}

void TerminalGraph::setCostFunctionParameters(const QVariantMap &params)
{
    validateCostFunctionParameters(params);
    QMutexLocker locker(&m_mutex);
    m_costFunctionParametersWeights = params;
}

void TerminalGraph::setLinkDefaultAttributes(const QVariantMap &attrs)
{
    const QVariantMap validated = validatedAttributeMap(
        attrs, routeAttributeKeys(), routeAttributeKeys(),
        QStringLiteral("default_link_attributes"));
    QMutexLocker locker(&m_mutex);   // Ensure thread safety
    m_defaultLinkAttributes = validated; // Update attributes
}

void TerminalGraph::resetConfigurationToDefaults()
{
    QMutexLocker locker(&m_mutex);
    m_costFunctionParametersWeights = defaultCostFunctionParameters();
    m_defaultLinkAttributes = defaultLinkAttributes();
}

Terminal *TerminalGraph::addTerminalInternal(const QVariantMap &terminalData)
{
    const QStringList terminalNames = parseTerminalNames(
        terminalData.value(QStringLiteral("terminal_names")),
        QStringLiteral("terminal"));

    QString     canonical    = terminalNames.first();
    QString     displayName  = terminalData["display_name"].toString();
    QVariantMap customConfig = terminalData["custom_config"].toMap();
    QString     region = terminalData.value("region", QString()).toString();

    const auto interfaces = parseTerminalInterfaces(
        terminalData.value(QStringLiteral("terminal_interfaces")).toMap(),
        canonical);

    // Parse mode network aliases from custom config
    auto modeNetworkAliases = Terminal::parseModeNetworkAliases(
        customConfig.value("mode_network_aliases").toMap());

    // Create terminal
    Terminal *term = new Terminal(
        canonical, displayName, interfaces, modeNetworkAliases,
        customConfig.value("capacity").toMap(),
        customConfig.value("dwell_time").toMap(),
        customConfig.value("customs").toMap(),
        customConfig.value("cost").toMap(),
        customConfig.value("system_dynamics").toMap(),
        m_pathToTerminalsDirectory);

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

    qCDebug(lcTerminalGraph) << "Added terminal" << canonical << "with"
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

    const QStringList terminalNames = parseTerminalNames(
        terminalData.value(QStringLiteral("terminal_names")),
        QStringLiteral("terminal"));

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

    parseTerminalInterfaces(
        terminalData.value(QStringLiteral("terminal_interfaces")).toMap(),
        canonical);

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

        const QStringList terminalNames = parseTerminalNames(
            terminalData.value(QStringLiteral("terminal_names")),
            QStringLiteral("terminal"));

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
            if (m_terminalAliases.contains(name))
            {
                throw std::invalid_argument("Duplicate terminal name: "
                                            + name.toStdString());
            }
            if (allNames.contains(name))
            {
                throw std::invalid_argument("Duplicate terminal name: "
                                            + name.toStdString());
            }
            allNames.insert(name);
        }

        parseTerminalInterfaces(
            terminalData.value(QStringLiteral("terminal_interfaces")).toMap(),
            canonical);
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
    qCDebug(lcTerminalGraph) << "Added alias" << alias << "to" << canonical;
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
    if (!isConcreteMode(mode))
    {
        throw std::invalid_argument(
            QString("Route %1 requires a concrete transportation mode")
                .arg(id)
                .toStdString());
    }

    QString startCanonical = m_terminalAliases.value(start, start);
    QString endCanonical   = m_terminalAliases.value(end, end);

    if (!m_terminals.contains(startCanonical)
        || !m_terminals.contains(endCanonical))
    {
        throw std::invalid_argument("Terminal not found");
    }

    if (!terminalSupportsMode(m_terminals.value(startCanonical), mode)
        || !terminalSupportsMode(m_terminals.value(endCanonical), mode))
    {
        throw std::invalid_argument(
            QString("Route %1 uses mode %2 but one or both endpoint terminals "
                    "do not support that mode/interface")
                .arg(id, EnumUtils::transportationModeToString(mode))
                .toStdString());
    }

    const QVariantMap routeAttrs = validatedRouteAttributes(
        attrs, m_defaultLinkAttributes,
        QStringLiteral("route '%1'").arg(id));

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
    params["terminal_delay"] = delay;           // seconds; sum of both endpoints' handlingTime
    params["terminal_cost"]  = terminalCost;    // USD per container

    // Compute total cost
    double cost = computeCost(params, m_costFunctionParametersWeights, mode);

    // Add edges to the graph (both directions)
    m_graph.addEdge(startCanonical, endCanonical, cost, mode);
    m_graph.addEdge(endCanonical, startCanonical, cost, mode);

    qCDebug(lcTerminalGraph) << "Added bidirectional route" << id << "between" << startCanonical
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
    struct ValidatedRoute
    {
        QString            id;
        QString            start;
        QString            end;
        TransportationMode mode;
        QVariantMap        attrs;
    };
    QList<ValidatedRoute> validatedRoutes;

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
        if (id.trimmed().isEmpty() || start.trimmed().isEmpty()
            || end.trimmed().isEmpty())
        {
            throw std::invalid_argument(
                "Route id, start terminal, and end terminal must be provided");
        }

        QString startCanonical = getCanonicalName(start);
        QString endCanonical   = getCanonicalName(end);

        if (!m_terminals.contains(startCanonical)
            || !m_terminals.contains(endCanonical))
        {
            throw std::invalid_argument("Terminal not found for route ID: "
                                        + id.toStdString());
        }

        // Parse mode
        const TransportationMode mode = parseModeVariant(
            routeData.value(QStringLiteral("mode")),
            QStringLiteral("route '%1'").arg(id),
            false);

        if (!terminalSupportsMode(m_terminals.value(startCanonical), mode)
            || !terminalSupportsMode(m_terminals.value(endCanonical), mode))
        {
            throw std::invalid_argument(
                QString("Route %1 uses mode %2 but one or both endpoint "
                        "terminals do not support that mode/interface")
                    .arg(id, EnumUtils::transportationModeToString(mode))
                    .toStdString());
        }

        // Get attributes
        QVariantMap attrs;
        if (routeData.contains("attributes")
            && routeData["attributes"].canConvert<QVariantMap>())
        {
            attrs = routeData["attributes"].toMap();
        }
        const QVariantMap validatedAttrs = validatedRouteAttributes(
            attrs, m_defaultLinkAttributes,
            QStringLiteral("route '%1'").arg(id));

        validatedRoutes.append(
            ValidatedRoute{id, start, end, mode, validatedAttrs});
    }

    // Add all routes after complete validation so a bad batch leaves topology
    // unchanged.
    for (const ValidatedRoute &routeData : validatedRoutes)
    {
        // Add the route
        QPair<QString, QString> route =
            addRouteInternal(routeData.id, routeData.start, routeData.end,
                             routeData.mode, routeData.attrs);
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

        m_graph.removeVertex(canonical);

        // Remove terminal from map (but don't delete yet)
        m_terminals.remove(canonical);

        // Remove node attributes
        m_nodeAttributes.remove(canonical);
        m_terminalData.remove(canonical);

        success = true;
    }

    // Delete the terminal after releasing the lock
    delete termToDelete;

    qCDebug(lcTerminalGraph) << "Removed terminal" << name;
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
        m_terminalData.clear();

        // Clear the graph
        m_graph = GraphType();
    }

    // Now delete all terminals without holding the lock
    for (Terminal *term : terminalsToDelete)
    {
        delete term;
    }

    qCDebug(lcTerminalGraph) << "Graph cleared";
}

int TerminalGraph::resetRuntimeState(const QStringList &terminalIds)
{
    QList<Terminal *> terminalsToReset;
    QStringList       resolvedTerminalIds;

    {
        QMutexLocker locker(&m_mutex);

        if (terminalIds.isEmpty())
        {
            for (auto it = m_terminals.constBegin();
                 it != m_terminals.constEnd(); ++it)
            {
                terminalsToReset.append(it.value());
                resolvedTerminalIds.append(it.key());
            }
        }
        else
        {
            QSet<QString> seen;
            for (const QString &terminalId : terminalIds)
            {
                const QString canonical =
                    getCanonicalName(terminalId.trimmed());
                if (canonical.isEmpty())
                    continue;
                if (!m_terminals.contains(canonical))
                {
                    throw std::invalid_argument(
                        QString("Terminal not found: %1")
                            .arg(terminalId)
                            .toStdString());
                }
                if (seen.contains(canonical))
                    continue;

                seen.insert(canonical);
                terminalsToReset.append(m_terminals.value(canonical));
                resolvedTerminalIds.append(canonical);
            }
        }
    }

    for (Terminal *terminal : terminalsToReset)
    {
        if (terminal)
            terminal->resetRuntimeState();
    }

    qCInfo(lcTerminalGraph) << "Runtime state reset for terminals:"
                            << resolvedTerminalIds;
    return terminalsToReset.size();
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
        if (!costAttributeKeys().contains(it.key()))
        {
            throw std::invalid_argument(
                QString("Unknown cost attribute in cost computation: %1")
                    .arg(it.key())
                    .toStdString());
        }

        if (!modeWeights.contains(it.key()))
        {
            throw std::invalid_argument(
                QString("Missing cost-function weight for attribute: %1")
                    .arg(it.key())
                    .toStdString());
        }

        const double value = numericAttributeValue(
            it.value(), QStringLiteral("cost.%1").arg(it.key()));
        validateNonNegative(value,
                            QStringLiteral("cost.%1").arg(it.key()));
        const double weight = numericAttributeValue(
            modeWeights.value(it.key()),
            QStringLiteral("weight.%1").arg(it.key()));
        validateNonNegative(weight,
                            QStringLiteral("weight.%1").arg(it.key()));
        double factorCost = weight * value;
        cost += factorCost;
    }

    return cost;
}

void TerminalGraph::buildPathSegment(PathSegment &segment, int sequenceIndex,
                                     bool isStart, bool isEnd,
                                     bool skipStartTerminal,
                                     bool skipEndTerminal,
                                     const QString &from, const QString &to,
                                     TransportationMode mode,
                                     const QVariantMap &attributes) const
{
    segment.from           = from;
    segment.to             = to;
    segment.fromTerminalId = from;
    segment.toTerminalId   = to;
    segment.mode           = mode;
    segment.sequenceIndex  = sequenceIndex;

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
    segment.estimatedValues = attributes;

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

    // Calculate detailed cost breakdown
    double carbonEmissions =
        computeCost({{"carbonEmissions", params["carbonEmissions"]}},
                    costFunctionWeights, mode);
    segment.estimatedCost["carbonEmissions"] = carbonEmissions;

    double directCost =
        computeCost({{"cost", params["cost"]}}, costFunctionWeights, mode);
    segment.estimatedCost["cost"] = directCost;

    double distance = computeCost({{"distance", params["distance"]}},
                                  costFunctionWeights, mode);
    segment.estimatedCost["distance"] = distance;

    double energyConsumption =
        computeCost({{"energyConsumption", params["energyConsumption"]}},
                    costFunctionWeights, mode);
    segment.estimatedCost["energyConsumption"] = energyConsumption;

    double risk =
        computeCost({{"risk", params["risk"]}}, costFunctionWeights, mode);
    segment.estimatedCost["risk"] = risk;

    double travelTime = computeCost({{"travelTime", params["travelTime"]}},
                                    costFunctionWeights, mode);
    segment.estimatedCost["travelTime"] = travelTime;

    double previousTerminalDelay = 0.0;
    if (!skipStartTerminal)
    {
        previousTerminalDelay = computeCost(
            {{"terminal_delay", fromHandlingTime}}, costFunctionWeights, mode);
    }
    previousTerminalDelay =
        isStart ? previousTerminalDelay : previousTerminalDelay / 2;
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

    double nextTerminalDelay = 0.0;
    if (!skipEndTerminal)
    {
        nextTerminalDelay = computeCost({{"terminal_delay", toHandlingTime}},
                                        costFunctionWeights, mode);
    }
    nextTerminalDelay = isEnd ? nextTerminalDelay : nextTerminalDelay / 2;
    segment.estimatedCost["nextTerminalDelay"] = nextTerminalDelay;

    double nextTerminalCost = 0.0;
    if (!skipEndTerminal)
    {
        nextTerminalCost =
            computeCost({{"terminal_cost", toCost}}, costFunctionWeights, mode);
    }
    nextTerminalCost = isEnd ? nextTerminalCost : nextTerminalCost / 2;
    segment.estimatedCost["nextTerminalCost"] = nextTerminalCost;

    segment.weightedEdgeCost =
        carbonEmissions + directCost + distance + energyConsumption
        + risk + travelTime;
    segment.weightedTerminalCostEmbeddedInSegment =
        previousTerminalDelay + previousTerminalCost + nextTerminalDelay
        + nextTerminalCost;
    segment.rankingCostContribution =
        segment.weightedEdgeCost
        + segment.weightedTerminalCostEmbeddedInSegment;
    segment.weight = segment.rankingCostContribution;
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
    const EdgePathInfoType &pathInfo, int displayPathId,
    TransportationMode requestedMode, bool skipDelays) const
{
    Path path;
    path.pathId             = displayPathId;
    path.totalEdgeCosts     = 0;
    path.totalTerminalCosts = 0;
    path.totalPathCost      = 0;
    path.rankingCost        = 0;
    path.requestedMode      = static_cast<int>(requestedMode);
    path.costBreakdown = QVariantMap{
        {QStringLiteral("weighted_edge"),
         QVariantMap{
             {QStringLiteral("carbonEmissions"), 0.0},
             {QStringLiteral("cost"), 0.0},
             {QStringLiteral("distance"), 0.0},
             {QStringLiteral("energyConsumption"), 0.0},
             {QStringLiteral("risk"), 0.0},
             {QStringLiteral("travelTime"), 0.0}}},
        {QStringLiteral("weighted_terminal"),
         QVariantMap{
             {QStringLiteral("delay"), 0.0},
             {QStringLiteral("direct_cost"), 0.0}}}
    };

    // Extract edges from path
    const auto &edges = pathInfo.first;

    // Skip if path is empty
    if (edges.empty())
    {
        return path;
    }

    // Copy necessary data while holding the lock
    QHash<EdgeIdentifier, QList<EdgeData>> edgeDataCopy;
    QHash<QString, TerminalDetails>        terminalData;

    {
        QMutexLocker locker(&m_mutex);
        edgeDataCopy  = m_edgeData;
        terminalData  = m_terminalData;
    }

    // Process segments without the lock
    QList<QVariantMap> terminalsInPath;

    // Add first terminal (source of first edge)
    QString firstTerminalName = edges[0].source();
    path.startTerminal        = firstTerminalName;
    path.endTerminal          = edges.back().target();

    double firstHandlingTime = terminalData[firstTerminalName].handlingTime;
    double firstCost         = terminalData[firstTerminalName].handlingCost;

    QVariantMap terminalInfo;
    terminalInfo["terminal"] = firstTerminalName;
    terminalInfo["terminal_id"] = firstTerminalName;
    terminalInfo["sequence_index"] = 0;
    terminalInfo["handling_time"] = firstHandlingTime;
    terminalInfo["cost"] = firstCost;
    terminalInfo["costs_skipped"] = true;
    terminalInfo["skip_reason"] = QStringLiteral("origin_terminal");
    terminalInfo["weighted_terminal_delay_contribution"] = 0.0;
    terminalInfo["weighted_terminal_cost_contribution"] = 0.0;
    terminalInfo["weighted_terminal_total_contribution"] = 0.0;

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
            const auto        &prevEdge = edges[i - 1];
            TransportationMode prevMode = prevEdge.mode();
            skipPreviousTerminalCost    = (mode == prevMode);
        }

        // Find the edge data
        EdgeIdentifier edgeKey(fromName, toName, mode);

        if (!edgeDataCopy.contains(edgeKey))
        {
            qCWarning(lcTerminalGraph) << "Edge data not found for path segment" << fromName
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
            qCWarning(lcTerminalGraph) << "No matching edge found for mode"
                                       << static_cast<int>(requestedMode);
            continue;
        }

        bool isStart = (i == 0);
        bool isEnd   = (i == edges.size() - 1);
        if (isStart)
        {
            skipPreviousTerminalCost = true;
        }
        // Create path segment
        PathSegment segment;
        buildPathSegment(segment, static_cast<int>(i), isStart, isEnd,
                         skipPreviousTerminalCost,
                         skipNextTerminalCost, fromName, toName, edgeData.mode,
                         edgeData.attributes);
        path.segments.append(segment);

        // Add to path-level weighted costs
        path.totalEdgeCosts += segment.weightedEdgeCost;
        path.rankingCost += segment.rankingCostContribution;

        QVariantMap weightedEdge =
            path.costBreakdown.value(QStringLiteral("weighted_edge")).toMap();
        for (const QString &category : {QStringLiteral("carbonEmissions"),
                                        QStringLiteral("cost"),
                                        QStringLiteral("distance"),
                                        QStringLiteral("energyConsumption"),
                                        QStringLiteral("risk"),
                                        QStringLiteral("travelTime")})
        {
            weightedEdge[category] =
                weightedEdge.value(category).toDouble()
                + segment.estimatedCost.value(category).toDouble();
        }
        path.costBreakdown[QStringLiteral("weighted_edge")] = weightedEdge;

        // Add destination terminal info
        double handlingTime = terminalData[toName].handlingTime;
        double cost         = terminalData[toName].handlingCost;

        QVariantMap terminalInfo;
        terminalInfo["terminal"] = toName;
        terminalInfo["terminal_id"] = toName;
        terminalInfo["sequence_index"] = static_cast<int>(i) + 1;
        terminalInfo["handling_time"] = handlingTime;
        terminalInfo["cost"] = cost;

        // Determine if costs should be skipped
        bool skipCosts = false;
        QString skipReason;
        if (skipDelays && i < edges.size() - 1)
        {
            // The terminal created on iteration i is the destination of edge i
            // and the origin of edge i+1, so same-mode continuation is a
            // forward-looking comparison.
            skipCosts = (edges[i].mode() == edges[i + 1].mode());
            if (skipCosts)
            {
                skipReason = QStringLiteral("same_mode_continuation");
            }
        }

        terminalInfo["costs_skipped"] = skipCosts;
        if (!skipReason.isEmpty())
        {
            terminalInfo["skip_reason"] = skipReason;
        }
        terminalInfo["weighted_terminal_delay_contribution"] = 0.0;
        terminalInfo["weighted_terminal_cost_contribution"] = 0.0;
        terminalInfo["weighted_terminal_total_contribution"] = 0.0;
        terminalsInPath.append(terminalInfo);

        const double previousDelayShare =
            estimatedCostValue(segment, "previousTerminalDelay");
        const double previousCostShare =
            estimatedCostValue(segment, "previousTerminalCost");
        const double nextDelayShare =
            estimatedCostValue(segment, "nextTerminalDelay");
        const double nextCostShare =
            estimatedCostValue(segment, "nextTerminalCost");

        path.weightedTerminalDelayTotal += previousDelayShare + nextDelayShare;
        path.weightedTerminalDirectCostTotal += previousCostShare
                                                + nextCostShare;

        QVariantMap previousTerminal = terminalsInPath[static_cast<int>(i)];
        previousTerminal["weighted_terminal_delay_contribution"] =
            previousTerminal
                .value(QStringLiteral("weighted_terminal_delay_contribution"))
                .toDouble()
            + previousDelayShare;
        previousTerminal["weighted_terminal_cost_contribution"] =
            previousTerminal
                .value(QStringLiteral("weighted_terminal_cost_contribution"))
                .toDouble()
            + previousCostShare;
        previousTerminal["weighted_terminal_total_contribution"] =
            previousTerminal
                .value(QStringLiteral("weighted_terminal_delay_contribution"))
                .toDouble()
            + previousTerminal
                .value(QStringLiteral("weighted_terminal_cost_contribution"))
                .toDouble();
        terminalsInPath[static_cast<int>(i)] = previousTerminal;

        QVariantMap currentTerminal = terminalsInPath.back();
        currentTerminal["weighted_terminal_delay_contribution"] =
            currentTerminal
                .value(QStringLiteral("weighted_terminal_delay_contribution"))
                .toDouble()
            + nextDelayShare;
        currentTerminal["weighted_terminal_cost_contribution"] =
            currentTerminal
                .value(QStringLiteral("weighted_terminal_cost_contribution"))
                .toDouble()
            + nextCostShare;
        currentTerminal["weighted_terminal_total_contribution"] =
            currentTerminal
                .value(QStringLiteral("weighted_terminal_delay_contribution"))
                .toDouble()
            + currentTerminal
                .value(QStringLiteral("weighted_terminal_cost_contribution"))
                .toDouble();
        terminalsInPath.back() = currentTerminal;
    }

    QVariantMap weightedTerminal =
        path.costBreakdown.value(QStringLiteral("weighted_terminal")).toMap();
    for (const QVariantMap &terminal : terminalsInPath)
    {
        const bool skipped =
            terminal.value(QStringLiteral("costs_skipped")).toBool();
        const double rawDelay =
            terminal.value(QStringLiteral("handling_time")).toDouble();
        const double rawCost =
            terminal.value(QStringLiteral("cost")).toDouble();
        if (!skipped)
        {
            path.rawTerminalDelayTotal += rawDelay;
            path.rawTerminalCostTotal += rawCost;
        }
    }
    weightedTerminal[QStringLiteral("delay")] =
        path.weightedTerminalDelayTotal;
    weightedTerminal[QStringLiteral("direct_cost")] =
        path.weightedTerminalDirectCostTotal;
    path.costBreakdown[QStringLiteral("weighted_terminal")] =
        weightedTerminal;

    path.terminalsInPath = terminalsInPath;
    path.totalTerminalCosts = path.weightedTerminalDelayTotal
                              + path.weightedTerminalDirectCostTotal;
    path.totalPathCost = path.rankingCost;

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
        qCDebug(lcTerminalGraph) << "Invalid request: n must be positive";
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
            qCDebug(lcTerminalGraph) << "Terminal not found: start=" << startCanonical
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

    // Resort and assign authoritative rank/display metadata after dedupe.
    std::sort(result.begin(), result.end(), [](const Path &a, const Path &b) {
        return a.rankingCost < b.rankingCost;
    });

    for (int i = 0; i < result.size(); i++)
    {
        result[i].rank = i;
        result[i].pathId = i + 1;
        result[i].requestedMode = static_cast<int>(mode);
        result[i].requestedTopN = n;
        result[i].skipSameModeTerminalDelaysAndCosts = skipDelays;
        result[i].pathUid = buildPathUid(result[i]);
    }

    qCDebug(lcTerminalGraph) << "Found" << result.size() << "paths from" << startCanonical
                             << "to" << endCanonical;
    return result.toList();
}

QString TerminalGraph::getCanonicalName(const QString &name) const
{
    return m_terminalAliases.value(name, name);
}

} // namespace TerminalSim
