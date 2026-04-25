#include "terminal.h"
#include "dwell_time/container_dwell_time.h"

#include <algorithm>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QVariant>
#include <QFileInfo>
#include <QDateTime>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <random>

#include "common/LogCategories.h"

namespace TerminalSim {

namespace
{

using NoHauler = ContainerCore::Container::HaulerType;

QVariant customVariable(const ContainerCore::Container &container,
                        const char                      *key)
{
    return container.getCustomVariable(NoHauler::noHauler,
                                       QString::fromUtf8(key));
}

QString customVariableString(const ContainerCore::Container &container,
                             const char                      *key)
{
    const QVariant value = customVariable(container, key);
    return value.isValid() ? value.toString() : QString();
}

int customVariableInt(const ContainerCore::Container &container,
                      const char                      *key,
                      int                              defaultValue = -1)
{
    const QVariant value = customVariable(container, key);
    bool           ok    = false;
    const int      out   = value.toInt(&ok);
    return ok ? out : defaultValue;
}

double expectedBaseDwellSeconds(const QString     &method,
                                const QVariantMap &params)
{
    if (method.compare("gamma", Qt::CaseInsensitive) == 0)
    {
        return params.value("shape", 2.0).toDouble()
               * params.value("scale", 24.0 * 3600.0).toDouble();
    }
    if (method.compare("exponential", Qt::CaseInsensitive) == 0)
    {
        return params.value("scale", 2.0 * 24.0 * 3600.0).toDouble();
    }
    if (method.compare("normal", Qt::CaseInsensitive) == 0)
    {
        return params.value("mean", 2.0 * 24.0 * 3600.0).toDouble();
    }
    if (method.compare("lognormal", Qt::CaseInsensitive) == 0)
    {
        const double mean =
            params.value("mean", std::log(2.0 * 24.0 * 3600.0)).toDouble();
        const double sigma = params.value("sigma", 0.25).toDouble();
        return std::exp(mean + (sigma * sigma / 2.0));
    }

    return 2.0 * 24.0 * 3600.0;
}

} // namespace

QJsonObject TerminalHandlingBatchRecord::toJson() const
{
    QJsonObject json;
    json["execution_id"] = executionId;
    json["path_identity"] = pathIdentity;
    json["scenario_terminal_id"] = scenarioTerminalId;
    json["runtime_terminal_id"] = runtimeTerminalId;
    json["terminal_sequence_index"] = terminalSequenceIndex;
    json["segment_index"] = segmentIndex;
    json["vehicle_id"] = vehicleId;
    json["vehicle_mode"] =
        EnumUtils::transportationModeToString(vehicleMode);
    json["event_type"] = eventType;
    json["container_count"] = containerCount;
    json["event_time"] = eventTime;
    json["sum_yard_dwell_seconds"] = sumYardDwellSeconds;
    json["sum_customs_delay_seconds"] = sumCustomsDelaySeconds;
    json["customs_applied_count"] = customsAppliedCount;
    json["sum_arrival_penalty_seconds"] = sumArrivalPenaltySeconds;
    json["sum_total_handling_seconds"] = sumTotalHandlingSeconds;
    json["sum_direct_cost_usd"] = sumDirectCostUsd;
    json["state_snapshot_before"] = stateSnapshotBefore;
    json["state_snapshot_after"] = stateSnapshotAfter;

    QJsonArray ids;
    for (const auto &id : containerIds)
        ids.append(id);
    json["container_ids"] = ids;
    return json;
}

QJsonObject TerminalExecutionResult::toJson() const
{
    QJsonObject json;
    json["execution_id"] = executionId;
    json["path_identity"] = pathIdentity;
    json["scenario_terminal_id"] = scenarioTerminalId;
    json["runtime_terminal_id"] = runtimeTerminalId;
    json["terminal_sequence_index"] = terminalSequenceIndex;
    json["total_dropped_containers"] = totalDroppedContainers;
    json["total_picked_containers"] = totalPickedContainers;
    json["arrival_events"] = arrivalEvents;
    json["pickup_events"] = pickupEvents;
    json["actual_yard_dwell_seconds"] = actualYardDwellSeconds;
    json["actual_customs_delay_seconds"] = actualCustomsDelaySeconds;
    json["customs_applied_count"] = customsAppliedCount;
    json["actual_arrival_penalty_seconds"] = actualArrivalPenaltySeconds;
    json["actual_total_handling_seconds"] = actualTotalHandlingSeconds;
    json["actual_direct_cost_usd"] = actualDirectCostUsd;
    json["first_arrival_state_snapshot"] = firstArrivalStateSnapshot;
    json["last_departure_state_snapshot"] = lastDepartureStateSnapshot;
    json["raw_batch_records"] = rawBatchRecords;
    return json;
}

bool Terminal::HandlingMetadata::isValid() const
{
    return !executionId.isEmpty() && !pathIdentity.isEmpty()
        && !scenarioTerminalId.isEmpty();
}

QString Terminal::HandlingMetadata::groupingKey() const
{
    return executionId + "|" + pathIdentity + "|"
        + scenarioTerminalId + "|" + runtimeTerminalId + "|"
        + QString::number(terminalSequenceIndex);
}

Terminal::Terminal(
    const QString &terminalName, const QString &displayName,
    const QMap<TerminalInterface, QSet<TransportationMode>> &interfaces,
    const QMap<QPair<TransportationMode, QString>, QString> &modeNetworkAliases,
    const QVariantMap &capacity, const QVariantMap &dwellTime,
    const QVariantMap &customs, const QVariantMap &cost,
    const QVariantMap &systemDynamics,
    const QString &pathToTerminalFolder)
    : QObject(nullptr)
    , m_terminalName(terminalName)
    , m_displayName(displayName)
    , m_interfaces(interfaces)
    , m_modeNetworkAliases(modeNetworkAliases)
    , m_maxCapacity(std::numeric_limits<int>::max())
    , m_criticalThreshold(0.9)
    , m_customsProbability(0.0)
    , m_customsDelayMean(0.0)
    , m_customsDelayVariance(0.0)
    , m_fixedCost(0.0)
    , m_customsCost(0.0)
    , m_riskFactor(0.0)
    , m_storage(nullptr)
    , m_folderPath(pathToTerminalFolder)
    , m_sdParams()
    , m_sdState()
{
    // Process capacity parameters
    if (!capacity.isEmpty()) {
        QVariant maxCapacityVariant =
            capacity.value("max_capacity",
                           QVariant(std::numeric_limits<int>::max()));
        if (!maxCapacityVariant.isNull()) {
            m_maxCapacity = maxCapacityVariant.toInt();
        }
        
        if (capacity.contains("critical_threshold")) {
            QVariant thresholdVariant = capacity.value("critical_threshold");
            if (thresholdVariant.isNull()) {
                m_criticalThreshold = -1.0; // No threshold
            } else {
                m_criticalThreshold = thresholdVariant.toDouble();
            }
        }
    }
    
    // Process dwell time parameters
    if (!dwellTime.isEmpty()) {
        m_dwellTimeMethod = dwellTime.value("method").toString();
        m_dwellTimeParameters = dwellTime.value("parameters").toMap();
        
        // Ensure numerical parameters are stored as doubles
        QVariantMap cleanParams;
        for (auto it = m_dwellTimeParameters.constBegin();
             it != m_dwellTimeParameters.constEnd(); ++it) {
            if (it.value().isValid() && !it.value().isNull()) {
                cleanParams[it.key()] = it.value().toDouble();
            }
        }
        m_dwellTimeParameters = cleanParams;
    }
    
    // Process customs parameters (all time fields in seconds;
    // variance in seconds²). Historical schema used hours; see
    // 2026-04-17 time-unit unification.
    if (!customs.isEmpty()) {
        m_customsProbability =
            customs.value("probability", 0.0).toDouble();
        m_customsDelayMean =
            customs.value("delay_mean", 0.0).toDouble();
        m_customsDelayVariance =
            customs.value("delay_variance", 0.0).toDouble();
    }
    
    // Process cost parameters
    if (!cost.isEmpty()) {
        m_fixedCost = cost.value("fixed_fees", 0.0).toDouble();
        m_customsCost = cost.value("customs_fees", 0.0).toDouble();
        m_riskFactor = cost.value("risk_factor", 0.0).toDouble();
    }

    // Process system dynamics parameters (disabled by default)
    if (!systemDynamics.isEmpty()) {
        m_sdParams.enabled = systemDynamics.value("enabled", false).toBool();
        m_sdParams.criticalUtilization =
            systemDynamics.value("critical_utilization", 0.7).toDouble();
        m_sdParams.congestionExponent =
            systemDynamics.value("congestion_exponent", 2.0).toDouble();
        m_sdParams.congestionSensitivity =
            systemDynamics.value("congestion_sensitivity", 1.0).toDouble();
        m_sdParams.delaySensitivity =
            systemDynamics.value("delay_sensitivity", 0.5).toDouble();
        m_sdParams.maxServiceRate =
            systemDynamics.value("max_service_rate", 100.0).toDouble();

        // Parse mode-specific delay parameters
        if (systemDynamics.contains("ship_delay_alpha")) {
            m_sdParams.shipDelay.alpha =
                systemDynamics.value("ship_delay_alpha").toDouble();
        }
        if (systemDynamics.contains("ship_delay_beta")) {
            m_sdParams.shipDelay.beta =
                systemDynamics.value("ship_delay_beta").toDouble();
        }
        if (systemDynamics.contains("truck_delay_alpha")) {
            m_sdParams.truckDelay.alpha =
                systemDynamics.value("truck_delay_alpha").toDouble();
        }
        if (systemDynamics.contains("truck_delay_beta")) {
            m_sdParams.truckDelay.beta =
                systemDynamics.value("truck_delay_beta").toDouble();
        }
        if (systemDynamics.contains("train_delay_alpha")) {
            m_sdParams.trainDelay.alpha =
                systemDynamics.value("train_delay_alpha").toDouble();
        }
        if (systemDynamics.contains("train_delay_beta")) {
            m_sdParams.trainDelay.beta =
                systemDynamics.value("train_delay_beta").toDouble();
        }

        // Parse arrival-side penalty parameters (seconds at full congestion)
        if (systemDynamics.contains("ship_arrival_penalty")) {
            m_sdParams.shipArrivalPenalty =
                systemDynamics.value("ship_arrival_penalty").toDouble();
        }
        if (systemDynamics.contains("truck_arrival_penalty")) {
            m_sdParams.truckArrivalPenalty =
                systemDynamics.value("truck_arrival_penalty").toDouble();
        }
        if (systemDynamics.contains("train_arrival_penalty")) {
            m_sdParams.trainArrivalPenalty =
                systemDynamics.value("train_arrival_penalty").toDouble();
        }

        // Initialize SD state if enabled
        if (m_sdParams.enabled) {
            m_sdState.serviceCapacity = m_sdParams.maxServiceRate;
            m_sdState.delayMultiplier = 1.0;
            qCDebug(lcTerminal) << "System dynamics enabled for terminal" << m_terminalName
                               << "with critical utilization:" << m_sdParams.criticalUtilization
                               << "max service rate:" << m_sdParams.maxServiceRate;
        }
    }

    // Initialize storage
    if (m_folderPath.isEmpty() || !QDir(m_folderPath).exists()) {
        m_storage = new ContainerCore::ContainerMap();
        m_sqlFile = QString();
    } else {
        QDir storageDir = QDir(m_folderPath);
        if (!storageDir.exists()) {
            storageDir.mkpath(".");
        }
        
        m_sqlFile = storageDir.filePath(m_terminalName + ".sql");
        m_storage = new ContainerCore::ContainerMap(m_sqlFile);
    }
    
    qCDebug(lcTerminal) << "Terminal" << m_terminalName
                       << "initialized with" << m_interfaces.size()
                       << "interfaces and max capacity:"
                       << (m_maxCapacity == std::numeric_limits<int>::max() ?
                               "unlimited" : QString::number(m_maxCapacity));
}

Terminal::~Terminal()
{
    qCDebug(lcTerminal) << "Destroying terminal" << m_terminalName;
    delete m_storage;
}

QString Terminal::getAliasByModeNetwork(TransportationMode mode,
                                        const QString& network) const
{
    QMutexLocker locker(&m_mutex);
    return m_modeNetworkAliases.value(qMakePair(mode, network));
}

void Terminal::addAliasForModeNetwork(TransportationMode mode,
                                      const QString& network,
                                      const QString& alias)
{
    QMutexLocker locker(&m_mutex);
    m_modeNetworkAliases[qMakePair(mode, network)] = alias;
    qCDebug(lcTerminal) << "Added alias" << alias
                       << "for terminal" << m_terminalName
                       << "with mode" << static_cast<int>(mode)
                       << "and network" << network;
}

QPair<bool, QString>
Terminal::checkCapacityStatusInternal(int additionalContainers) const
{
    // Caller must hold m_mutex
    int currentCount = m_storage->size();
    int newCount = currentCount + additionalContainers;

    // If unlimited capacity
    if (m_maxCapacity == std::numeric_limits<int>::max()) {
        return qMakePair(true, QString("OK"));
    }

    // Check if exceeds max capacity
    if (newCount > m_maxCapacity) {
        return qMakePair(false,
                         QString("Exceeds max capacity of %1")
                             .arg(m_maxCapacity));
    }

    // If no critical threshold is set
    if (m_criticalThreshold < 0.0) {
        return qMakePair(true, QString("OK"));
    }

    // Check against critical threshold
    double criticalLimit = m_maxCapacity * m_criticalThreshold;
    if (newCount > criticalLimit) {
        return qMakePair(false,
                         QString("Exceeds critical threshold (%1% of %2)")
                             .arg(m_criticalThreshold * 100)
                             .arg(m_maxCapacity));
    }

    // Check against warning threshold (90% of critical threshold)
    double warningLimit = criticalLimit * 0.9;
    if (newCount > warningLimit) {
        return qMakePair(true,
                         QString("Warning: Approaching critical "
                                 "capacity (%1/%2)")
                             .arg(newCount).arg(qRound(criticalLimit)));
    }

    return qMakePair(true, QString("OK"));
}

QPair<bool, QString>
Terminal::checkCapacityStatus(int additionalContainers) const
{
    QMutexLocker locker(&m_mutex);
    return checkCapacityStatusInternal(additionalContainers);
}

double Terminal::estimateContainerHandlingTime() const
{
    QMutexLocker locker(&m_mutex);

    double totalSeconds = 0.0;

    // 1. Dwell Time (Storage Duration) — already in seconds.
    if (!m_dwellTimeParameters.isEmpty()) {
        totalSeconds += ContainerDwellTime::getDepartureTime(
                            0.0,
                            m_dwellTimeMethod.isEmpty() ?
                                "gamma" : m_dwellTimeMethod,
                            m_dwellTimeParameters);
    }

    // 2. Customs Inspection (Expected) — delay_mean is seconds.
    if (m_customsProbability > 0.0 && m_customsDelayMean > 0.0) {
        totalSeconds += m_customsProbability * m_customsDelayMean;
    }

    return totalSeconds;
}

double
Terminal::estimateContainerCostInternal(
    const ContainerCore::Container *container,
    bool applyCustoms) const
{
    // Caller must hold m_mutex
    double totalCost = 0.0;

    // Add fixed cost if applicable
    if (m_fixedCost > 0.0) {
        totalCost += m_fixedCost;
    }

    // Add customs cost if applicable
    if (applyCustoms && m_customsCost > 0.0) {
        totalCost += m_customsCost;
    }

    // Apply risk factor based on container value if applicable
    if (container != nullptr && m_riskFactor > 0.0) {
        QVariant dollarValue = container->getCustomVariable(
            ContainerCore::Container::Container::HaulerType::noHauler,
            "dollar_value");

        if (dollarValue.isValid() && !dollarValue.toString().isEmpty()) {
            bool ok;
            double value = dollarValue.toDouble(&ok);
            if (ok) {
                totalCost += value * m_riskFactor;
            }
        }
    }

    return totalCost;
}

double
Terminal::estimateContainerCost(const ContainerCore::Container *container,
                                bool applyCustoms) const
{
    QMutexLocker locker(&m_mutex);
    return estimateContainerCostInternal(container, applyCustoms);
}

double Terminal::estimateTotalCostByWeights(
    double delayConst, double costWeight,
    const ContainerCore::Container *container) const
{
    return estimateContainerHandlingTime() * delayConst
           + estimateContainerCost(container) * costWeight;
}

bool Terminal::canAcceptTransport(TransportationMode mode,
                                  TerminalInterface side) const
{
    QMutexLocker locker(&m_mutex);
    
    auto it = m_interfaces.find(side);
    if (it == m_interfaces.end()) {
        return false;
    }
    
    return it.value().contains(mode);
}

void Terminal::addContainer(const ContainerCore::Container& container,
                            double addingTime,
                            TransportationMode arrivalMode)
{
    QMutexLocker locker(&m_mutex);
    const QJsonObject before = runtimeTerminalSnapshotLocked();
    const HandlingMetadata metadata =
        extractHandlingMetadataLocked(container, arrivalMode);
    const auto outcome = handleContainerArrivalLocked(
        container, addingTime, arrivalMode);
    recordHandlingBatchLocked(metadata, QList<ContainerHandlingOutcome>{outcome}, before,
                              runtimeTerminalSnapshotLocked(),
                              QStringLiteral("arrival_dropoff"));
}

void
Terminal::addContainers(const QVector<ContainerCore::Container>& containers,
                        double addingTime,
                        TransportationMode arrivalMode)
{
    QMutexLocker locker(&m_mutex);

    // Check capacity before adding containers (internal — we hold m_mutex)
    int containerCount = containers.size();

    QPair<bool, QString> capacityStatus = checkCapacityStatusInternal(containerCount);
    if (!capacityStatus.first) {
        qCWarning(lcTerminal) << "Cannot add" << containerCount
                             << "containers to terminal" << m_terminalName
                             << ":" << capacityStatus.second;
        throw std::runtime_error(
            QString("Cannot add %1 containers: %2")
                .arg(containerCount).arg(capacityStatus.second).toStdString()
            );
    }
    
    if (capacityStatus.second.startsWith("Warning")) {
        qCWarning(lcTerminal) << "Terminal" << m_terminalName
                              << ":" << capacityStatus.second;
    }

    const QJsonObject stateSnapshotBefore =
        runtimeTerminalSnapshotLocked();

    QHash<QString, HandlingMetadata> groupedMetadata;
    QHash<QString, QList<ContainerHandlingOutcome>> groupedOutcomes;

    for (const ContainerCore::Container& container : containers) {
        const HandlingMetadata metadata =
            extractHandlingMetadataLocked(container, arrivalMode);
        const QString key = metadata.groupingKey();
        if (!groupedMetadata.contains(key))
            groupedMetadata.insert(key, metadata);
        groupedOutcomes[key].append(handleContainerArrivalLocked(
            container, addingTime, arrivalMode));
    }

    const QJsonObject stateSnapshotAfter =
        runtimeTerminalSnapshotLocked();
    for (auto it = groupedOutcomes.constBegin();
         it != groupedOutcomes.constEnd(); ++it) {
        recordHandlingBatchLocked(groupedMetadata.value(it.key()),
                                  it.value(),
                                  stateSnapshotBefore,
                                  stateSnapshotAfter,
                                  QStringLiteral("arrival_dropoff"));
    }
}

void Terminal::addContainersFromJson(const QJsonObject& containers,
                                     double addingTime,
                                     TransportationMode arrivalMode)
{
    // Parse the containers from JSON
    QVector<ContainerCore::Container> containerList;
    
    try {
        // Check if it's a container array
        if (containers.contains("containers") &&
            containers["containers"].isArray()) {
            QJsonArray containerArray = containers["containers"].toArray();
            for (const QJsonValue& value : containerArray) {
                if (value.isObject()) {
                    ContainerCore::Container container(value.toObject());
                    containerList.append(container);
                }
            }
        } 
        // Check if it's a single container object with container properties
        else if (containers.contains("containerID")) {
            ContainerCore::Container container(containers);
            containerList.append(container);
        }
        // Otherwise, treat the whole object as a map of containers
        else {
            for (auto it = containers.constBegin();
                 it != containers.constEnd(); ++it) {
                if (it.value().isObject()) {
                    ContainerCore::Container container(it.value().toObject());
                    containerList.append(container);
                }
            }
        }
    } catch (const std::exception& e) {
        qCWarning(lcTerminal) << "Error parsing containers from JSON:"
                              << e.what();
        throw std::runtime_error(QString("Invalid container JSON: %1")
                                     .arg(e.what()).toStdString());
    }
    
    int containerCount = containerList.size();
    if (containerCount == 0) {
        qCWarning(lcTerminal) << "No valid containers found in JSON";
        return;
    }

    qCDebug(lcTerminal) << "Adding" << containerCount
                        << "containers from JSON to terminal" << m_terminalName;
    
    // Add the containers
    addContainers(containerList, addingTime, arrivalMode);
}

QJsonArray
Terminal::getContainersByDepatingTime(double departingTime,
                                      const QString& condition) const
{
    QMutexLocker locker(&m_mutex);
    
    // Validate condition
    QStringList validConditions = {"<", "<=", ">", ">=", "==", "!="};
    if (!validConditions.contains(condition)) {
        qCWarning(lcTerminal) << "Invalid condition for getContainersByDepatingTime:"
                              << condition;
        throw std::invalid_argument(
            QString("Invalid condition: %1. Must be one of: "
                    "<, <=, >, >=, ==, !=").arg(condition).toStdString()
            );
    }
    
    // Get containers from storage based on departure time
    QVector<ContainerCore::Container *> containers =
        m_storage->getContainersByLeavingTime(condition, departingTime);
    
    // Convert to JSON array
    QJsonArray result;
    for (const ContainerCore::Container* container : containers) {
        result.append(container->toJson());
    }
    
    qCDebug(lcTerminal) << "Found" << result.size()
                       << "containers with departure time" <<
        condition << departingTime
                       << "in terminal" << m_terminalName;
    
    return result;
}

QJsonArray
Terminal::getContainersByAddedTime(double addedTime,
                                   const QString& condition) const
{
    QMutexLocker locker(&m_mutex);
    
    // Validate condition
    QStringList validConditions = {"<", "<=", ">", ">=", "==", "!="};
    if (!validConditions.contains(condition)) {
        qCWarning(lcTerminal) << "Invalid condition for getContainersByAddedTime:"
                              << condition;
        throw std::invalid_argument(
            QString("Invalid condition: %1. Must be one of: "
                    "<, <=, >, >=, ==, !=").arg(condition).toStdString()
            );
    }
    
    // Get containers from storage based on added time
    QVector<ContainerCore::Container *> containers =
        m_storage->getContainersByAddedTime(condition, addedTime);
    
    // Convert to JSON array
    QJsonArray result;
    for (const ContainerCore::Container* container : containers) {
        result.append(container->toJson());
    }
    
    qCDebug(lcTerminal) << "Found" << result.size()
                       << "containers with added time" << condition << addedTime
                       << "in terminal" << m_terminalName;
    
    return result;
}

QJsonArray
Terminal::getContainersByNextDestination(const QString& destination) const
{
    QMutexLocker locker(&m_mutex);
    
    // Get containers from storage based on next destination
    QVector<ContainerCore::Container *> containers =
        m_storage->getContainersByNextDestination(destination);
    
    // Convert to JSON array
    QJsonArray result;
    for (const ContainerCore::Container* container : containers) {
        result.append(container->toJson());
    }
    
    qCDebug(lcTerminal) << "Found" << result.size()
                       << "containers with next destination" << destination
                       << "in terminal" << m_terminalName;
    
    return result;
}

QJsonArray
Terminal::dequeueContainersByNextDestination(const QString& destination)
{
    QMutexLocker locker(&m_mutex);

    // Get and remove containers from storage based on next destination
    QVector<ContainerCore::Container *> containers =
        m_storage->dequeueContainersByNextDestination(destination);

    int requestedCount = containers.size();
    int servedCount = requestedCount;

    // Apply System Dynamics service capacity limit
    if (m_sdParams.enabled && requestedCount > 0) {
        int remainingCapacity = capacityThisStep() - m_sdState.departuresThisStep;

        if (remainingCapacity <= 0) {
            // No capacity remaining - re-add all containers and return empty
            for (ContainerCore::Container* container : containers) {
                m_storage->addContainer(
                    container->getContainerID(), container,
                    0.0, 0.0); // Re-add with original times (will be overwritten)
            }
            qCWarning(lcTerminal) << "SD: Service capacity exhausted at terminal" << m_terminalName
                                  << "- cannot serve" << requestedCount << "containers";
            return QJsonArray();
        }

        if (requestedCount > remainingCapacity) {
            // Partial service - re-add excess containers
            servedCount = remainingCapacity;
            for (int i = servedCount; i < requestedCount; ++i) {
                ContainerCore::Container* container = containers[i];
                m_storage->addContainer(
                    container->getContainerID(), container,
                    0.0, 0.0);
            }
            containers.resize(servedCount);

            qCDebug(lcTerminal) << "SD: Service capacity limited departure at terminal"
                               << m_terminalName << "- served" << servedCount
                               << "of" << requestedCount << "requested";
        }

        // Track departures for SD
        m_sdState.departuresThisStep += servedCount;
    }

    // Convert to JSON array
    QJsonArray result;
    for (const ContainerCore::Container* container : containers) {
        result.append(container->toJson());
    }

    qCDebug(lcTerminal) << "Removed" << result.size()
                       << "containers with next destination" << destination
                       << "from terminal" << m_terminalName;

    return result;
}

int Terminal::getContainerCount() const
{
    QMutexLocker locker(&m_mutex);
    return m_storage->size();
}

int Terminal::getAvailableCapacity() const
{
    // Create a copy of needed values with minimal locking
    QMutexLocker locker(&m_mutex);
    int maxCap = m_maxCapacity;
    locker.unlock();

    // Get the container count
    int currentCount =
        getContainerCount(); // This method will handle its own locking

    if (maxCap == std::numeric_limits<int>::max()) {
        return std::numeric_limits<int>::max();
    }

    return maxCap - currentCount;
}

int Terminal::getMaxCapacity() const
{
    QMutexLocker locker(&m_mutex);
    return m_maxCapacity;
}

void Terminal::clear()
{
    QMutexLocker locker(&m_mutex);
    
    qCDebug(lcTerminal) << "Clearing all containers from terminal" << m_terminalName;
    m_storage->clear();
}

QJsonObject Terminal::toJson() const
{
    QMutexLocker locker(&m_mutex);

    QJsonObject json;

    // Terminal properties
    json["terminal_name"] = m_terminalName;
    json["display_name"]  = m_displayName;

    // Interfaces
    QJsonObject interfacesJson;
    for (auto it = m_interfaces.constBegin();
         it != m_interfaces.constEnd(); ++it) {
        QJsonArray modesArray;
        for (TransportationMode mode : it.value()) {
            modesArray.append(static_cast<int>(mode));
        }
        interfacesJson[QString::number(static_cast<int>(it.key()))] =
            modesArray;
    }
    json["interfaces"] = interfacesJson;

    // Mode network aliases
    QJsonObject aliasesJson;
    for (auto it = m_modeNetworkAliases.constBegin();
         it != m_modeNetworkAliases.constEnd(); ++it) {
        QString key = QString("%1:%2")
        .arg(static_cast<int>(it.key().first)).arg(it.key().second);
        aliasesJson[key] = it.value();
    }
    json["mode_network_aliases"] = aliasesJson;

    // Capacity
    QJsonObject capacityJson;
    capacityJson["max_capacity"] =
        (m_maxCapacity == std::numeric_limits<int>::max())
            ? QJsonValue(QJsonValue::Null)
            : QJsonValue(m_maxCapacity);
    capacityJson["critical_threshold"] =
        (m_criticalThreshold < 0)
            ? QJsonValue(QJsonValue::Null)
            : QJsonValue(m_criticalThreshold);
    json["capacity"] = capacityJson;

    // Dwell time
    QJsonObject dwellTimeJson;
    dwellTimeJson["method"] = m_dwellTimeMethod;

    QJsonObject parametersJson;
    for (auto it = m_dwellTimeParameters.constBegin();
         it != m_dwellTimeParameters.constEnd(); ++it) {
        parametersJson[it.key()] = it.value().toDouble();
    }
    dwellTimeJson["parameters"] = parametersJson;
    json["dwell_time"] = dwellTimeJson;

    // Customs
    QJsonObject customsJson;
    customsJson["probability"] = m_customsProbability;
    customsJson["delay_mean"] = m_customsDelayMean;
    customsJson["delay_variance"] = m_customsDelayVariance;
    json["customs"] = customsJson;

    // Cost
    QJsonObject costJson;
    costJson["fixed_fees"] = m_fixedCost;
    costJson["customs_fees"] = m_customsCost;
    costJson["risk_factor"] = m_riskFactor;
    json["cost"] = costJson;

    // Storage information - Direct access instead of
    // calling methods that lock the mutex
    json["container_count"] =
        m_storage->size(); // Direct access instead of getContainerCount()

    // Calculate available capacity directly
    int availableCapacity =
        (m_maxCapacity == std::numeric_limits<int>::max()) ?
            std::numeric_limits<int>::max() :
            (m_maxCapacity - m_storage->size());
    json["available_capacity"] = availableCapacity;

    // SQL file path if available
    if (!m_sqlFile.isEmpty()) {
        QFileInfo fileInfo(m_sqlFile);
        if (fileInfo.exists()) {
            json["sql_file"] = m_sqlFile;
            json["sql_file_size"] = fileInfo.size();
            json["sql_file_modified"] =
                fileInfo.lastModified().toString(Qt::ISODate);
        }
    }

    // System Dynamics configuration and state
    QJsonObject sdJson;
    sdJson["enabled"] = m_sdParams.enabled;
    sdJson["critical_utilization"] = m_sdParams.criticalUtilization;
    sdJson["congestion_exponent"] = m_sdParams.congestionExponent;
    sdJson["congestion_sensitivity"] = m_sdParams.congestionSensitivity;
    sdJson["delay_sensitivity"] = m_sdParams.delaySensitivity;
    sdJson["max_service_rate"] = m_sdParams.maxServiceRate;

    // Mode-specific delay parameters
    QJsonObject modeDelayJson;
    QJsonObject shipDelayJson;
    shipDelayJson["alpha"] = m_sdParams.shipDelay.alpha;
    shipDelayJson["beta"] = m_sdParams.shipDelay.beta;
    modeDelayJson["ship"] = shipDelayJson;

    QJsonObject truckDelayJson;
    truckDelayJson["alpha"] = m_sdParams.truckDelay.alpha;
    truckDelayJson["beta"] = m_sdParams.truckDelay.beta;
    modeDelayJson["truck"] = truckDelayJson;

    QJsonObject trainDelayJson;
    trainDelayJson["alpha"] = m_sdParams.trainDelay.alpha;
    trainDelayJson["beta"] = m_sdParams.trainDelay.beta;
    modeDelayJson["train"] = trainDelayJson;

    sdJson["mode_delay_params"] = modeDelayJson;

    // Arrival penalty parameters
    QJsonObject arrivalPenaltyJson;
    arrivalPenaltyJson["ship"] = m_sdParams.shipArrivalPenalty;
    arrivalPenaltyJson["truck"] = m_sdParams.truckArrivalPenalty;
    arrivalPenaltyJson["train"] = m_sdParams.trainArrivalPenalty;
    sdJson["arrival_penalties"] = arrivalPenaltyJson;

    json["system_dynamics"] = sdJson;

    // Include current SD state if enabled
    if (m_sdParams.enabled) {
        QJsonObject sdStateJson;
        sdStateJson["utilization"] = m_sdState.utilization;
        sdStateJson["congestion"] = m_sdState.congestion;
        sdStateJson["service_capacity"] = m_sdState.serviceCapacity;
        sdStateJson["delay_multiplier"] = m_sdState.delayMultiplier;
        json["system_dynamics_state"] = sdStateJson;
    }

    return json;
}

Terminal* Terminal::fromJson(const QJsonObject& json,
                             const QString& pathToTerminalFolder)
{
    // Extract terminal name
    if (!json.contains("terminal_name") ||
        !json["terminal_name"].isString()) {
        qCWarning(lcTerminal) << "Missing or invalid terminal_name in JSON";
        return nullptr;
    }
    QString terminalName = json["terminal_name"].toString();
    QString displayName  = json["display_name"].toString();

    // Extract interfaces — accept both "interfaces" (canonical) and
    // "terminal_interfaces" (CargoNetSim wire key) for compatibility.
    QMap<TerminalInterface,
         QSet<TransportationMode>> interfaces;
    const QJsonValue ifaceVal = json.contains("interfaces")
        ? json.value("interfaces")
        : json.value("terminal_interfaces");
    if (ifaceVal.isObject()) {
        QJsonObject interfacesJson = ifaceVal.toObject();
        for (auto it = interfacesJson.constBegin();
             it != interfacesJson.constEnd(); ++it) {
            bool ok;
            int interfaceInt = it.key().toInt(&ok);
            if (!ok) continue;
            
            TerminalInterface interface =
                static_cast<TerminalInterface>(interfaceInt);
            QSet<TransportationMode> modes;
            
            if (it.value().isArray()) {
                QJsonArray modesArray = it.value().toArray();
                for (const QJsonValue& modeValue : modesArray) {
                    if (modeValue.isDouble()) {
                        int modeInt = modeValue.toInt();
                        modes.insert(static_cast<TransportationMode>(modeInt));
                    }
                }
            }
            
            interfaces[interface] = modes;
        }
    }
    
    // Use custom_config wrapper if present (CargoNetSim sends nested format);
    // fall back to flat top-level for backward compat with other senders.
    const QJsonValue cfgVal = json.value("custom_config");
    const QJsonObject cfg   = cfgVal.isObject() ? cfgVal.toObject() : json;

    // Extract mode network aliases
    QMap<QPair<TransportationMode, QString>, QString> modeNetworkAliases;
    if (cfg.contains("mode_network_aliases") &&
        cfg["mode_network_aliases"].isObject()) {
        modeNetworkAliases = parseModeNetworkAliases(
            cfg["mode_network_aliases"].toObject().toVariantMap());
    }

    // Extract capacity
    QVariantMap capacity;
    if (cfg.contains("capacity") && cfg["capacity"].isObject()) {
        QJsonObject capacityJson = cfg["capacity"].toObject();
        
        if (!capacityJson["max_capacity"].isNull()) {
            capacity["max_capacity"] = capacityJson["max_capacity"].toInt();
        }
        
        if (!capacityJson["critical_threshold"].isNull()) {
            capacity["critical_threshold"] =
                capacityJson["critical_threshold"].toDouble();
        }
    }
    
    // Extract dwell time
    QVariantMap dwellTime;
    if (cfg.contains("dwell_time") && cfg["dwell_time"].isObject()) {
        QJsonObject dwellTimeJson = cfg["dwell_time"].toObject();
        
        if (dwellTimeJson.contains("method") &&
            dwellTimeJson["method"].isString()) {
            dwellTime["method"] = dwellTimeJson["method"].toString();
        }
        
        if (dwellTimeJson.contains("parameters") &&
            dwellTimeJson["parameters"].isObject()) {
            QJsonObject parametersJson =
                dwellTimeJson["parameters"].toObject();
            QVariantMap parameters;
            
            for (auto it = parametersJson.constBegin();
                 it != parametersJson.constEnd(); ++it) {
                parameters[it.key()] = it.value().toDouble();
            }
            
            dwellTime["parameters"] = parameters;
        }
    }
    
    // Extract customs
    QVariantMap customs;
    if (cfg.contains("customs") && cfg["customs"].isObject()) {
        QJsonObject customsJson = cfg["customs"].toObject();
        
        if (customsJson.contains("probability")) {
            customs["probability"] =
                customsJson["probability"].toDouble();
        }
        
        if (customsJson.contains("delay_mean")) {
            customs["delay_mean"] =
                customsJson["delay_mean"].toDouble();
        }
        
        if (customsJson.contains("delay_variance")) {
            customs["delay_variance"] =
                customsJson["delay_variance"].toDouble();
        }
    }
    
    // Extract cost
    QVariantMap cost;
    if (cfg.contains("cost") && cfg["cost"].isObject()) {
        QJsonObject costJson = cfg["cost"].toObject();

        if (costJson.contains("fixed_fees")) {
            cost["fixed_fees"] = costJson["fixed_fees"].toDouble();
        }

        if (costJson.contains("customs_fees")) {
            cost["customs_fees"] = costJson["customs_fees"].toDouble();
        }

        if (costJson.contains("risk_factor")) {
            cost["risk_factor"] = costJson["risk_factor"].toDouble();
        }
    }

    // Extract system dynamics
    QVariantMap systemDynamics;
    if (cfg.contains("system_dynamics") && cfg["system_dynamics"].isObject()) {
        QJsonObject sdJson = cfg["system_dynamics"].toObject();
        systemDynamics = sdJson.toVariantMap();

        if (systemDynamics.contains("mode_delay_params")) {
            QVariantMap modeMap = systemDynamics.value("mode_delay_params").toMap();
            if (modeMap.contains("ship")) {
                QVariantMap ship = modeMap.value("ship").toMap();
                systemDynamics["ship_delay_alpha"] = ship.value("alpha", 0.5);
                systemDynamics["ship_delay_beta"] = ship.value("beta", 2.0);
            }
            if (modeMap.contains("truck")) {
                QVariantMap truck = modeMap.value("truck").toMap();
                systemDynamics["truck_delay_alpha"] = truck.value("alpha", 0.3);
                systemDynamics["truck_delay_beta"] = truck.value("beta", 2.5);
            }
            if (modeMap.contains("train")) {
                QVariantMap train = modeMap.value("train").toMap();
                systemDynamics["train_delay_alpha"] = train.value("alpha", 0.8);
                systemDynamics["train_delay_beta"] = train.value("beta", 3.0);
            }
        }

        if (systemDynamics.contains("arrival_penalties")) {
            QVariantMap penaltyMap = systemDynamics.value("arrival_penalties").toMap();
            if (penaltyMap.contains("ship"))
                systemDynamics["ship_arrival_penalty"] = penaltyMap.value("ship", 14400.0);
            if (penaltyMap.contains("truck"))
                systemDynamics["truck_arrival_penalty"] = penaltyMap.value("truck", 1800.0);
            if (penaltyMap.contains("train"))
                systemDynamics["train_arrival_penalty"] = penaltyMap.value("train", 7200.0);
        }
    }

    // Create terminal
    Terminal *terminal =
        new Terminal(terminalName, displayName, interfaces, modeNetworkAliases,
                     capacity, dwellTime, customs, cost, systemDynamics,
                     pathToTerminalFolder);

    return terminal;
}

QMap<QPair<TransportationMode, QString>, QString>
Terminal::parseModeNetworkAliases(const QVariantMap &aliasesMap)
{
    QMap<QPair<TransportationMode, QString>, QString> result;
    for (auto it = aliasesMap.constBegin(); it != aliasesMap.constEnd(); ++it)
    {
        QStringList parts = it.key().split(':');
        if (parts.size() != 2)
            continue;

        bool ok;
        int  modeInt = parts[0].toInt(&ok);
        if (!ok)
            continue;

        TransportationMode mode    = static_cast<TransportationMode>(modeInt);
        QString            network = parts[1];
        QString            alias   = it.value().toString();

        result[qMakePair(mode, network)] = alias;
    }
    return result;
}

// ============================================================================
// System Dynamics Implementation
// ============================================================================

double Terminal::calculateCongestion(double utilization) const
{
    // Equation 4: G_k(t) = clamp(((U_k - U_crit) / (1 - U_crit))^γ, 0, 1)
    if (utilization <= m_sdParams.criticalUtilization)
    {
        return 0.0;
    }

    double normalized = (utilization - m_sdParams.criticalUtilization) /
                        (1.0 - m_sdParams.criticalUtilization);
    double congestion = std::pow(normalized, m_sdParams.congestionExponent);
    return std::min(1.0, congestion);
}

double Terminal::calculateServiceCapacity(double congestion) const
{
    // Equation 5: S_k^cap = S_max / (1 + β * G_k)
    return m_sdParams.maxServiceRate /
           (1.0 + m_sdParams.congestionSensitivity * congestion);
}

double Terminal::calculateDelayMultiplier(double utilization,
                                          TransportationMode mode) const
{
    // If utilization is below critical threshold, no congestion delay
    if (utilization <= m_sdParams.criticalUtilization) {
        return 1.0;
    }

    // Select mode-specific parameters
    const ModeDelayParams* params = nullptr;
    switch (mode) {
        case TransportationMode::Ship:
            params = &m_sdParams.shipDelay;
            break;
        case TransportationMode::Truck:
            params = &m_sdParams.truckDelay;
            break;
        case TransportationMode::Train:
            params = &m_sdParams.trainDelay;
            break;
        case TransportationMode::Any:
        default:
            // Fallback: use legacy linear formula M = 1 + δ * G_k
            return 1.0 + m_sdParams.delaySensitivity *
                         calculateCongestion(utilization);
    }

    // Continuous BPR-inspired volume-delay function (normalized excess):
    // M_k(t, mode) = 1 + α · ((U_k - U_crit) / (1 - U_crit))^β
    // Continuous at U_k = U_crit (numerator → 0), max multiplier 1+α at U_k = 1.
    double range  = 1.0 - m_sdParams.criticalUtilization;
    double excess = utilization - m_sdParams.criticalUtilization;
    return 1.0 + params->alpha * std::pow(excess / range, params->beta);
}

double Terminal::calculateArrivalPenalty(double utilization,
                                         TransportationMode mode) const
{
    // No penalty below critical utilization
    if (utilization <= m_sdParams.criticalUtilization) {
        return 0.0;
    }

    // Select base penalty for this mode (seconds at full congestion)
    double basePenalty = 0.0;
    switch (mode) {
        case TransportationMode::Ship:
            basePenalty = m_sdParams.shipArrivalPenalty;
            break;
        case TransportationMode::Truck:
            basePenalty = m_sdParams.truckArrivalPenalty;
            break;
        case TransportationMode::Train:
            basePenalty = m_sdParams.trainArrivalPenalty;
            break;
        case TransportationMode::Any:
        default:
            return 0.0;
    }

    // Scale penalty by congestion level G_k(t) in [0, 1]
    double congestion = calculateCongestion(utilization);
    return basePenalty * congestion;
}

QJsonObject Terminal::runtimeTerminalProjectionLocked(
    TransportationMode mode) const
{
    QJsonObject projection;
    projection["terminal_id"] = m_terminalName;
    projection["mode"] =
        EnumUtils::transportationModeToString(mode);

    const double baseDwellSeconds =
        expectedBaseDwellSeconds(m_dwellTimeMethod, m_dwellTimeParameters);
    const double delayMultiplier =
        calculateDelayMultiplier(m_sdState.utilization, mode);
    const double expectedYardDwellSeconds =
        baseDwellSeconds * delayMultiplier;
    const double expectedCustomsDelaySeconds =
        m_customsProbability * m_customsDelayMean;
    const double expectedArrivalPenaltySeconds =
        calculateArrivalPenalty(m_sdState.utilization, mode);
    const double expectedDirectCostUsd =
        m_fixedCost + (m_customsProbability * m_customsCost);

    projection["base_dwell_seconds"] = baseDwellSeconds;
    projection["delay_multiplier"] = delayMultiplier;
    projection["expected_yard_dwell_seconds"] =
        expectedYardDwellSeconds;
    projection["expected_customs_delay_seconds"] =
        expectedCustomsDelaySeconds;
    projection["expected_arrival_penalty_seconds"] =
        expectedArrivalPenaltySeconds;
    projection["expected_total_handling_seconds"] =
        expectedYardDwellSeconds + expectedCustomsDelaySeconds
        + expectedArrivalPenaltySeconds;
    projection["expected_direct_cost_usd"] =
        expectedDirectCostUsd;
    projection["customs_probability"] = m_customsProbability;
    return projection;
}

QJsonObject Terminal::runtimeTerminalSnapshotLocked() const
{
    QJsonObject state;
    state["terminal_id"] = m_terminalName;
    state["display_name"] = m_displayName;

    // Parameters
    QJsonObject params;
    params["enabled"] = m_sdParams.enabled;
    params["critical_utilization"] = m_sdParams.criticalUtilization;
    params["congestion_exponent"] = m_sdParams.congestionExponent;
    params["congestion_sensitivity"] = m_sdParams.congestionSensitivity;
    params["delay_sensitivity"] = m_sdParams.delaySensitivity;
    params["max_service_rate"] = m_sdParams.maxServiceRate;

    QJsonObject modeParams;
    modeParams["ship"] = QJsonObject{
        {"alpha", m_sdParams.shipDelay.alpha},
        {"beta", m_sdParams.shipDelay.beta}};
    modeParams["truck"] = QJsonObject{
        {"alpha", m_sdParams.truckDelay.alpha},
        {"beta", m_sdParams.truckDelay.beta}};
    modeParams["train"] = QJsonObject{
        {"alpha", m_sdParams.trainDelay.alpha},
        {"beta", m_sdParams.trainDelay.beta}};
    params["mode_delay_params"] = modeParams;

    params["arrival_penalties"] = QJsonObject{
        {"ship", m_sdParams.shipArrivalPenalty},
        {"truck", m_sdParams.truckArrivalPenalty},
        {"train", m_sdParams.trainArrivalPenalty}};
    state["parameters"] = params;

    QJsonObject currentState;
    currentState["utilization"] = m_sdState.utilization;
    currentState["congestion"] = m_sdState.congestion;
    currentState["service_capacity"] = m_sdState.serviceCapacity;
    currentState["delay_multiplier"] = m_sdState.delayMultiplier;
    currentState["arrivals_this_step"] = m_sdState.arrivalsThisStep;
    currentState["departures_this_step"] = m_sdState.departuresThisStep;
    currentState["last_update_time"] = m_sdState.lastUpdateTime;
    currentState["delta_t"] = m_sdState.deltaT;
    currentState["mode_delay_multipliers"] = QJsonObject{
        {"ship", calculateDelayMultiplier(
                     m_sdState.utilization, TransportationMode::Ship)},
        {"truck", calculateDelayMultiplier(
                      m_sdState.utilization, TransportationMode::Truck)},
        {"train", calculateDelayMultiplier(
                      m_sdState.utilization, TransportationMode::Train)}};
    state["state"] = currentState;

    state["remaining_service_capacity"] = getRemainingServiceCapacity();
    state["container_count"] = m_storage ? m_storage->size() : 0;
    state["max_capacity"] = m_maxCapacity;
    return state;
}

Terminal::HandlingMetadata Terminal::extractHandlingMetadataLocked(
    const ContainerCore::Container &container,
    TransportationMode              arrivalMode) const
{
    HandlingMetadata metadata;
    metadata.executionId =
        customVariableString(container, "execution_id");
    metadata.pathIdentity =
        customVariableString(container, "path_identity");
    metadata.scenarioTerminalId =
        customVariableString(container, "scenario_terminal_id");
    metadata.runtimeTerminalId =
        customVariableString(container, "runtime_terminal_id");
    metadata.terminalSequenceIndex =
        customVariableInt(container, "terminal_sequence_index");
    metadata.segmentIndex =
        customVariableInt(container, "segment_index");
    metadata.vehicleId =
        customVariableString(container, "vehicle_id");

    const QString vehicleMode =
        customVariableString(container, "vehicle_mode");
    metadata.vehicleMode = vehicleMode.isEmpty()
        ? arrivalMode
        : EnumUtils::stringToTransportationMode(vehicleMode);

    if (metadata.runtimeTerminalId.isEmpty())
        metadata.runtimeTerminalId = m_terminalName;
    return metadata;
}

Terminal::ContainerHandlingOutcome Terminal::handleContainerArrivalLocked(
    const ContainerCore::Container &container,
    double                          addingTime,
    TransportationMode              arrivalMode)
{
    // Caller must hold m_mutex.
    QPair<bool, QString> capacityStatus =
        checkCapacityStatusInternal(1);
    if (!capacityStatus.first) {
        qCWarning(lcTerminal) << "Cannot add container to terminal"
                              << m_terminalName
                              << ":" << capacityStatus.second;
        throw std::runtime_error(QString("Cannot add container: %1")
                                     .arg(capacityStatus.second).toStdString());
    }

    if (capacityStatus.second.startsWith("Warning")) {
        qCWarning(lcTerminal) << "Terminal" << m_terminalName
                              << ":" << capacityStatus.second;
    }

    ContainerHandlingOutcome outcome;
    ContainerCore::Container *containerCopy = container.copy();
    outcome.containerId = containerCopy->getContainerID();
    outcome.baseAddingTime = (addingTime < 0) ? 0.0 : addingTime;
    outcome.baseDeparture = outcome.baseAddingTime;

    if (addingTime >= 0) {
        if (!m_dwellTimeMethod.isEmpty() && !m_dwellTimeParameters.isEmpty()) {
            const double rawDeparture =
                ContainerDwellTime::getDepartureTime(
                    outcome.baseAddingTime,
                    m_dwellTimeMethod,
                    m_dwellTimeParameters);
            outcome.yardDwellSeconds =
                rawDeparture - outcome.baseAddingTime;
        }

        if (m_sdParams.enabled) {
            const double yardMultiplier = calculateDelayMultiplier(
                m_sdState.utilization, arrivalMode);
            if (yardMultiplier > 1.0) {
                const double originalDwell =
                    outcome.yardDwellSeconds;
                outcome.yardDwellSeconds *= yardMultiplier;

                qCDebug(lcTerminal)
                    << "Yard dwell congestion applied to container"
                    << containerCopy->getContainerID()
                    << ": mode="
                    << EnumUtils::transportationModeToString(arrivalMode)
                    << "M_k=" << yardMultiplier
                    << "yard dwell adjusted from" << originalDwell
                    << "to" << outcome.yardDwellSeconds;
            }
        }

        outcome.baseDeparture =
            outcome.baseAddingTime + outcome.yardDwellSeconds;

        if (m_customsProbability > 0.0 && m_customsDelayMean > 0.0) {
            if (QRandomGenerator::global()->generateDouble()
                < m_customsProbability) {
                const double stdDev = (m_customsDelayVariance > 0.0)
                    ? std::sqrt(m_customsDelayVariance)
                    : 1.0;
                std::normal_distribution<double> normalDist(
                    m_customsDelayMean, stdDev);
                std::mt19937 generator(
                    QRandomGenerator::global()->generate());
                outcome.customsDelaySeconds =
                    qMax(0.0, normalDist(generator));
                outcome.baseDeparture +=
                    outcome.customsDelaySeconds;
                outcome.customsApplied = true;

                qCDebug(lcTerminal)
                    << "Container"
                    << containerCopy->getContainerID()
                    << "selected for customs inspection. Delay:"
                    << outcome.customsDelaySeconds
                    << "seconds (not congestion-scaled)";
            }
        }

        if (m_sdParams.enabled
            && m_sdState.utilization > m_sdParams.criticalUtilization) {
            outcome.arrivalPenaltySeconds =
                calculateArrivalPenalty(m_sdState.utilization, arrivalMode);
            if (outcome.arrivalPenaltySeconds > 0.0) {
                outcome.baseDeparture +=
                    outcome.arrivalPenaltySeconds;
                qCDebug(lcTerminal)
                    << "Arrival-side penalty applied to container"
                    << containerCopy->getContainerID()
                    << ": mode="
                    << EnumUtils::transportationModeToString(arrivalMode)
                    << "penalty="
                    << outcome.arrivalPenaltySeconds / 3600.0
                    << "hours";
            }
        }
    }

    if (m_sdParams.enabled) {
        m_sdState.arrivalsThisStep++;
    }

    outcome.directCostUsd = estimateContainerCostInternal(
        containerCopy, outcome.customsApplied);

    const QVariant costSoFar = containerCopy->getCustomVariable(
        NoHauler::noHauler, "cost");
    double totalCost = outcome.directCostUsd;
    if (costSoFar.isValid() && !costSoFar.toString().isEmpty()) {
        bool ok = false;
        const double previousCost = costSoFar.toDouble(&ok);
        if (ok)
            totalCost += previousCost;
    }
    containerCopy->addCustomVariable(
        NoHauler::noHauler, "cost", totalCost);

    outcome.totalHandlingSeconds =
        outcome.baseDeparture - outcome.baseAddingTime;
    const QVariant timeSoFar = containerCopy->getCustomVariable(
        NoHauler::noHauler, "time");
    double totalTime = outcome.totalHandlingSeconds;
    if (timeSoFar.isValid() && !timeSoFar.toString().isEmpty()) {
        bool ok = false;
        const double previousTime = timeSoFar.toDouble(&ok);
        if (ok)
            totalTime += previousTime;
    }
    containerCopy->addCustomVariable(
        NoHauler::noHauler, "time", totalTime);
    containerCopy->setContainerCurrentLocation(m_terminalName);
    m_storage->addContainer(containerCopy->getContainerID(),
                            containerCopy,
                            outcome.baseAddingTime,
                            outcome.baseDeparture);

    qCDebug(lcTerminal) << "Container" << containerCopy->getContainerID()
                        << "added to terminal" << m_terminalName
                        << "with arrival time:" << outcome.baseAddingTime
                        << "and estimated departure:"
                        << outcome.baseDeparture;
    return outcome;
}

void Terminal::recordHandlingBatchLocked(
    const HandlingMetadata                &metadata,
    const QList<ContainerHandlingOutcome> &outcomes,
    const QJsonObject                     &stateSnapshotBefore,
    const QJsonObject                     &stateSnapshotAfter,
    const QString                         &eventType)
{
    if (!metadata.isValid() || outcomes.isEmpty())
        return;

    TerminalHandlingBatchRecord batch;
    batch.executionId = metadata.executionId;
    batch.pathIdentity = metadata.pathIdentity;
    batch.scenarioTerminalId = metadata.scenarioTerminalId;
    batch.runtimeTerminalId = metadata.runtimeTerminalId;
    batch.terminalSequenceIndex = metadata.terminalSequenceIndex;
    batch.segmentIndex = metadata.segmentIndex;
    batch.vehicleId = metadata.vehicleId;
    batch.vehicleMode = metadata.vehicleMode;
    batch.eventType = eventType;
    batch.stateSnapshotBefore = stateSnapshotBefore;
    batch.stateSnapshotAfter = stateSnapshotAfter;

    for (const auto &outcome : outcomes) {
        batch.containerCount += 1;
        batch.eventTime = qMax(batch.eventTime, outcome.baseAddingTime);
        batch.sumYardDwellSeconds += outcome.yardDwellSeconds;
        batch.sumCustomsDelaySeconds += outcome.customsDelaySeconds;
        batch.customsAppliedCount += outcome.customsApplied ? 1 : 0;
        batch.sumArrivalPenaltySeconds += outcome.arrivalPenaltySeconds;
        batch.sumTotalHandlingSeconds += outcome.totalHandlingSeconds;
        batch.sumDirectCostUsd += outcome.directCostUsd;
        batch.containerIds.append(outcome.containerId);
    }

    m_handlingBatchRecordsByExecution[metadata.executionId].append(batch);
}

QList<TerminalExecutionResult> Terminal::terminalExecutionResultsLocked(
    const QString     &executionId,
    const QStringList &pathIdentities) const
{
    const QSet<QString> pathIdentityFilter(
        pathIdentities.cbegin(), pathIdentities.cend());

    auto appendRecords = [&](const QList<TerminalHandlingBatchRecord> &records,
                             QList<TerminalHandlingBatchRecord>       &dest) {
        for (const auto &record : records) {
            if (!pathIdentityFilter.isEmpty()
                && !pathIdentityFilter.contains(record.pathIdentity)) {
                continue;
            }
            dest.append(record);
        }
    };

    QList<TerminalHandlingBatchRecord> selected;
    if (!executionId.isEmpty()) {
        appendRecords(m_handlingBatchRecordsByExecution.value(executionId),
                      selected);
    } else {
        for (auto it = m_handlingBatchRecordsByExecution.constBegin();
             it != m_handlingBatchRecordsByExecution.constEnd(); ++it) {
            appendRecords(it.value(), selected);
        }
    }

    QHash<QString, TerminalExecutionResult> grouped;
    QList<QString> order;
    for (const auto &record : selected) {
        const QString key = record.executionId + "|" + record.pathIdentity
            + "|" + record.scenarioTerminalId + "|"
            + record.runtimeTerminalId + "|"
            + QString::number(record.terminalSequenceIndex);

        if (!grouped.contains(key)) {
            TerminalExecutionResult result;
            result.executionId = record.executionId;
            result.pathIdentity = record.pathIdentity;
            result.scenarioTerminalId = record.scenarioTerminalId;
            result.runtimeTerminalId = record.runtimeTerminalId;
            result.terminalSequenceIndex = record.terminalSequenceIndex;
            grouped.insert(key, result);
            order.append(key);
        }

        auto &result = grouped[key];
        if (record.eventType == QStringLiteral("arrival_dropoff")) {
            result.totalDroppedContainers += record.containerCount;
            result.arrivalEvents += 1;
            if (result.firstArrivalStateSnapshot.isEmpty())
                result.firstArrivalStateSnapshot = record.stateSnapshotBefore;
        } else if (record.eventType == QStringLiteral("pickup_departure")) {
            result.totalPickedContainers += record.containerCount;
            result.pickupEvents += 1;
            result.lastDepartureStateSnapshot = record.stateSnapshotAfter;
        }

        result.actualYardDwellSeconds += record.sumYardDwellSeconds;
        result.actualCustomsDelaySeconds += record.sumCustomsDelaySeconds;
        result.customsAppliedCount += record.customsAppliedCount;
        result.actualArrivalPenaltySeconds +=
            record.sumArrivalPenaltySeconds;
        result.actualTotalHandlingSeconds +=
            record.sumTotalHandlingSeconds;
        result.actualDirectCostUsd += record.sumDirectCostUsd;
        result.rawBatchRecords.append(record.toJson());
    }

    QList<TerminalExecutionResult> results;
    results.reserve(order.size());
    for (const auto &key : order)
        results.append(grouped.value(key));

    std::sort(results.begin(), results.end(),
              [](const TerminalExecutionResult &lhs,
                 const TerminalExecutionResult &rhs) {
                  if (lhs.executionId != rhs.executionId)
                      return lhs.executionId < rhs.executionId;
                  if (lhs.pathIdentity != rhs.pathIdentity)
                      return lhs.pathIdentity < rhs.pathIdentity;
                  if (lhs.terminalSequenceIndex != rhs.terminalSequenceIndex)
                      return lhs.terminalSequenceIndex < rhs.terminalSequenceIndex;
                  return lhs.runtimeTerminalId < rhs.runtimeTerminalId;
              });
    return results;
}

void Terminal::updateSystemDynamics(double currentTime, double deltaT)
{
    QMutexLocker locker(&m_mutex);

    if (!m_sdParams.enabled)
    {
        return;
    }

    // Store time step for service capacity calculations
    m_sdState.deltaT = deltaT;
    m_sdState.lastUpdateTime = currentTime;

    // Calculate utilization: U_k = I_k / Cap_k
    int containerCount = m_storage ? m_storage->size() : 0;
    if (m_maxCapacity > 0 && m_maxCapacity != std::numeric_limits<int>::max())
    {
        m_sdState.utilization =
            static_cast<double>(containerCount) / static_cast<double>(m_maxCapacity);
    }
    else
    {
        // Unlimited capacity means no congestion from utilization
        m_sdState.utilization = 0.0;
    }

    // Calculate congestion G_k(t)
    m_sdState.congestion = calculateCongestion(m_sdState.utilization);

    // Calculate service capacity S_k^cap(t)
    m_sdState.serviceCapacity = calculateServiceCapacity(m_sdState.congestion);

    // Calculate delay multiplier M_k(t)
    // Store legacy multiplier for backward compatibility (mode=Any)
    m_sdState.delayMultiplier = calculateDelayMultiplier(m_sdState.utilization,
                                                          TransportationMode::Any);

    // Reset per-step counters for the new time step
    m_sdState.arrivalsThisStep = 0;
    m_sdState.departuresThisStep = 0;

    qCDebug(lcTerminal) << "SD update for" << m_terminalName << "at t=" << currentTime
                       << ": U=" << m_sdState.utilization << "G=" << m_sdState.congestion
                       << "S_cap=" << m_sdState.serviceCapacity
                       << "M=" << m_sdState.delayMultiplier;
}

QJsonObject Terminal::getSystemDynamicsState() const
{
    QMutexLocker locker(&m_mutex);
    return runtimeTerminalSnapshotLocked();
}

QJsonObject Terminal::getRuntimeTerminalSnapshot() const
{
    QMutexLocker locker(&m_mutex);
    return runtimeTerminalSnapshotLocked();
}

QJsonObject Terminal::getRuntimeTerminalProjection(
    TransportationMode mode) const
{
    QMutexLocker locker(&m_mutex);
    return runtimeTerminalProjectionLocked(mode);
}

QJsonObject Terminal::getRuntimeTerminalProjectionsByMode() const
{
    QMutexLocker locker(&m_mutex);
    QJsonObject projections;
    projections["terminal_id"] = m_terminalName;
    projections["ship"] = runtimeTerminalProjectionLocked(
        TransportationMode::Ship);
    projections["truck"] = runtimeTerminalProjectionLocked(
        TransportationMode::Truck);
    projections["train"] = runtimeTerminalProjectionLocked(
        TransportationMode::Train);
    return projections;
}

QJsonArray Terminal::getTerminalExecutionResults(
    const QString     &executionId,
    const QStringList &pathIdentities) const
{
    QMutexLocker locker(&m_mutex);
    QJsonArray   results;
    for (const auto &result :
         terminalExecutionResultsLocked(executionId, pathIdentities)) {
        results.append(result.toJson());
    }
    return results;
}

int Terminal::clearTerminalExecutionResults(
    const QString &executionId)
{
    QMutexLocker locker(&m_mutex);
    if (executionId.isEmpty()) {
        int cleared = 0;
        for (auto it = m_handlingBatchRecordsByExecution.constBegin();
             it != m_handlingBatchRecordsByExecution.constEnd(); ++it) {
            cleared += it.value().size();
        }
        m_handlingBatchRecordsByExecution.clear();
        return cleared;
    }

    const int cleared =
        m_handlingBatchRecordsByExecution.value(executionId).size();
    m_handlingBatchRecordsByExecution.remove(executionId);
    return cleared;
}

double Terminal::getDelayMultiplier(TransportationMode mode) const
{
    QMutexLocker locker(&m_mutex);
    return calculateDelayMultiplier(m_sdState.utilization, mode);
}

int Terminal::getRemainingServiceCapacity() const
{
    if (!m_sdParams.enabled)
    {
        return std::numeric_limits<int>::max(); // Unlimited if SD disabled
    }

    // Remaining capacity = capacityThisStep - departuresThisStep
    int totalCapacityThisStep = capacityThisStep();
    int remaining = totalCapacityThisStep - m_sdState.departuresThisStep;
    return std::max(0, remaining);
}

int Terminal::capacityThisStep() const
{
    // Caller must hold m_mutex (or be on a read-only path).
    // serviceCapacity is TEU/hour; deltaT is seconds.
    return static_cast<int>(m_sdState.serviceCapacity
                            * (m_sdState.deltaT / 3600.0));
}

} // namespace TerminalSim
