#pragma once

#include <QObject>
#include <QMap>
#include <QSet>
#include <QString>
#include <QVariantMap>
#include <QJsonObject>
#include <QPair>
#include <QMutex>

#include <containerLib/container.h>
#include <containerLib/containermap.h>

#include "common/common.h"

namespace TerminalSim {

/**
 * @brief Terminal class representing a container
 *        terminal with various operations
 */
class Terminal : public QObject {
    Q_OBJECT

public:
    /**
     * @brief Constructs a terminal with configuration
     * @param terminalName Unique identifier for the terminal
     * @param interfaces Map of supported interfaces and transportation modes
     * @param modeNetworkAliases Aliases for mode-network combinations
     * @param capacity Capacity configuration
     * @param dwellTime Dwell time configuration
     * @param customs Customs operations configuration
     * @param cost Cost configuration
     * @param pathToTerminalFolder Directory for persistent storage
     */
    Terminal(
        const QString& terminalName,
        const QMap<TerminalInterface, QSet<TransportationMode>>& interfaces,
        const QMap<QPair<TransportationMode, QString>,
                   QString>& modeNetworkAliases = {},
        const QVariantMap& capacity = {},
        const QVariantMap& dwellTime = {},
        const QVariantMap& customs = {},
        const QVariantMap& cost = {},
        const QString& pathToTerminalFolder = QString()
        );
    
    ~Terminal();

    // Alias management
    QString getAliasByModeNetwork(TransportationMode mode,
                                  const QString& network) const;
    void addAliasForModeNetwork(TransportationMode mode,
                                const QString& network,
                                const QString& alias);
    
    // Capacity management
    QPair<bool, QString> checkCapacityStatus(int additionalContainers) const;
    
    // Container handling
    double estimateContainerHandlingTime() const;
    double
    estimateContainerCost(const ContainerCore::Container* container = nullptr,
                          bool applyCustoms = false) const;
    bool canAcceptTransport(TransportationMode mode,
                            TerminalInterface side) const;
    
    // Container operations
    void addContainer(const ContainerCore::Container& container,
                      double addingTime = -1);
    void addContainers(const QList<ContainerCore::Container>& containers,
                       double addingTime = -1);
    void addContainersFromJson(const QJsonObject& containers,
                               double addingTime = -1);
    
    // Container queries
    QJsonArray
    getContainersByDepatingTime(double departingTime,
                                const QString& condition = "<") const;
    QJsonArray
    getContainersByAddedTime(double addedTime,
                             const QString& condition) const;
    QJsonArray
    getContainersByNextDestination(const QString& destination) const;
    QJsonArray
    dequeueContainersByNextDestination(const QString& destination);
    
    // Terminal status
    int getContainerCount() const;
    int getAvailableCapacity() const;
    int getMaxCapacity() const;
    void clear();
    
    // Getters
    const QString& getTerminalName() const { return m_terminalName; }
    const QMap<TerminalInterface, QSet<TransportationMode>>&
    getInterfaces() const { return m_interfaces; }

    // Serialization
    QJsonObject toJson() const;
    static Terminal* fromJson(const QJsonObject& json,
                              const QString& pathToTerminalFolder = QString());
    
private:
    // Terminal properties
    QString m_terminalName;
    QMap<TerminalInterface, QSet<TransportationMode>> m_interfaces;
    QMap<QPair<TransportationMode, QString>, QString> m_modeNetworkAliases;
    
    // Capacity parameters
    int m_maxCapacity;
    double m_criticalThreshold;
    
    // Dwell time parameters
    QString m_dwellTimeMethod;
    QVariantMap m_dwellTimeParameters;
    
    // Customs parameters
    double m_customsProbability;
    double m_customsDelayMean;
    double m_customsDelayVariance;
    
    // Cost parameters
    double m_fixedCost;
    double m_customsCost;
    double m_riskFactor;
    
    // Storage
    ContainerCore::ContainerMap* m_storage;
    QString m_folderPath;
    QString m_sqlFile;
    
    // Thread safety
    mutable QMutex m_mutex;
};

} // namespace TerminalSim
