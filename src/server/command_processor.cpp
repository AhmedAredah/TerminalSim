#include "command_processor.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutexLocker>
#include <QUuid>
#include <stdexcept>

#include "common/LogCategories.h"

namespace TerminalSim
{

CommandProcessor::CommandProcessor(TerminalGraph *graph, QObject *parent)
    : QObject(parent)
    , m_graph(graph)
{
    registerCommands();
    qCDebug(lcCommandProcessor) << "Command processor initialized with" << m_commandHandlers.size()
                                << "command handlers";
}

CommandProcessor::~CommandProcessor()
{
    qCDebug(lcCommandProcessor) << "Command processor destroyed";
}

void CommandProcessor::registerCommands()
{
    // System commands
    registerCommand("ping", [this](const QVariantMap &params) {
        return handlePing(params);
    });

    registerCommand("resetServer", [this](const QVariantMap &params) {
        return handleResetServer(params);
    });

    registerCommand("set_cost_function_parameters",
                    [this](const QVariantMap &params) {
                        return handleSetCostFunctionParameters(params);
                    });

    // Terminal commands
    registerCommand("add_terminal", [this](const QVariantMap &params) {
        return handleAddTerminal(params);
    });
    registerCommand("add_terminals", [this](const QVariantMap &params) {
        return handleAddTerminals(params);
    });
    registerCommand("add_alias_to_terminal", [this](const QVariantMap &params) {
        QString terminalName = params.value("terminal_name").toString();
        QString alias        = params.value("alias").toString();

        if (terminalName.isEmpty() || alias.isEmpty())
        {
            throw std::invalid_argument("Terminal name and alias "
                                        "must be provided");
        }

        m_graph->addAliasToTerminal(terminalName, alias);
        return QVariant(true);
    });

    registerCommand(
        "get_aliases_of_terminal", [this](const QVariantMap &params) {
            QString terminalName = params.value("terminal_name").toString();

            if (terminalName.isEmpty())
            {
                throw std::invalid_argument("Terminal name must be provided");
            }

            return QVariant(m_graph->getAliasesOfTerminal(terminalName));
        });

    registerCommand("remove_terminal", [this](const QVariantMap &params) {
        QString terminalName = params.value("terminal_name").toString();

        if (terminalName.isEmpty())
        {
            throw std::invalid_argument("Terminal name must be provided");
        }

        return QVariant(m_graph->removeTerminal(terminalName));
    });

    registerCommand("get_terminal_count", [this](const QVariantMap &) {
        return QVariant(m_graph->getTerminalCount());
    });

    registerCommand("get_terminal_status", [this](const QVariantMap &params) {
        QString terminalName = params.value("terminal_name").toString();
        return m_graph->getTerminalStatus(terminalName);
    });

    registerCommand("get_terminal", [this](const QVariantMap &params) {
        return handleGetTerminal(params);
    });

    // Route commands
    registerCommand("add_route", [this](const QVariantMap &params) {
        return handleAddRoute(params);
    });
    registerCommand("add_routes", [this](const QVariantMap &params) {
        return handleAddRoutes(params);
    });

    // Path finding commands
    registerCommand("find_shortest_path", [this](const QVariantMap &params) {
        return handleFindShortestPath(params);
    });
    registerCommand("find_top_paths", [this](const QVariantMap &params) {
        return handleFindTopPaths(params);
    });

    // Terminal container operations
    registerCommand("add_container", [this](const QVariantMap &params) {
        return handleAddContainer(params);
    });

    registerCommand("add_containers", [this](const QVariantMap &params) {
        QString terminalId = params.value("terminal_id").toString();
        double  addingTime = params.value("adding_time", -1).toDouble();

        if (terminalId.isEmpty())
        {
            throw std::invalid_argument("Terminal ID must be provided");
        }

        Terminal *terminal = getTerminalFromParams(params);

        // Extract arrival mode if provided
        TransportationMode arrivalMode = TransportationMode::Any;
        if (params.contains("arrival_mode")) {
            arrivalMode = EnumUtils::stringToTransportationMode(
                params.value("arrival_mode").toString());
        }

        QList<ContainerCore::Container> containers;

        QVariant containersValue = params.value("containers");

        if (containersValue.typeId() == QMetaType::QString)
        {
            // JSON document string (from CargoNetSim string variant)
            // Format: {"containers": [{...}, {...}]}
            QString jsonStr = containersValue.toString();
            QJsonDocument doc =
                QJsonDocument::fromJson(jsonStr.toUtf8());
            if (doc.isObject())
            {
                QJsonArray arr =
                    doc.object().value("containers").toArray();
                for (const QJsonValue &val : arr)
                {
                    if (val.isObject())
                    {
                        ContainerCore::Container container(
                            val.toObject());
                        containers.append(container);
                    }
                }
            }
        }
        else if (containersValue.canConvert<QVariantList>())
        {
            // List of container objects (from QJsonArray path)
            QVariantList containersList = containersValue.toList();
            for (const QVariant &containerVar : containersList)
            {
                QJsonObject containerJson;
                if (containerVar.canConvert<QVariantMap>())
                {
                    containerJson =
                        QJsonObject::fromVariantMap(containerVar.toMap());
                }
                else
                {
                    containerJson =
                        QJsonDocument::fromJson(
                            containerVar.toString().toUtf8())
                            .object();
                }
                ContainerCore::Container container(containerJson);
                containers.append(container);
            }
        }

        terminal->addContainers(containers, addingTime, arrivalMode);
        return QVariant(true);
    });

    registerCommand("add_containers_from_json", [this](
                                                    const QVariantMap &params) {
        QString terminalId = params.value("terminal_id").toString();
        double  addingTime = params.value("adding_time", -1).toDouble();
        QString jsonStr    = params.value("containers_json").toString();

        if (terminalId.isEmpty() || jsonStr.isEmpty())
        {
            throw std::invalid_argument("Terminal ID and containers "
                                        "JSON must be provided");
        }

        Terminal *terminal = getTerminalFromParams(params);

        // Extract arrival mode if provided
        TransportationMode arrivalMode = TransportationMode::Any;
        if (params.contains("arrival_mode")) {
            arrivalMode = EnumUtils::stringToTransportationMode(
                params.value("arrival_mode").toString());
        }

        QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8());
        if (!doc.isObject())
        {
            throw std::invalid_argument("Invalid JSON format for containers");
        }

        terminal->addContainersFromJson(doc.object(), addingTime, arrivalMode);
        return QVariant(true);
    });

    registerCommand(
        "get_containers_by_departing_time", [this](const QVariantMap &params) {
            QString terminalId    = params.value("terminal_id").toString();
            double  departingTime = params.value("departing_time").toDouble();
            QString condition     = params.value("condition", "<").toString();

            if (terminalId.isEmpty())
            {
                throw std::invalid_argument("Terminal ID must be provided");
            }

            Terminal *terminal = getTerminalFromParams(params);

            return terminal->getContainersByDepatingTime(departingTime,
                                                         condition);
        });

    registerCommand(
        "get_containers_by_added_time", [this](const QVariantMap &params) {
            QString terminalId = params.value("terminal_id").toString();
            double  addedTime  = params.value("added_time").toDouble();
            QString condition  = params.value("condition").toString();

            if (terminalId.isEmpty() || condition.isEmpty())
            {
                throw std::invalid_argument("Terminal ID and condition "
                                            "must be provided");
            }

            Terminal *terminal = getTerminalFromParams(params);

            return terminal->getContainersByAddedTime(addedTime, condition);
        });

    registerCommand(
        "get_containers_by_next_destination",
        [this](const QVariantMap &params) {
            QString terminalId  = params.value("terminal_id").toString();
            QString destination = params.value("destination").toString();

            if (terminalId.isEmpty() || destination.isEmpty())
            {
                throw std::invalid_argument("Terminal ID and destination "
                                            "must be provided");
            }

            Terminal *terminal = getTerminalFromParams(params);

            return terminal->getContainersByNextDestination(destination);
        });

    registerCommand(
        "dequeue_containers_by_next_destination",
        [this](const QVariantMap &params) {
            QString terminalId  = params.value("terminal_id").toString();
            QString destination = params.value("destination").toString();

            if (terminalId.isEmpty() || destination.isEmpty())
            {
                throw std::invalid_argument("Terminal ID and destination must "
                                            "be provided");
            }

            Terminal *terminal = getTerminalFromParams(params);

            return terminal->dequeueContainersByNextDestination(destination);
        });

    registerCommand("get_container_count", [this](const QVariantMap &params) {
        QString terminalId = params.value("terminal_id").toString();

        if (terminalId.isEmpty())
        {
            throw std::invalid_argument("Terminal ID must be provided");
        }

        Terminal *terminal = getTerminalFromParams(params);

        return QVariant(terminal->getContainerCount());
    });

    registerCommand(
        "get_available_capacity", [this](const QVariantMap &params) {
            QString terminalId = params.value("terminal_id").toString();

            if (terminalId.isEmpty())
            {
                throw std::invalid_argument("Terminal ID must be provided");
            }

            Terminal *terminal = getTerminalFromParams(params);

            return QVariant(terminal->getAvailableCapacity());
        });

    registerCommand("get_max_capacity", [this](const QVariantMap &params) {
        QString terminalId = params.value("terminal_id").toString();

        if (terminalId.isEmpty())
        {
            throw std::invalid_argument("Terminal ID must be provided");
        }

        Terminal *terminal = getTerminalFromParams(params);

        return QVariant(terminal->getMaxCapacity());
    });

    registerCommand("clear_terminal", [this](const QVariantMap &params) {
        QString terminalId = params.value("terminal_id").toString();

        if (terminalId.isEmpty())
        {
            throw std::invalid_argument("Terminal ID must be provided");
        }

        Terminal *terminal = getTerminalFromParams(params);

        terminal->clear();
        return QVariant(true);
    });

    // System Dynamics commands
    registerCommand("update_system_dynamics", [this](const QVariantMap &params) {
        QString terminalId = params.value("terminal_id").toString();
        double  currentTime = params.value("current_time", 0.0).toDouble();
        double  deltaT = params.value("delta_t", 3600.0).toDouble();  // seconds; 3600 = 1 hour

        if (terminalId.isEmpty())
        {
            throw std::invalid_argument("Terminal ID must be provided");
        }

        Terminal *terminal = getTerminalFromParams(params);
        terminal->updateSystemDynamics(currentTime, deltaT);

        return terminal->getSystemDynamicsState();
    });

    registerCommand("get_system_dynamics_state", [this](const QVariantMap &params) {
        QString terminalId = params.value("terminal_id").toString();

        if (terminalId.isEmpty())
        {
            throw std::invalid_argument("Terminal ID must be provided");
        }

        Terminal *terminal = getTerminalFromParams(params);
        return terminal->getSystemDynamicsState();
    });

    registerCommand("update_all_terminals_sd", [this](const QVariantMap &params) {
        double currentTime = params.value("current_time", 0.0).toDouble();
        double deltaT = params.value("delta_t", 3600.0).toDouble();  // seconds; 3600 = 1 hour

        QJsonArray results;
        QStringList terminalNames = m_graph->getAllTerminalNames(false).keys();

        for (const QString& terminalName : terminalNames)
        {
            Terminal* terminal = m_graph->getTerminal(terminalName);
            if (terminal && terminal->isSystemDynamicsEnabled())
            {
                terminal->updateSystemDynamics(currentTime, deltaT);

                QJsonObject terminalResult;
                terminalResult["terminal_id"] = terminalName;
                terminalResult["state"] = terminal->getSystemDynamicsState();
                results.append(terminalResult);
            }
        }

        QJsonObject response;
        response["terminals_updated"] = results.size();
        response["results"] = results;
        return response;
    });

    registerCommand("get_terminals_runtime_state",
                    [this](const QVariantMap &params) {
        QVariantList terminalIds = params.value("terminal_ids").toList();
        if (terminalIds.isEmpty())
        {
            throw std::invalid_argument(
                "terminal_ids must be provided");
        }

        QJsonArray results;
        for (const auto &terminalIdVar : terminalIds)
        {
            const QString terminalId = terminalIdVar.toString();
            if (terminalId.isEmpty())
                continue;

            Terminal *terminal = m_graph->getTerminal(terminalId);
            if (!terminal)
            {
                throw std::invalid_argument(
                    QString("Terminal not found: %1")
                        .arg(terminalId)
                        .toStdString());
            }

            results.append(terminal->getRuntimeTerminalSnapshot());
        }

        QJsonObject response;
        response["terminals_requested"] = terminalIds.size();
        response["results"] = results;
        return response;
    });

    registerCommand("get_terminals_runtime_projections",
                    [this](const QVariantMap &params) {
        QVariantList terminalIds = params.value("terminal_ids").toList();
        if (terminalIds.isEmpty())
        {
            throw std::invalid_argument(
                "terminal_ids must be provided");
        }

        QJsonArray results;
        for (const auto &terminalIdVar : terminalIds)
        {
            const QString terminalId = terminalIdVar.toString();
            if (terminalId.isEmpty())
                continue;

            Terminal *terminal = m_graph->getTerminal(terminalId);
            if (!terminal)
            {
                throw std::invalid_argument(
                    QString("Terminal not found: %1")
                        .arg(terminalId)
                        .toStdString());
            }

            results.append(terminal->getRuntimeTerminalProjectionsByMode());
        }

        QJsonObject response;
        response["terminals_requested"] = terminalIds.size();
        response["results"] = results;
        return response;
    });

    registerCommand("get_terminal_execution_results",
                    [this](const QVariantMap &params) {
        const QString executionId =
            params.value("execution_id").toString();
        const QVariantList terminalIds =
            params.value("terminal_ids").toList();
        const QVariantList pathIdentityVars =
            params.value("path_identities").toList();

        QStringList pathIdentities;
        for (const auto &pathIdentity : pathIdentityVars)
        {
            if (!pathIdentity.toString().isEmpty())
                pathIdentities.append(pathIdentity.toString());
        }

        QStringList resolvedTerminalIds;
        if (!terminalIds.isEmpty())
        {
            for (const auto &terminalIdVar : terminalIds)
            {
                if (!terminalIdVar.toString().isEmpty())
                    resolvedTerminalIds.append(terminalIdVar.toString());
            }
        }
        else
        {
            resolvedTerminalIds =
                m_graph->getAllTerminalNames(false).keys();
        }

        QJsonArray results;
        for (const auto &terminalId : resolvedTerminalIds)
        {
            Terminal *terminal = m_graph->getTerminal(terminalId);
            if (!terminal)
            {
                throw std::invalid_argument(
                    QString("Terminal not found: %1")
                        .arg(terminalId)
                        .toStdString());
            }

            const QJsonArray terminalResults =
                terminal->getTerminalExecutionResults(
                    executionId, pathIdentities);
            for (const auto &value : terminalResults)
                results.append(value);
        }

        QJsonObject response;
        response["execution_id"] = executionId;
        response["results"] = results;
        return response;
    });

    registerCommand("clear_terminal_execution_results",
                    [this](const QVariantMap &params) {
        const QString executionId =
            params.value("execution_id").toString();
        const QVariantList terminalIds =
            params.value("terminal_ids").toList();

        QStringList resolvedTerminalIds;
        if (!terminalIds.isEmpty())
        {
            for (const auto &terminalIdVar : terminalIds)
            {
                if (!terminalIdVar.toString().isEmpty())
                    resolvedTerminalIds.append(terminalIdVar.toString());
            }
        }
        else
        {
            resolvedTerminalIds =
                m_graph->getAllTerminalNames(false).keys();
        }

        int cleared = 0;
        for (const auto &terminalId : resolvedTerminalIds)
        {
            Terminal *terminal = m_graph->getTerminal(terminalId);
            if (!terminal)
            {
                throw std::invalid_argument(
                    QString("Terminal not found: %1")
                        .arg(terminalId)
                        .toStdString());
            }
            cleared += terminal->clearTerminalExecutionResults(
                executionId);
        }

        QJsonObject response;
        response["execution_id"] = executionId;
        response["terminals_cleared"] = resolvedTerminalIds.size();
        response["records_cleared"] = cleared;
        return response;
    });
}

void CommandProcessor::registerCommand(const QString &command,
                                       CommandHandler handler)
{
    QMutexLocker locker(&m_mutex);
    m_commandHandlers[command] = handler;
}

QVariant CommandProcessor::processCommand(const QString     &command,
                                          const QVariantMap &params)
{
    QMutexLocker locker(&m_mutex);

    qCDebug(lcCommandProcessor) << "Processing command:" << command;

    // Check if command exists
    if (!m_commandHandlers.contains(command))
    {
        qCWarning(lcCommandProcessor) << "Unknown command:" << command;
        throw std::invalid_argument(
            QString("Unknown command: %1").arg(command).toStdString());
    }

    // Execute command handler
    try
    {
        QVariantMap processedParams = deserializeParams(params);
        // qDebug() << "After deserializeParams:" << processedParams;

        QVariant result = m_commandHandlers[command](processedParams);
        // qDebug() << "Result from handler:" << result;

        QVariant serializedResult = serializeResponse(result);
        // qDebug() << "After serializeResponse:" << serializedResult;

        return serializedResult;
    }
    catch (const std::exception &e)
    {
        qCWarning(lcCommandProcessor) << "Error processing command" << command << ":" << e.what();
        throw;
    }
}

QJsonObject
CommandProcessor::processJsonCommand(const QJsonObject &commandObject)
{
    QJsonObject response;

    // Extract command and parameters
    if (!commandObject.contains("command")
        || !commandObject["command"].isString())
    {
        response["success"] = false;
        response["error"]   = "Missing or invalid command";
        return response;
    }

    QString     command = commandObject["command"].toString();
    QVariantMap params;

    if (commandObject.contains("params") && commandObject["params"].isObject())
    {
        params = commandObject["params"].toObject().toVariantMap();
    }

    // Add request ID to response if provided
    if (commandObject.contains("request_id"))
    {
        response["request_id"] = commandObject["request_id"];
    }
    else
    {
        // Generate a request ID if not provided
        response["request_id"] = QUuid::createUuid().toString();
    }

    // Copy command ID if present
    if (commandObject.contains("commandId"))
    {
        response["commandId"] = commandObject["commandId"];
    }

    // Echo params back so the caller can identify the response
    if (commandObject.contains("params"))
    {
        response["params"] = commandObject["params"];
    }

    // Add timestamp
    response["timestamp"] =
        QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    // Map command to event name - this is what the client is looking for
    QString eventName = determineEventName(command);
    response["event"] = eventName;

    // Process command
    try
    {
        QVariant result     = processCommand(command, params);
        response["success"] = true;
        response["result"]  = QJsonValue::fromVariant(result);
    }
    catch (const std::exception &e)
    {
        response["success"] = false;
        response["error"]   = QString(e.what());
        // The event name remains in the response even for errors
    }

    return response;
}

QString CommandProcessor::determineEventName(const QString &command)
{
    if (command == "add_terminal" || command == "add_alias_to_terminal")
    {
        return "terminalAdded";
    }
    else if (command == "add_terminals")
    {
        return "terminalsAdded";
    }
    else if (command == "get_aliases_of_terminal")
    {
        return "terminalAliases";
    }
    else if (command == "remove_terminal")
    {
        return "terminalRemoved";
    }
    else if (command == "get_terminal_count")
    {
        return "terminalCount";
    }
    else if (command == "get_terminal")
    {
        return "terminalStatus";
    }
    else if (command == "add_route" || command == "change_route_weight"
             || command == "connect_terminals_by_interface_modes"
             || command == "connect_terminals_in_region_by_mode"
             || command == "connect_regions_by_mode")
    {
        return "routeAdded";
    }
    else if (command == "add_routes")
    {
        return "routesAdded";
    }
    else if (command == "find_shortest_path" || command == "find_top_paths")
    {
        return "pathFound";
    }
    else if (command == "add_container" || command == "add_containers"
             || command == "add_containers_from_json"
             || command == "clear_terminal")
    {
        return "containersAdded";
    }
    else if (command == "get_containers_by_departing_time"
             || command == "get_containers_by_added_time"
             || command == "get_containers_by_next_destination"
             || command == "dequeue_containers_by_next_destination")
    {
        return "containersFetched";
    }
    else if (command == "get_container_count"
             || command == "get_available_capacity"
             || command == "get_max_capacity")
    {
        return "capacityFetched";
    }
    else if (command == "serialize_graph")
    {
        return "graphSerialized";
    }
    else if (command == "deserialize_graph")
    {
        return "graphDeserialized";
    }
    else if (command == "ping")
    {
        return "pingResponse";
    }
    else if (command == "resetServer")
    {
        return "serverReset";
    }
    else if (command == "set_cost_function_parameters")
    {
        return "costFunctionUpdated";
    }
    else if (command == "get_system_dynamics_state")
    {
        return "systemDynamicsState";
    }
    else if (command == "update_system_dynamics"
             || command == "update_all_terminals_sd")
    {
        return "systemDynamicsUpdated";
    }
    else if (command == "get_terminals_runtime_state")
    {
        return "terminalRuntimeState";
    }
    else if (command == "get_terminals_runtime_projections")
    {
        return "terminalRuntimeProjections";
    }
    else if (command == "get_terminal_execution_results")
    {
        return "terminalExecutionResults";
    }
    else if (command == "clear_terminal_execution_results")
    {
        return "terminalExecutionResultsCleared";
    }
    else
    {
        return "errorOccurred"; // Default event for unknown commands
    }
}

Terminal *CommandProcessor::getTerminalFromParams(const QVariantMap &params)
{
    QString terminalId = params.value("terminal_id").toString();

    if (terminalId.isEmpty())
    {
        throw std::invalid_argument("Terminal ID must be provided");
    }

    try
    {
        return m_graph->getTerminal(terminalId);
    }
    catch (const std::exception &e)
    {
        throw std::invalid_argument(
            QString("Terminal not found: %1").arg(terminalId).toStdString());
    }
}

QVariant CommandProcessor::handlePing(const QVariantMap &params)
{
    QVariantMap response;
    response["status"] = "ok";
    response["timestamp"] =
        QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    if (params.contains("echo"))
    {
        response["echo"] = params["echo"];
    }

    return response;
}


QVariant CommandProcessor::handleResetServer(const QVariantMap &params)
{
    Q_UNUSED(params); // Mark params as unused

    // Clear the existing graph
    m_graph->clear();

    // Reinitialize any default settings
    // reset default link attributes or cost function parameters
    m_graph->setLinkDefaultAttributes({{"cost", 1.0},
                                       {"travelTime", 1.0},
                                       {"distance", 1.0},
                                       {"carbonEmissions", 1.0},
                                       {"risk", 1.0},
                                       {"energyConsumption", 1.0}});

    qCInfo(lcCommandProcessor) << "Server reset: Terminal graph cleared "
                                  "and reinitialized to fresh state";

    // Return success
    QVariantMap response;
    response["status"]  = "success";
    response["message"] = "Server has been reset to a fresh state";
    return response;
}

QVariant
CommandProcessor::handleSetCostFunctionParameters(const QVariantMap &params)
{
    if (!params.contains("parameters")
        || !params["parameters"].canConvert<QVariantMap>())
    {
        throw std::invalid_argument("Missing or invalid parameters parameter");
    }

    QVariantMap costParameters = params["parameters"].toMap();

    // Verify required mode entries exist
    QStringList requiredModes = {
        "default", QString::number(static_cast<int>(TransportationMode::Ship)),
        QString::number(static_cast<int>(TransportationMode::Train)),
        QString::number(static_cast<int>(TransportationMode::Truck))};

    for (const QString &mode : requiredModes)
    {
        if (!costParameters.contains(mode)
            || !costParameters[mode].canConvert<QVariantMap>())
        {
            throw std::invalid_argument(
                QString("Missing or invalid parameters for mode: %1")
                    .arg(mode)
                    .toStdString());
        }

        // For each mode, verify required cost attributes exist
        QVariantMap modeParams    = costParameters[mode].toMap();
        QStringList requiredAttrs = {
            "cost", "travelTime",        "distance",       "carbonEmissions",
            "risk", "energyConsumption", "terminal_delay", "terminal_cost"};

        for (const QString &attr : requiredAttrs)
        {
            if (!modeParams.contains(attr)
                || !modeParams[attr].canConvert<double>())
            {
                throw std::invalid_argument(
                    QString("Missing or invalid %1 parameter for mode %2")
                        .arg(attr, mode)
                        .toStdString());
            }
        }
    }

    // All validations passed, update cost function parameters
    m_graph->setCostFunctionParameters(costParameters);
    return QVariant(true);
}

QVariant CommandProcessor::handleAddTerminal(const QVariantMap &params)
{

    // Add terminal to graph
    return m_graph->addTerminal(params)->toJson();
}

QVariant CommandProcessor::handleAddTerminals(const QVariantMap &params)
{
    if (!params.contains("terminals")
        || !params["terminals"].canConvert<QVariantList>())
    {
        throw std::invalid_argument("Missing or invalid terminals parameter");
    }

    QVariantList       terminalsList = params["terminals"].toList();
    QList<QVariantMap> terminalsData;

    // Convert QVariantList to QList<QVariantMap>
    for (const QVariant &terminal : terminalsList)
    {
        if (!terminal.canConvert<QVariantMap>())
        {
            throw std::invalid_argument("Invalid terminal data format");
        }
        terminalsData.append(terminal.toMap());
    }

    // Add terminals to graph
    QMap<QString, Terminal *> addedTerminals =
        m_graph->addTerminals(terminalsData);

    // Create response
    QJsonArray terminalsArray;
    for (auto it = addedTerminals.begin(); it != addedTerminals.end(); ++it)
    {
        terminalsArray.append(it.value()->toJson());
    }

    return terminalsArray;
}

QVariant CommandProcessor::handleAddRoute(const QVariantMap &params)
{
    if (!params.contains("route_id") || !params.contains("start_terminal")
        || !params.contains("end_terminal") || !params.contains("mode"))
    {
        throw std::invalid_argument("Missing required parameters for "
                                    "add_route");
    }

    QString routeId       = params["route_id"].toString();
    QString startTerminal = params["start_terminal"].toString();
    QString endTerminal   = params["end_terminal"].toString();

    // Extract mode
    TransportationMode mode;
    QVariant           modeVar = params["mode"];

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
        throw std::invalid_argument("Invalid mode parameter");
    }

    // Extract attributes (optional)
    QVariantMap attributes;
    if (params.contains("attributes")
        && params["attributes"].canConvert<QVariantMap>())
    {
        attributes = params["attributes"].toMap();
    }

    // Add route to graph
    auto startEndTerminals = m_graph->addRoute(routeId, startTerminal,
                                               endTerminal, mode, attributes);

    QJsonObject startEndTerminalsJson;
    startEndTerminalsJson["start_terminal"] = startEndTerminals.first;
    startEndTerminalsJson["end_terminal"]   = startEndTerminals.second;

    return startEndTerminalsJson;
}

QVariant CommandProcessor::handleAddRoutes(const QVariantMap &params)
{
    if (!params.contains("routes")
        || !params["routes"].canConvert<QVariantList>())
    {
        throw std::invalid_argument("Missing or invalid routes parameter");
    }

    QVariantList       routesList = params["routes"].toList();
    QList<QVariantMap> routesData;

    // Convert QVariantList to QList<QVariantMap>
    for (const QVariant &route : routesList)
    {
        if (!route.canConvert<QVariantMap>())
        {
            throw std::invalid_argument("Invalid route data format");
        }
        routesData.append(route.toMap());
    }

    // Add routes to graph
    QList<QPair<QString, QString>> addedRoutes = m_graph->addRoutes(routesData);

    // Create response
    QJsonArray routesArray;
    for (const QPair<QString, QString> &route : addedRoutes)
    {
        QJsonObject routeObj;
        routeObj["start_terminal"] = route.first;
        routeObj["end_terminal"]   = route.second;
        routesArray.append(routeObj);
    }

    return routesArray;
}

QVariant CommandProcessor::handleGetTerminal(const QVariantMap &params)
{
    if (!params.contains("terminal_name"))
    {
        throw std::invalid_argument("Missing terminal_name parameter");
    }

    QString terminalName = params["terminal_name"].toString();

    Terminal *terminal = m_graph->getTerminal(terminalName);
    return terminal->toJson();
}

QVariant CommandProcessor::handleFindShortestPath(const QVariantMap &params)
{
    if (!params.contains("start_terminal") || !params.contains("end_terminal"))
    {
        throw std::invalid_argument("Missing start_terminal or"
                                    " end_terminal parameter");
    }

    QString startTerminal = params["start_terminal"].toString();
    QString endTerminal   = params["end_terminal"].toString();

    // Extract mode (optional)
    TransportationMode mode = TransportationMode::Any; // Default
    if (params.contains("mode"))
    {
        QVariant modeVar = params["mode"];

        if (modeVar.canConvert<int>())
        {
            mode = static_cast<TransportationMode>(modeVar.toInt());
        }
        else if (modeVar.canConvert<QString>())
        {
            mode = EnumUtils::stringToTransportationMode(modeVar.toString());
        }
    }

    // Find the shortest path
    QList<PathSegment> pathSegments =
        m_graph->findShortestPath(startTerminal, endTerminal, mode);

    // Convert path segments to JSON array using the toJson() method
    QJsonArray pathArray;
    for (const PathSegment &segment : pathSegments)
    {
        pathArray.append(segment.toJson());
    }

    return pathArray;
}

QVariant CommandProcessor::handleFindTopPaths(const QVariantMap &params)
{
    if (!params.contains("start_terminal") || !params.contains("end_terminal"))
    {
        throw std::invalid_argument("Missing start_terminal or "
                                    "end_terminal parameter");
    }

    QString startTerminal = params["start_terminal"].toString();
    QString endTerminal   = params["end_terminal"].toString();

    // Extract number of paths (optional)
    int n = params.value("n", 5).toInt();

    // Extract mode (optional)
    TransportationMode mode = TransportationMode::Truck; // Default
    if (params.contains("mode"))
    {
        QVariant modeVar = params["mode"];

        if (modeVar.canConvert<int>())
        {
            mode = static_cast<TransportationMode>(modeVar.toInt());
        }
        else if (modeVar.canConvert<QString>())
        {
            mode = EnumUtils::stringToTransportationMode(modeVar.toString());
        }
    }

    // Extract skip option (optional)
    bool skipSameModeTerminalDelaysAndCosts =
        params.value("skip_same_mode_terminal_delays_and_costs", true).toBool();

    // Get paths
    QList<Path> paths =
        m_graph->findTopNShortestPaths(startTerminal, endTerminal, n, mode,
                                       skipSameModeTerminalDelaysAndCosts);

    QJsonObject pathsJson;
    pathsJson["start_terminal"] = startTerminal;
    pathsJson["end_terminal"]   = endTerminal;

    // Convert paths to JSON array
    QJsonArray pathsArray;
    for (const Path &path : paths)
    {
        pathsArray.append(path.toJson());
    }
    pathsJson["paths"] = pathsArray;

    return pathsJson;
}

QVariant CommandProcessor::handleAddContainer(const QVariantMap &params)
{
    QString terminalId = params.value("terminal_id").toString();
    double  addingTime = params.value("adding_time", -1).toDouble();

    if (terminalId.isEmpty() || !params.contains("container"))
    {
        throw std::invalid_argument("Terminal ID and container "
                                    "must be provided");
    }

    Terminal *terminal = getTerminalFromParams(params);

    // Parse container JSON
    QVariant    containerVar = params["container"];
    QJsonObject containerJson;

    if (containerVar.canConvert<QString>())
    {
        // Parse JSON string
        QJsonDocument doc =
            QJsonDocument::fromJson(containerVar.toString().toUtf8());
        if (!doc.isObject())
        {
            throw std::invalid_argument("Invalid JSON format for container");
        }
        containerJson = doc.object();
    }
    else if (containerVar.canConvert<QVariantMap>())
    {
        // Convert variant map to JSON object
        containerJson = QJsonObject::fromVariantMap(containerVar.toMap());
    }
    else
    {
        throw std::invalid_argument("Container must be a JSON "
                                    "string or object");
    }

    // Create container object
    ContainerCore::Container container(containerJson);

    // Extract arrival mode if provided
    TransportationMode arrivalMode = TransportationMode::Any;
    if (params.contains("arrival_mode")) {
        arrivalMode = EnumUtils::stringToTransportationMode(
            params.value("arrival_mode").toString());
    }

    // Add container to terminal with mode-specific delay
    terminal->addContainer(container, addingTime, arrivalMode);

    return true;
}

QVariant CommandProcessor::serializeResponse(const QVariant &result)
{
    // If result is already a QVariant, return it directly
    if (result.typeId() == QMetaType::QVariant)
    {
        return result;
    }

    // For QJsonValue, QJsonObject, QJsonArray, convert to QVariant
    if (result.canConvert<QJsonValue>())
    {
        return QVariant::fromValue(result.value<QJsonValue>().toVariant());
    }

    // For other types, return as is
    return result;
}

QVariantMap CommandProcessor::deserializeParams(const QVariantMap &params)
{
    QVariantMap result;

    for (auto it = params.constBegin(); it != params.constEnd(); ++it)
    {
        const QString  &key   = it.key();
        const QVariant &value = it.value();

        // Handle different types of parameters
        if (key == "container" || key == "containers_json"
            || value.typeId() == QMetaType::QString)
        {
            // For container JSON or string values, leave as is
            result[key] = value;
        }
        else if (value.canConvert<QVariantList>())
        {
            // For lists, process each item
            QVariantList list = value.toList();
            QVariantList processedList;

            for (const QVariant &item : list)
            {
                if (item.canConvert<QVariantMap>())
                {
                    processedList.append(deserializeParams(item.toMap()));
                }
                else
                {
                    processedList.append(item);
                }
            }

            result[key] = processedList;
        }
        else if (value.canConvert<QVariantMap>())
        {
            // For maps, recursively process
            result[key] = deserializeParams(value.toMap());
        }
        else
        {
            // For other types, handle special cases
            result[key] = value;
        }
    }

    return result;
}

} // namespace TerminalSim
