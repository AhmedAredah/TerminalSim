#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QMutex>
#include <QObject>
#include <QPair>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <containerLib/container.h>
#include <containerLib/containermap.h>

#include "common/common.h"

namespace TerminalSim
{

/**
 * @brief Per-mode delay parameters for BPR-style volume-delay function
 *
 * Each transportation mode experiences terminal congestion differently:
 * - Ships (M/D/c queueing): moderate α, quadratic β
 * - Trucks (M/Ek/c queueing): lowest α, steeper β (many servers, short service)
 * - Trains (batch/scheduled): highest α, steepest β (cascading conflicts)
 *
 * Formula: M_k(t, mode) = 1 + α · ((U_k - U_crit) / (1 - U_crit))^β   when U_k > U_crit
 *          M_k(t, mode) = 1.0                                              otherwise
 *
 * Continuous at U_k = U_crit (multiplier → 1 from both sides); max = 1+α at U_k = 1.
 */
struct ModeDelayParams
{
    double alpha = 0.5;  ///< Delay scaling coefficient
    double beta  = 2.0;  ///< Delay nonlinearity exponent
};

/**
 * @brief System Dynamics parameters for terminal congestion modeling
 *
 * These parameters control the stock-flow dynamics of terminal operations,
 * including congestion effects and throughput degradation.
 */
struct SystemDynamicsParams
{
    bool   enabled              = false; ///< Whether SD is active for this terminal
    double criticalUtilization  = 0.7;   ///< U_k^crit: threshold for congestion activation
    double congestionExponent   = 2.0;   ///< γ: nonlinearity of congestion function
    double congestionSensitivity = 1.0;  ///< β_k: service capacity degradation rate
    double delaySensitivity     = 0.5;   ///< δ_k: delay multiplier sensitivity
    double maxServiceRate       = 100.0; ///< S_max,k: max service rate (TEU/hour)

    // Mode-specific delay parameters (BPR-style volume-delay)
    ModeDelayParams shipDelay  {0.5, 2.5};  ///< Ship: M/D/c queueing behavior
    ModeDelayParams truckDelay {0.3, 2.0};  ///< Truck: M/Ek/c, lower & smoother
    ModeDelayParams trainDelay {0.8, 3.0};  ///< Train: batch/scheduled, steepest

    // Arrival-side base penalty times (seconds) at full congestion (U_k = 1.0)
    double shipArrivalPenalty  = 14400.0;  ///< Ship arrival penalty (seconds). 4 hours of berth waiting at anchorage.
    double truckArrivalPenalty =  1800.0;  ///< Truck arrival penalty (seconds). 0.5 hours of gate-in queue.
    double trainArrivalPenalty =  7200.0;  ///< Train arrival penalty (seconds). 2 hours of rail unloading.
};

/**
 * @brief System Dynamics state variables for a terminal
 *
 * These variables are updated each simulation time step and represent
 * the current congestion and throughput state of the terminal.
 */
struct SystemDynamicsState
{
    double utilization       = 0.0; ///< U_k: current utilization (I_k / Cap_k)
    double congestion        = 0.0; ///< G_k: congestion level [0, 1]
    double serviceCapacity   = 0.0; ///< S_k^cap: congestion-limited service rate (TEU/hour)
    int    serviceCapacityThisStep = 0; ///< Integer TEU budget available in the current SD step.
    double serviceCapacityCarryoverTeu = 0.0; ///< Fractional TEU budget carried across SD steps.
    double delayMultiplier   = 1.0; ///< M_k: dwell time multiplier
    int    arrivalsThisStep  = 0;   ///< N_k^arr: arrivals in current time step
    int    departuresThisStep = 0;  ///< N_k^srv: departures in current time step
    double lastUpdateTime    = 0.0;    ///< Last SD update timestamp (seconds). Same scale as currentTime parameter.
    double deltaT            = 3600.0; ///< Current time step duration (seconds). Default 3600.0 = 1 hour.
};

enum class TerminalArrivalSemantics
{
    Infer,
    RuntimeArrival,
    Preload
};

struct TerminalHandlingBatchRecord
{
    QString            executionId;
    QString            canonicalPathKey;
    QString            scenarioTerminalId;
    QString            runtimeTerminalId;
    int                terminalSequenceIndex = -1;
    int                segmentIndex = -1;
    QString            vehicleId;
    TransportationMode vehicleMode = TransportationMode::Any;
    QString            eventType;
    int                containerCount = 0;
    double             eventTime = 0.0;
    double             sumYardDwellSeconds = 0.0;
    double             sumCustomsDelaySeconds = 0.0;
    int                customsAppliedCount = 0;
    double             sumArrivalPenaltySeconds = 0.0;
    double             sumTotalHandlingSeconds = 0.0;
    double             sumDirectCostUsd = 0.0;
    QJsonObject        stateSnapshotBefore;
    QJsonObject        stateSnapshotAfter;
    QStringList        containerIds;

    QJsonObject toJson() const;
};

struct TerminalExecutionResult
{
    QString     executionId;
    QString     canonicalPathKey;
    QString     scenarioTerminalId;
    QString     runtimeTerminalId;
    int         terminalSequenceIndex = -1;
    int         totalDroppedContainers = 0;
    int         totalPickedContainers = 0;
    int         arrivalEvents = 0;
    int         pickupEvents = 0;
    double      actualYardDwellSeconds = 0.0;
    double      actualCustomsDelaySeconds = 0.0;
    int         customsAppliedCount = 0;
    double      actualArrivalPenaltySeconds = 0.0;
    double      actualTotalHandlingSeconds = 0.0;
    double      actualDirectCostUsd = 0.0;
    QJsonObject firstArrivalStateSnapshot;
    QJsonObject lastDepartureStateSnapshot;
    QJsonArray  rawBatchRecords;

    QJsonObject toJson() const;
};

struct TerminalContainerReservation
{
    QString     reservationId;
    QStringList containerIds;
    QJsonArray  containersSnapshot;
};

/**
 * @brief Terminal class representing a container
 *        terminal with various operations
 */
class Terminal : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Constructs a terminal with configuration
     * @param terminalName Unique identifier for the terminal
     * @param displayName Human-readable name for the terminal
     * @param interfaces Map of supported interfaces and transportation modes
     * @param modeNetworkAliases Aliases for mode-network combinations
     * @param capacity Capacity configuration
     * @param dwellTime Dwell time configuration
     * @param customs Customs operations configuration
     * @param cost Cost configuration
     * @param systemDynamics System dynamics configuration (optional)
     * @param pathToTerminalFolder Directory for persistent storage
     */
    Terminal(
        const QString &terminalName, const QString &displayName,
        const QMap<TerminalInterface, QSet<TransportationMode>> &interfaces,
        const QMap<QPair<TransportationMode, QString>, QString>
                          &modeNetworkAliases = {},
        const QVariantMap &capacity = {}, const QVariantMap &dwellTime = {},
        const QVariantMap &customs = {}, const QVariantMap &cost = {},
        const QVariantMap &systemDynamics = {},
        const QString &pathToTerminalFolder = QString());

    ~Terminal();

    // Alias management
    QString getAliasByModeNetwork(TransportationMode mode,
                                  const QString     &network) const;
    void addAliasForModeNetwork(TransportationMode mode, const QString &network,
                                const QString &alias);

    // Capacity management
    QPair<bool, QString> checkCapacityStatus(int additionalContainers) const;

    // Container handling
    /**
     * @brief Expected per-container handling time at this terminal.
     * @return Expected handling time (seconds). Sums the dwell-time distribution
     *         mean and the expected customs delay (probability × delay_mean).
     *         Callers that weight this by a cost coefficient must scale their
     *         weights to account for seconds rather than hours.
     */
    double estimateContainerHandlingTime() const;
    double
    estimateContainerCost(const ContainerCore::Container *container = nullptr,
                          bool applyCustoms = false) const;
    double
         estimateTotalCostByWeights(double delayConst, double costWeight,
                                    const ContainerCore::Container *container) const;
    bool canAcceptTransport(TransportationMode mode,
                            TerminalInterface  side) const;

    // Container operations
    void addContainer(const ContainerCore::Container &container,
                      double                          addingTime = -1,
                      TransportationMode              arrivalMode = TransportationMode::Any,
                      TerminalArrivalSemantics        arrivalSemantics =
                          TerminalArrivalSemantics::Infer);
    void addContainers(const QList<ContainerCore::Container> &containers,
                       double                                 addingTime = -1,
                       TransportationMode arrivalMode = TransportationMode::Any,
                       TerminalArrivalSemantics arrivalSemantics =
                           TerminalArrivalSemantics::Infer);
    void addContainersFromJson(const QJsonObject &containers,
                               double             addingTime = -1,
                               TransportationMode arrivalMode = TransportationMode::Any,
                               TerminalArrivalSemantics arrivalSemantics =
                                   TerminalArrivalSemantics::Infer);

    // Container queries
    QJsonArray
               getContainersByDepatingTime(double         departingTime,
                                           const QString &condition = "<") const;
    QJsonArray getContainersByAddedTime(double         addedTime,
                                        const QString &condition) const;
    QJsonArray getContainersByNextDestination(const QString &destination) const;
    QJsonArray
    getContainers(const ContainerCore::ContainerSelectionCriteria &criteria) const;
    QJsonArray dequeueContainersByNextDestination(const QString &destination,
                                                  double operationTime = -1.0);
    QJsonArray
    dequeueContainers(const ContainerCore::ContainerSelectionCriteria &criteria,
                      double operationTime = -1.0);
    QJsonObject reserveContainers(
        const QString                                      &reservationId,
        const ContainerCore::ContainerSelectionCriteria    &criteria);
    QJsonObject commitContainerReservation(
        const QString &reservationId,
        double operationTime = -1.0);
    QJsonObject releaseContainerReservation(
        const QString &reservationId);

    // Terminal status
    int  getContainerCount() const;
    int  getAvailableCapacity() const;
    int  getMaxCapacity() const;
    void clear();
    void resetRuntimeState();

    // Getters
    const QString &getTerminalName() const
    {
        return m_terminalName;
    }
    const QMap<TerminalInterface, QSet<TransportationMode>> &
    getInterfaces() const
    {
        return m_interfaces;
    }

    // Serialization
    QJsonObject      toJson() const;
    static Terminal *fromJson(const QJsonObject &json,
                              const QString &pathToTerminalFolder = QString());

    // Utility for parsing mode-network aliases from config
    static QMap<QPair<TransportationMode, QString>, QString>
    parseModeNetworkAliases(const QVariantMap &aliasesMap);

    // System Dynamics methods
    /**
     * @brief Update system dynamics state for the current time step
     * @param currentTime Current simulation time (seconds)
     * @param deltaT Time step duration (seconds)
     *
     * Recalculates utilization, congestion, service capacity, and delay
     * multiplier based on current inventory. Should be called once per
     * simulation time step.
     */
    void updateSystemDynamics(double currentTime, double deltaT);

    /**
     * @brief Get current system dynamics state as JSON
     * @return JSON object containing all SD state variables and parameters
     */
    QJsonObject getSystemDynamicsState() const;
    QJsonObject getRuntimeTerminalSnapshot() const;
    QJsonObject getRuntimeTerminalProjection(TransportationMode mode) const;
    QJsonObject getRuntimeTerminalProjectionsByMode() const;
    QJsonArray  getTerminalExecutionResults(
         const QString     &executionId = QString(),
         const QStringList &canonicalPathKeys = {}) const;
    int clearTerminalExecutionResults(
        const QString &executionId = QString());

    /**
     * @brief Check if system dynamics is enabled for this terminal
     * @return true if SD is enabled
     */
    bool isSystemDynamicsEnabled() const { return m_sdParams.enabled; }

    /**
     * @brief Get current delay multiplier M_k(t)
     * @return Delay multiplier (>= 1.0)
     */
    double getDelayMultiplier() const { return m_sdState.delayMultiplier; }

    /**
     * @brief Get mode-specific delay multiplier M_k(t, mode)
     * @param mode Transportation mode (Ship, Truck, Train)
     * @return Mode-specific delay multiplier (>= 1.0)
     */
    double getDelayMultiplier(TransportationMode mode) const;

    /**
     * @brief Get current congestion level G_k(t)
     * @return Congestion level [0, 1]
     */
    double getCongestionLevel() const { return m_sdState.congestion; }

    /**
     * @brief Get current service capacity S_k^cap(t)
     * @return Service capacity in TEU/hour
     */
    double getServiceCapacity() const { return m_sdState.serviceCapacity; }

    /**
     * @brief Get remaining service capacity for current time step
     * @return Remaining capacity in TEU after committed departures and active
     *         reservations are accounted for.
     */
    int getRemainingServiceCapacity() const;

private:
    struct ContainerHandlingOutcome
    {
        QString containerId;
        double  baseAddingTime = 0.0;
        double  baseDeparture = 0.0;
        bool    customsApplied = false;
        double  yardDwellSeconds = 0.0;
        double  customsDelaySeconds = 0.0;
        double  arrivalPenaltySeconds = 0.0;
        double  totalHandlingSeconds = 0.0;
        double  directCostUsd = 0.0;
    };

    struct HandlingMetadata
    {
        QString            executionId;
        QString            canonicalPathKey;
        QString            scenarioTerminalId;
        QString            runtimeTerminalId;
        int                terminalSequenceIndex = -1;
        int                segmentIndex = -1;
        QString            vehicleId;
        TransportationMode vehicleMode = TransportationMode::Any;

        bool    isValid() const;
        QString groupingKey() const;
    };

    struct RecordedContainerArrival
    {
        HandlingMetadata         metadata;
        ContainerHandlingOutcome outcome;
    };

    // Terminal properties
    QString                                           m_terminalName;
    QString                                           m_displayName;
    QMap<TerminalInterface, QSet<TransportationMode>> m_interfaces;
    QMap<QPair<TransportationMode, QString>, QString> m_modeNetworkAliases;

    // Capacity parameters
    int    m_maxCapacity;
    double m_criticalThreshold;

    // Dwell time parameters
    QString     m_dwellTimeMethod;
    QVariantMap m_dwellTimeParameters;

    // Customs parameters
    double m_customsProbability;
    double m_customsDelayMean;      ///< Mean customs inspection delay (seconds).
    double m_customsDelayVariance;  ///< Variance of customs inspection delay (seconds²).

    // Cost parameters
    double m_fixedCost;
    double m_customsCost;
    double m_riskFactor;

    // Storage
    ContainerCore::ContainerMap *m_storage;
    QString                      m_folderPath;
    QString                      m_sqlFile;

    // System Dynamics
    SystemDynamicsParams m_sdParams;
    SystemDynamicsState  m_sdState;
    QHash<QString, QList<TerminalHandlingBatchRecord>>
        m_handlingBatchRecordsByExecution;
    QHash<QString, TerminalContainerReservation>
        m_containerReservations;
    QHash<QString, QString> m_reservedContainerIds;
    QHash<QString, QJsonObject> m_completedContainerReservations;
    QSet<QString> m_releasedContainerReservations;

    // Thread safety
    mutable QMutex m_mutex;

    // Lock-free helpers (caller must hold m_mutex)
    QPair<bool, QString> checkCapacityStatusInternal(int additionalContainers) const;
    double estimateContainerCostInternal(
        const ContainerCore::Container *container = nullptr,
        bool applyCustoms = false) const;

    /**
     * @brief Per-step service capacity in TEU.
     * @return `serviceCapacity × deltaT` converted to TEU. `serviceCapacity` is
     *         TEU/hour; `deltaT` is seconds (see 2026-04-17 unification).
     *         The /3600.0 bridge lives here so callers need not repeat it.
     */
    int capacityThisStep() const;
    int remainingServiceCapacityLocked() const;
    void resetRuntimeStateLocked(bool clearExecutionRecords);
    void refreshServiceCapacityBudgetLocked();

    // Private SD helper methods
    double calculateCongestion(double utilization) const;
    double calculateServiceCapacity(double congestion) const;
    double calculateDelayMultiplier(double utilization,
                                    TransportationMode mode = TransportationMode::Any) const;
    double calculateArrivalPenalty(double utilization,
                                   TransportationMode mode) const;
    QJsonObject runtimeTerminalSnapshotLocked() const;
    QJsonObject runtimeTerminalProjectionLocked(
        TransportationMode mode) const;
    HandlingMetadata extractHandlingMetadataLocked(
        const ContainerCore::Container &container,
        TransportationMode              arrivalMode) const;
    ContainerHandlingOutcome handleContainerArrivalLocked(
        const ContainerCore::Container &container,
        double                          addingTime,
        TransportationMode              arrivalMode,
        TerminalArrivalSemantics        arrivalSemantics);
    void recordHandlingBatchLocked(
        const HandlingMetadata                &metadata,
        const QList<ContainerHandlingOutcome> &outcomes,
        const QJsonObject                     &stateSnapshotBefore,
        const QJsonObject                     &stateSnapshotAfter,
        const QString                         &eventType);
    QStringList selectContainerIdsForPickupLocked(
        const ContainerCore::ContainerSelectionCriteria &criteria,
        bool excludeReserved) const;
    QJsonArray containersSnapshotLocked(
        const QStringList &containerIds) const;
    QJsonArray removeContainersAndRecordPickupLocked(
        const QStringList &containerIds,
        const QString     &eventType,
        double             operationTime);
    QList<TerminalExecutionResult> terminalExecutionResultsLocked(
        const QString     &executionId,
        const QStringList &canonicalPathKeys) const;
};

} // namespace TerminalSim
