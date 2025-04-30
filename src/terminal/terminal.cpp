#include "terminal.h"
#include "dwell_time/container_dwell_time.h"

#include <QDir>
#include <QDebug>
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

namespace TerminalSim {

Terminal::Terminal(
    const QString &terminalName, const QString &displayName,
    const QMap<TerminalInterface, QSet<TransportationMode>> &interfaces,
    const QMap<QPair<TransportationMode, QString>, QString> &modeNetworkAliases,
    const QVariantMap &capacity, const QVariantMap &dwellTime,
    const QVariantMap &customs, const QVariantMap &cost,
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
    
    // Process customs parameters
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
    
    qDebug() << "Terminal" << m_terminalName
             << "initialized with" << m_interfaces.size()
             << "interfaces and max capacity:"
             << (m_maxCapacity == std::numeric_limits<int>::max() ?
                     "unlimited" : QString::number(m_maxCapacity));
}

Terminal::~Terminal()
{
    qDebug() << "Destroying terminal" << m_terminalName;
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
    qDebug() << "Added alias" << alias
             << "for terminal" << m_terminalName
             << "with mode" << static_cast<int>(mode)
             << "and network" << network;
}

QPair<bool, QString>
Terminal::checkCapacityStatus(int additionalContainers) const
{
    QMutexLocker locker(&m_mutex);
    
    int currentCount = getContainerCount();
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

double Terminal::estimateContainerHandlingTime() const
{
    QMutexLocker locker(&m_mutex);
    
    double totalHours = 0.0;
    
    // 1. Dwell Time (Storage Duration)
    if (!m_dwellTimeParameters.isEmpty()) {
        double dwellMeanHours = ContainerDwellTime::getDepartureTime(
                                    0.0, m_dwellTimeMethod.isEmpty() ?
                                        "gamma" : m_dwellTimeMethod,
                                    m_dwellTimeParameters) /
                                3600.0; // Convert seconds to hours
        totalHours += dwellMeanHours;
    }
    
    // 2. Customs Inspection (Expected)
    if (m_customsProbability > 0.0 && m_customsDelayMean > 0.0) {
        totalHours += m_customsProbability * m_customsDelayMean;
    }
    
    return totalHours;
}

double
Terminal::estimateContainerCost(const ContainerCore::Container *container,
                                bool applyCustoms) const
{
    QMutexLocker locker(&m_mutex);
    
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
                            double addingTime)
{
    QMutexLocker locker(&m_mutex);
    
    // Check capacity
    QPair<bool, QString> capacityStatus = checkCapacityStatus(1);
    if (!capacityStatus.first) {
        qWarning() << "Cannot add container to terminal"
                   << m_terminalName
                   << ":" << capacityStatus.second;
        throw std::runtime_error(QString("Cannot add container: %1")
                                     .arg(capacityStatus.second).toStdString());
    }
    
    if (capacityStatus.second.startsWith("Warning")) {
        qWarning() << "Terminal" << m_terminalName
                   << ":" << capacityStatus.second;
    }
    
    // Create a copy of the container to modify
    ContainerCore::Container* containerCopy = container.copy();
    
    // Handle the case when addingTime is not specified
    double baseAddingTime = (addingTime < 0) ? 0.0 : addingTime;
    double baseDeparture = baseAddingTime;
    bool customsApplied = false;
    
    if (addingTime >= 0) {
        // 1. Predict dwell time based on method and parameters
        if (!m_dwellTimeMethod.isEmpty() && !m_dwellTimeParameters.isEmpty()) {
            baseDeparture = ContainerDwellTime::getDepartureTime(
                baseAddingTime,
                m_dwellTimeMethod,
                m_dwellTimeParameters
                );
        }
        
        // 2. Apply customs delay if applicable based on probability
        if (m_customsProbability > 0.0 && m_customsDelayMean > 0.0) {
            if (QRandomGenerator::global()->generateDouble() <
                m_customsProbability) {
                double stdDev = (m_customsDelayVariance > 0.0) ?
                                    std::sqrt(m_customsDelayVariance) : 1.0;
                double customsDelay = 0.0;
                {
                    // Create a normal distribution with the given
                    // mean and standard deviation
                    std::normal_distribution<double>
                        normalDist(m_customsDelayMean, stdDev);

                    // Use Qt's random generator to get a seed value
                    std::mt19937
                        generator(QRandomGenerator::global()->generate());

                    // Generate a random value from the normal distribution
                    customsDelay = qMax(0.0, normalDist(generator));
                }

                baseDeparture +=
                    customsDelay * 3600.0; // Convert hours to seconds
                customsApplied = true;
                
                qDebug() << "Container"
                         << containerCopy->getContainerID()
                         << "selected for customs inspection. Delay:"
                         << customsDelay << "hours";
            }
        }
    }
    
    // 3. Calculate total cost for the container
    double containerCost =
        estimateContainerCost(containerCopy, customsApplied);
    QVariant costSoFar =
        containerCopy->getCustomVariable(
            ContainerCore::Container::HaulerType::noHauler,
            "cost");
    
    double totalCost = containerCost;
    if (costSoFar.isValid() && !costSoFar.toString().isEmpty()) {
        bool ok;
        double previousCost = costSoFar.toDouble(&ok);
        if (ok) {
            totalCost += previousCost;
        }
    }
    
    containerCopy->addCustomVariable(
        ContainerCore::Container::HaulerType::noHauler,
        "cost",
        totalCost);
    
    // 4. Accumulate total time for the container
    QVariant timeSoFar =
        containerCopy->getCustomVariable(
            ContainerCore::Container::HaulerType::noHauler,
            "time");
    
    double totalTime = baseDeparture - baseAddingTime;
    if (timeSoFar.isValid() && !timeSoFar.toString().isEmpty()) {
        bool ok;
        double previousTime = timeSoFar.toDouble(&ok);
        if (ok) {
            totalTime += previousTime;
        }
    }
    
    containerCopy->addCustomVariable(
        ContainerCore::Container::HaulerType::noHauler,
        "time",
        totalTime);
    
    // 5. Set container location
    containerCopy->setContainerCurrentLocation(m_terminalName);
    
    // 6. Add to storage
    m_storage->addContainer(containerCopy->getContainerID(),
                            containerCopy, baseAddingTime, baseDeparture);
    
    qDebug() << "Container" << containerCopy->getContainerID()
             << "added to terminal" << m_terminalName
             << "with arrival time:" << baseAddingTime
             << "and estimated departure:" << baseDeparture;
}

void
Terminal::addContainers(const QVector<ContainerCore::Container>& containers,
                        double addingTime)
{
    QMutexLocker locker(&m_mutex);
    
    // Check capacity before adding containers
    int containerCount = containers.size();
    
    QPair<bool, QString> capacityStatus = checkCapacityStatus(containerCount);
    if (!capacityStatus.first) {
        qWarning() << "Cannot add" << containerCount
                   << "containers to terminal" << m_terminalName
                   << ":" << capacityStatus.second;
        throw std::runtime_error(
            QString("Cannot add %1 containers: %2")
                .arg(containerCount).arg(capacityStatus.second).toStdString()
            );
    }
    
    if (capacityStatus.second.startsWith("Warning")) {
        qWarning() << "Terminal" << m_terminalName
                   << ":" << capacityStatus.second;
    }
    
    // Add each container individually
    // Must release mutex to prevent deadlock during
    // nested lock acquisition in addContainer
    locker.unlock();
    
    for (const ContainerCore::Container& container : containers) {
        addContainer(container, addingTime);
    }
}

void Terminal::addContainersFromJson(const QJsonObject& containers,
                                     double addingTime)
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
        qWarning() << "Error parsing containers from JSON:"
                   << e.what();
        throw std::runtime_error(QString("Invalid container JSON: %1")
                                     .arg(e.what()).toStdString());
    }
    
    int containerCount = containerList.size();
    if (containerCount == 0) {
        qWarning() << "No valid containers found in JSON";
        return;
    }
    
    qDebug() << "Adding" << containerCount
             << "containers from JSON to terminal" << m_terminalName;
    
    // Add the containers
    addContainers(containerList, addingTime);
}

QJsonArray
Terminal::getContainersByDepatingTime(double departingTime,
                                      const QString& condition) const
{
    QMutexLocker locker(&m_mutex);
    
    // Validate condition
    QStringList validConditions = {"<", "<=", ">", ">=", "==", "!="};
    if (!validConditions.contains(condition)) {
        qWarning() << "Invalid condition for getContainersByDepatingTime:"
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
    
    qDebug() << "Found" << result.size()
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
        qWarning() << "Invalid condition for getContainersByAddedTime:"
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
    
    qDebug() << "Found" << result.size()
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
    
    qDebug() << "Found" << result.size()
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
    
    // Convert to JSON array
    QJsonArray result;
    for (const ContainerCore::Container* container : containers) {
        result.append(container->toJson());
    }
    
    qDebug() << "Removed" << result.size()
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
    
    qDebug() << "Clearing all containers from terminal" << m_terminalName;
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

    return json;
}

Terminal* Terminal::fromJson(const QJsonObject& json,
                             const QString& pathToTerminalFolder)
{
    // Extract terminal name
    if (!json.contains("terminal_name") ||
        !json["terminal_name"].isString()) {
        qWarning() << "Missing or invalid terminal_name in JSON";
        return nullptr;
    }
    QString terminalName = json["terminal_name"].toString();
    QString displayName  = json["display_name"].toString();

    // Extract interfaces
    QMap<TerminalInterface,
         QSet<TransportationMode>> interfaces;
    if (json.contains("interfaces") &&
        json["interfaces"].isObject()) {
        QJsonObject interfacesJson = json["interfaces"].toObject();
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
    
    // Extract mode network aliases
    QMap<QPair<TransportationMode, QString>, QString> modeNetworkAliases;
    if (json.contains("mode_network_aliases") &&
        json["mode_network_aliases"].isObject()) {
        QJsonObject aliasesJson = json["mode_network_aliases"].toObject();
        for (auto it = aliasesJson.constBegin();
             it != aliasesJson.constEnd(); ++it) {
            QString key = it.key();
            QStringList parts = key.split(':');
            if (parts.size() != 2) continue;
            
            bool ok;
            int modeInt = parts[0].toInt(&ok);
            if (!ok) continue;
            
            TransportationMode mode = static_cast<TransportationMode>(modeInt);
            QString network = parts[1];
            QString alias = it.value().toString();
            
            modeNetworkAliases[qMakePair(mode, network)] = alias;
        }
    }
    
    // Extract capacity
    QVariantMap capacity;
    if (json.contains("capacity") && json["capacity"].isObject()) {
        QJsonObject capacityJson = json["capacity"].toObject();
        
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
    if (json.contains("dwell_time") && json["dwell_time"].isObject()) {
        QJsonObject dwellTimeJson = json["dwell_time"].toObject();
        
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
    if (json.contains("customs") && json["customs"].isObject()) {
        QJsonObject customsJson = json["customs"].toObject();
        
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
    if (json.contains("cost") && json["cost"].isObject()) {
        QJsonObject costJson = json["cost"].toObject();
        
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
    
    // Create terminal
    Terminal *terminal =
        new Terminal(terminalName, displayName, interfaces, modeNetworkAliases,
                     capacity, dwellTime, customs, cost, pathToTerminalFolder);

    return terminal;
}

} // namespace TerminalSim
