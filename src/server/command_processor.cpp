#include "command_processor.h"

#include <QDebug>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMutexLocker>
#include <QDateTime>
#include <QUuid>
#include <stdexcept>

namespace TerminalSim {

CommandProcessor::CommandProcessor(TerminalGraph* graph, QObject* parent)
    : QObject(parent), m_graph(graph)
{
    registerCommands();
    qDebug() << "Command processor initialized with" << m_commandHandlers.size() << "command handlers";
}

CommandProcessor::~CommandProcessor()
{
    qDebug() << "Command processor destroyed";
}

void CommandProcessor::registerCommands()
{
    // System commands
    registerCommand("ping", [this](const QVariantMap& params) { return handlePing(params); });
    registerCommand("serialize_graph", [this](const QVariantMap& params) { return handleSerializeGraph(params); });
    registerCommand("deserialize_graph", [this](const QVariantMap& params) { return handleDeserializeGraph(params); });
    
    // Terminal commands
    registerCommand("add_terminal", [this](const QVariantMap& params) { return handleAddTerminal(params); });
    registerCommand("add_alias_to_terminal", [this](const QVariantMap& params) {
        QString terminalName = params.value("terminal_name").toString();
        QString alias = params.value("alias").toString();
        
        if (terminalName.isEmpty() || alias.isEmpty()) {
            throw std::invalid_argument("Terminal name and alias must be provided");
        }
        
        m_graph->addAliasToTerminal(terminalName, alias);
        return QVariant(true);
    });
    
    registerCommand("get_aliases_of_terminal", [this](const QVariantMap& params) {
        QString terminalName = params.value("terminal_name").toString();
        
        if (terminalName.isEmpty()) {
            throw std::invalid_argument("Terminal name must be provided");
        }
        
        return QVariant(m_graph->getAliasesOfTerminal(terminalName));
    });
    
    registerCommand("remove_terminal", [this](const QVariantMap& params) {
        QString terminalName = params.value("terminal_name").toString();
        
        if (terminalName.isEmpty()) {
            throw std::invalid_argument("Terminal name must be provided");
        }
        
        return QVariant(m_graph->removeTerminal(terminalName));
    });
    
    registerCommand("get_terminal_count", [this](const QVariantMap&) {
        return QVariant(m_graph->getTerminalCount());
    });
    
    registerCommand("get_terminal_status", [this](const QVariantMap& params) {
        QString terminalName = params.value("terminal_name").toString();
        return m_graph->getTerminalStatus(terminalName);
    });
    
    registerCommand("get_terminal", [this](const QVariantMap& params) { return handleGetTerminal(params); });
    
    // Route commands
    registerCommand("add_route", [this](const QVariantMap& params) { return handleAddRoute(params); });
    
    registerCommand("change_route_weight", [this](const QVariantMap& params) {
        QString startTerminal = params.value("start_terminal").toString();
        QString endTerminal = params.value("end_terminal").toString();
        int modeInt = params.value("mode", 0).toInt();
        QVariantMap newAttributes = params.value("attributes").toMap();
        
        if (startTerminal.isEmpty() || endTerminal.isEmpty() || newAttributes.isEmpty()) {
            throw std::invalid_argument("Start terminal, end terminal, and attributes must be provided");
        }
        
        TransportationMode mode = static_cast<TransportationMode>(modeInt);
        m_graph->changeRouteWeight(startTerminal, endTerminal, mode, newAttributes);
        return QVariant(true);
    });
    
    // Auto-connection commands
    registerCommand("connect_terminals_by_interface_modes", [this](const QVariantMap&) {
        m_graph->connectTerminalsByInterfaceModes();
        return QVariant(true);
    });
    
    registerCommand("connect_terminals_in_region_by_mode", [this](const QVariantMap& params) {
        QString region = params.value("region").toString();
        
        if (region.isEmpty()) {
            throw std::invalid_argument("Region must be provided");
        }
        
        m_graph->connectTerminalsInRegionByMode(region);
        return QVariant(true);
    });
    
    registerCommand("connect_regions_by_mode", [this](const QVariantMap& params) {
        int modeInt = params.value("mode", 0).toInt();
        TransportationMode mode = static_cast<TransportationMode>(modeInt);
        
        m_graph->connectRegionsByMode(mode);
        return QVariant(true);
    });
    
    // Path finding commands
    registerCommand("find_shortest_path", [this](const QVariantMap& params) { return handleFindShortestPath(params); });
    registerCommand("find_top_paths", [this](const QVariantMap& params) { return handleFindTopPaths(params); });
    
    // Terminal container operations
    registerCommand("add_container", [this](const QVariantMap& params) { return handleAddContainer(params); });
    
    registerCommand("add_containers", [this](const QVariantMap& params) {
        QString terminalId = params.value("terminal_id").toString();
        double addingTime = params.value("adding_time", -1).toDouble();
        
        if (terminalId.isEmpty()) {
            throw std::invalid_argument("Terminal ID must be provided");
        }
        
        Terminal* terminal = getTerminalFromParams(params);
        
        QList<ContainerCore::Container> containers;
        
        QVariantList containersList = params.value("containers").toList();
        for (const QVariant& containerVar : containersList) {
            QJsonObject containerJson = QJsonDocument::fromJson(containerVar.toString().toUtf8()).object();
            ContainerCore::Container container(containerJson);
            containers.append(container);
        }
        
        terminal->addContainers(containers, addingTime);
        return QVariant(true);
    });
    
    registerCommand("add_containers_from_json", [this](const QVariantMap& params) {
        QString terminalId = params.value("terminal_id").toString();
        double addingTime = params.value("adding_time", -1).toDouble();
        QString jsonStr = params.value("containers_json").toString();
        
        if (terminalId.isEmpty() || jsonStr.isEmpty()) {
            throw std::invalid_argument("Terminal ID and containers JSON must be provided");
        }
        
        Terminal* terminal = getTerminalFromParams(params);
        
        QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8());
        if (!doc.isObject()) {
            throw std::invalid_argument("Invalid JSON format for containers");
        }
        
        terminal->addContainersFromJson(doc.object(), addingTime);
        return QVariant(true);
    });
    
    registerCommand("get_containers_by_departing_time", [this](const QVariantMap& params) {
        QString terminalId = params.value("terminal_id").toString();
        double departingTime = params.value("departing_time").toDouble();
        QString condition = params.value("condition", "<").toString();
        
        if (terminalId.isEmpty()) {
            throw std::invalid_argument("Terminal ID must be provided");
        }
        
        Terminal* terminal = getTerminalFromParams(params);
        
        return terminal->getContainersByDepatingTime(departingTime, condition);
    });
    
    registerCommand("get_containers_by_added_time", [this](const QVariantMap& params) {
        QString terminalId = params.value("terminal_id").toString();
        double addedTime = params.value("added_time").toDouble();
        QString condition = params.value("condition").toString();
        
        if (terminalId.isEmpty() || condition.isEmpty()) {
            throw std::invalid_argument("Terminal ID and condition must be provided");
        }
        
        Terminal* terminal = getTerminalFromParams(params);
        
        return terminal->getContainersByAddedTime(addedTime, condition);
    });
    
    registerCommand("get_containers_by_next_destination", [this](const QVariantMap& params) {
        QString terminalId = params.value("terminal_id").toString();
        QString destination = params.value("destination").toString();
        
        if (terminalId.isEmpty() || destination.isEmpty()) {
            throw std::invalid_argument("Terminal ID and destination must be provided");
        }
        
        Terminal* terminal = getTerminalFromParams(params);
        
        return terminal->getContainersByNextDestination(destination);
    });
    
    registerCommand("dequeue_containers_by_next_destination", [this](const QVariantMap& params) {
        QString terminalId = params.value("terminal_id").toString();
        QString destination = params.value("destination").toString();
        
        if (terminalId.isEmpty() || destination.isEmpty()) {
            throw std::invalid_argument("Terminal ID and destination must be provided");
        }
        
        Terminal* terminal = getTerminalFromParams(params);
        
        return terminal->dequeueContainersByNextDestination(destination);
    });
    
    registerCommand("get_container_count", [this](const QVariantMap& params) {
        QString terminalId = params.value("terminal_id").toString();
        
        if (terminalId.isEmpty()) {
            throw std::invalid_argument("Terminal ID must be provided");
        }
        
        Terminal* terminal = getTerminalFromParams(params);
        
        return QVariant(terminal->getContainerCount());
    });
    
    registerCommand("get_available_capacity", [this](const QVariantMap& params) {
        QString terminalId = params.value("terminal_id").toString();
        
        if (terminalId.isEmpty()) {
            throw std::invalid_argument("Terminal ID must be provided");
        }
        
        Terminal* terminal = getTerminalFromParams(params);
        
        return QVariant(terminal->getAvailableCapacity());
    });
    
    registerCommand("get_max_capacity", [this](const QVariantMap& params) {
        QString terminalId = params.value("terminal_id").toString();
        
        if (terminalId.isEmpty()) {
            throw std::invalid_argument("Terminal ID must be provided");
        }
        
        Terminal* terminal = getTerminalFromParams(params);
        
        return QVariant(terminal->getMaxCapacity());
    });
    
    registerCommand("clear_terminal", [this](const QVariantMap& params) {
        QString terminalId = params.value("terminal_id").toString();
        
        if (terminalId.isEmpty()) {
            throw std::invalid_argument("Terminal ID must be provided");
        }
        
        Terminal* terminal = getTerminalFromParams(params);
        
        terminal->clear();
        return QVariant(true);
    });
}

void CommandProcessor::registerCommand(const QString& command, CommandHandler handler)
{
    QMutexLocker locker(&m_mutex);
    m_commandHandlers[command] = handler;
}

QVariant CommandProcessor::processCommand(const QString& command, const QVariantMap& params)
{
    QMutexLocker locker(&m_mutex);

    qDebug() << "Processing command:" << command << "with params:" << params;

    // Check if command exists
    if (!m_commandHandlers.contains(command)) {
        qWarning() << "Unknown command:" << command;
        throw std::invalid_argument(QString("Unknown command: %1").arg(command).toStdString());
    }

    // Execute command handler
    try {
        QVariantMap processedParams = deserializeParams(params);
        qDebug() << "After deserializeParams:" << processedParams;

        QVariant result = m_commandHandlers[command](processedParams);
        qDebug() << "Result from handler:" << result;

        QVariant serializedResult = serializeResponse(result);
        qDebug() << "After serializeResponse:" << serializedResult;

        return serializedResult;
    } catch (const std::exception& e) {
        qWarning() << "Error processing command" << command << ":" << e.what();
        throw;
    }
}

QJsonObject CommandProcessor::processJsonCommand(const QJsonObject& commandObject)
{
    QJsonObject response;
    
    // Extract command and parameters
    if (!commandObject.contains("command") || !commandObject["command"].isString()) {
        response["success"] = false;
        response["error"] = "Missing or invalid command";
        return response;
    }
    
    QString command = commandObject["command"].toString();
    QVariantMap params;
    
    if (commandObject.contains("params") && commandObject["params"].isObject()) {
        params = commandObject["params"].toObject().toVariantMap();
    }
    
    // Add request ID to response if provided
    if (commandObject.contains("request_id")) {
        response["request_id"] = commandObject["request_id"];
    } else {
        // Generate a request ID if not provided
        response["request_id"] = QUuid::createUuid().toString();
    }
    
    // Add timestamp
    response["timestamp"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    
    // Process command
    try {
        QVariant result = processCommand(command, params);
        response["success"] = true;
        response["result"] = QJsonValue::fromVariant(result);
    } catch (const std::exception& e) {
        response["success"] = false;
        response["error"] = QString(e.what());
    }
    
    return response;
}

Terminal* CommandProcessor::getTerminalFromParams(const QVariantMap& params)
{
    QString terminalId = params.value("terminal_id").toString();
    
    if (terminalId.isEmpty()) {
        throw std::invalid_argument("Terminal ID must be provided");
    }
    
    try {
        return m_graph->getTerminal(terminalId);
    } catch (const std::exception& e) {
        throw std::invalid_argument(QString("Terminal not found: %1").arg(terminalId).toStdString());
    }
}

QVariant CommandProcessor::handlePing(const QVariantMap& params)
{
    QVariantMap response;
    response["status"] = "ok";
    response["timestamp"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    
    if (params.contains("echo")) {
        response["echo"] = params["echo"];
    }
    
    return response;
}

QVariant CommandProcessor::handleSerializeGraph(const QVariantMap&)
{
    return m_graph->serializeGraph();
}

QVariant CommandProcessor::handleDeserializeGraph(const QVariantMap& params)
{
    if (!params.contains("graph_data") ||
        !params["graph_data"].canConvert<QVariantMap>()) {
        throw std::invalid_argument("Missing or invalid graph_data parameter");
    }
    
    QVariantMap graphData = params["graph_data"].toMap();
    QJsonObject jsonGraphData = QJsonObject::fromVariantMap(graphData);
    
    // Create a new graph from the data
    TerminalGraph* newGraph =
        TerminalGraph::deserializeGraph(jsonGraphData,
                                        m_graph->getPathToTerminalsDirectory());
    
    // Replace old graph implementation with new one
    QMutexLocker locker(&m_mutex);
    
    // Store the old graph for deletion
    TerminalGraph* oldGraph = m_graph;
    
    // Point to the new graph
    m_graph = newGraph;
    
    // Delete the old graph (need to be careful with threading here)
    QMetaObject::invokeMethod(this, [oldGraph]() {
        delete oldGraph;
    }, Qt::QueuedConnection);
    
    return true;
}

QVariant CommandProcessor::handleAddTerminal(const QVariantMap& params)
{
    if (!params.contains("terminal_names") || !params.contains("custom_config") ||
        !params.contains("terminal_interfaces")) {
        throw std::invalid_argument("Missing required parameters for add_terminal");
    }
    
    // Extract terminal names
    QStringList terminalNames;
    QVariant terminalNamesVar = params["terminal_names"];
    
    if (terminalNamesVar.canConvert<QString>()) {
        // Single name
        terminalNames << terminalNamesVar.toString();
    } else if (terminalNamesVar.canConvert<QStringList>()) {
        // List of names
        terminalNames = terminalNamesVar.toStringList();
    } else {
        throw std::invalid_argument("terminal_names must be a string or list of strings");
    }
    
    if (terminalNames.isEmpty()) {
        throw std::invalid_argument("At least one terminal name must be provided");
    }
    
    // Extract custom config
    QVariantMap customConfig = params["custom_config"].toMap();
    
    // Extract terminal interfaces
    QVariantMap interfacesMap = params["terminal_interfaces"].toMap();
    QMap<TerminalInterface, QSet<TransportationMode>> terminalInterfaces;
    
    for (auto it = interfacesMap.constBegin(); it != interfacesMap.constEnd(); ++it) {
        // Convert interface string/int to enum
        TerminalInterface interface;
        bool ok = false;
        
        int interfaceInt = it.key().toInt(&ok);
        if (ok) {
            interface = static_cast<TerminalInterface>(interfaceInt);
        } else {
            // Try to convert from string
            interface = EnumUtils::stringToTerminalInterface(it.key());
        }
        
        // Extract supported modes
        QSet<TransportationMode> modes;
        QVariantList modesList = it.value().toList();
        
        for (const QVariant& modeVar : modesList) {
            TransportationMode mode;
            
            if (modeVar.canConvert<int>()) {
                mode = static_cast<TransportationMode>(modeVar.toInt());
            } else if (modeVar.canConvert<QString>()) {
                mode = EnumUtils::stringToTransportationMode(modeVar.toString());
            } else {
                continue;
            }
            
            modes.insert(mode);
        }
        
        if (!modes.isEmpty()) {
            terminalInterfaces[interface] = modes;
        }
    }
    
    if (terminalInterfaces.isEmpty()) {
        throw std::invalid_argument("At least one terminal interface with modes must be provided");
    }
    
    // Extract region (optional)
    QString region = params.value("region").toString();
    
    // Add terminal to graph
    m_graph->addTerminal(terminalNames, customConfig, terminalInterfaces, region);
    
    return true;
}

QVariant CommandProcessor::handleAddRoute(const QVariantMap& params)
{
    if (!params.contains("route_id") || !params.contains("start_terminal") ||
        !params.contains("end_terminal") || !params.contains("mode")) {
        throw std::invalid_argument("Missing required parameters for add_route");
    }
    
    QString routeId = params["route_id"].toString();
    QString startTerminal = params["start_terminal"].toString();
    QString endTerminal = params["end_terminal"].toString();
    
    // Extract mode
    TransportationMode mode;
    QVariant modeVar = params["mode"];
    
    if (modeVar.canConvert<int>()) {
        mode = static_cast<TransportationMode>(modeVar.toInt());
    } else if (modeVar.canConvert<QString>()) {
        mode = EnumUtils::stringToTransportationMode(modeVar.toString());
    } else {
        throw std::invalid_argument("Invalid mode parameter");
    }
    
    // Extract attributes (optional)
    QVariantMap attributes;
    if (params.contains("attributes") && params["attributes"].canConvert<QVariantMap>()) {
        attributes = params["attributes"].toMap();
    }
    
    // Add route to graph
    m_graph->addRoute(routeId, startTerminal, endTerminal, mode, attributes);
    
    return true;
}

QVariant CommandProcessor::handleGetTerminal(const QVariantMap& params)
{
    if (!params.contains("terminal_name")) {
        throw std::invalid_argument("Missing terminal_name parameter");
    }
    
    QString terminalName = params["terminal_name"].toString();
    
    Terminal* terminal = m_graph->getTerminal(terminalName);
    return terminal->toJson();
}

QVariant CommandProcessor::handleFindShortestPath(const QVariantMap& params)
{
    if (!params.contains("start_terminal") || !params.contains("end_terminal")) {
        throw std::invalid_argument("Missing start_terminal or end_terminal parameter");
    }
    
    QString startTerminal = params["start_terminal"].toString();
    QString endTerminal = params["end_terminal"].toString();
    
    // Extract mode (optional)
    TransportationMode mode = TransportationMode::Truck;  // Default
    if (params.contains("mode")) {
        QVariant modeVar = params["mode"];
        
        if (modeVar.canConvert<int>()) {
            mode = static_cast<TransportationMode>(modeVar.toInt());
        } else if (modeVar.canConvert<QString>()) {
            mode = EnumUtils::stringToTransportationMode(modeVar.toString());
        }
    }
    
    // Check if we need to restrict by regions
    if (params.contains("allowed_regions") && params["allowed_regions"].canConvert<QStringList>()) {
        QStringList allowedRegions = params["allowed_regions"].toStringList();
        
        return QVariant::fromValue(m_graph->findShortestPathWithinRegions(
            startTerminal, endTerminal, allowedRegions, mode));
    } else {
        return QVariant::fromValue(m_graph->findShortestPath(startTerminal, endTerminal, mode));
    }
}

QVariant CommandProcessor::handleFindTopPaths(const QVariantMap& params)
{
    if (!params.contains("start_terminal") || !params.contains("end_terminal")) {
        throw std::invalid_argument("Missing start_terminal or end_terminal parameter");
    }
    
    QString startTerminal = params["start_terminal"].toString();
    QString endTerminal = params["end_terminal"].toString();
    
    // Extract number of paths (optional)
    int n = params.value("n", 5).toInt();
    
    // Extract mode (optional)
    TransportationMode mode = TransportationMode::Truck;  // Default
    if (params.contains("mode")) {
        QVariant modeVar = params["mode"];
        
        if (modeVar.canConvert<int>()) {
            mode = static_cast<TransportationMode>(modeVar.toInt());
        } else if (modeVar.canConvert<QString>()) {
            mode = EnumUtils::stringToTransportationMode(modeVar.toString());
        }
    }
    
    // Extract skip option (optional)
    bool skipSameModeTerminalDelaysAndCosts = params.value("skip_same_mode_terminal_delays_and_costs", true).toBool();
    
    return QVariant::fromValue(m_graph->findTopNShortestPaths(
        startTerminal, endTerminal, n, mode, skipSameModeTerminalDelaysAndCosts));
}

QVariant CommandProcessor::handleAddContainer(const QVariantMap& params)
{
    QString terminalId = params.value("terminal_id").toString();
    double addingTime = params.value("adding_time", -1).toDouble();
    
    if (terminalId.isEmpty() || !params.contains("container")) {
        throw std::invalid_argument("Terminal ID and container must be provided");
    }
    
    Terminal* terminal = getTerminalFromParams(params);
    
    // Parse container JSON
    QVariant containerVar = params["container"];
    QJsonObject containerJson;
    
    if (containerVar.canConvert<QString>()) {
        // Parse JSON string
        QJsonDocument doc = QJsonDocument::fromJson(containerVar.toString().toUtf8());
        if (!doc.isObject()) {
            throw std::invalid_argument("Invalid JSON format for container");
        }
        containerJson = doc.object();
    } else if (containerVar.canConvert<QVariantMap>()) {
        // Convert variant map to JSON object
        containerJson = QJsonObject::fromVariantMap(containerVar.toMap());
    } else {
        throw std::invalid_argument("Container must be a JSON string or object");
    }
    
    // Create container object
    ContainerCore::Container container(containerJson);
    
    // Add container to terminal
    terminal->addContainer(container, addingTime);
    
    return true;
}

QVariant CommandProcessor::serializeResponse(const QVariant& result)
{
    // If result is already a QVariant, return it directly
    if (result.typeId() == QMetaType::QVariant) {
        return result;
    }
    
    // For QJsonValue, QJsonObject, QJsonArray, convert to QVariant
    if (result.canConvert<QJsonValue>()) {
        return QVariant::fromValue(result.value<QJsonValue>().toVariant());
    }
    
    // For other types, return as is
    return result;
}

QVariantMap CommandProcessor::deserializeParams(const QVariantMap& params)
{
    QVariantMap result;

    for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
        const QString& key = it.key();
        const QVariant& value = it.value();

        // Handle different types of parameters
        if (key == "container" || key == "containers_json" ||
            value.typeId() == QMetaType::QString) {  // Use typeId() and QMetaType instead
            // For container JSON or string values, leave as is
            result[key] = value;
        } else if (value.canConvert<QVariantList>()) {
            // For lists, process each item
            QVariantList list = value.toList();
            QVariantList processedList;

            for (const QVariant& item : list) {
                if (item.canConvert<QVariantMap>()) {
                    processedList.append(deserializeParams(item.toMap()));
                } else {
                    processedList.append(item);
                }
            }

            result[key] = processedList;
        } else if (value.canConvert<QVariantMap>()) {
            // For maps, recursively process
            result[key] = deserializeParams(value.toMap());
        } else {
            // For other types, handle special cases
            result[key] = value;
        }
    }

    return result;
}

} // namespace TerminalSim
