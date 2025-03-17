#pragma once

#include <QObject>
#include <QHash>
#include <QMap>
#include <QSet>
#include <QString>
#include <QList>
#include <QJsonObject>
#include <QMutex>

#include "common/common.h"
#include "terminal/terminal.h"

namespace TerminalSim {

/**
 * @brief Class representing a network of terminals with routes
 */
class TerminalGraph : public QObject {
    Q_OBJECT

public:
    /**
     * @brief Structure representing a graph edge with attributes
     */
    struct Edge {
        QString from;
        QString to;
        QString routeId;
        TransportationMode mode;
        QVariantMap attributes;
    };
    
    /**
     * @brief Structure representing a path between terminals
     */
    struct PathSegment {
        QString from;
        QString to;
        TransportationMode mode;
        double weight;
        QString fromTerminalId;
        QString toTerminalId;
        QVariantMap attributes;
    };
    
    /**
     * @brief Structure representing a complete path with details
     */
    struct Path {
        int pathId;
        double totalPathCost;
        double totalEdgeCosts;
        double totalTerminalCosts;
        QList<QVariantMap> terminalsInPath;
        QList<PathSegment> segments;
    };

    /**
     * @brief Constructs a terminal graph
     * @param pathToTerminalsDirectory Directory for terminal storage
     */
    explicit TerminalGraph(const QString& pathToTerminalsDirectory = QString());
    ~TerminalGraph();
    
    // Link management
    void setLinkDefaultAttributes(const QVariantMap& attributes);
    void setCostFunctionParameters(const QVariantMap& parametersWeights);
    
    // Terminal management
    void addTerminal(
        const QStringList& terminalNames,
        const QVariantMap& customConfig,
        const QMap<TerminalInterface, QSet<TransportationMode>>& terminalInterfaces,
        const QString& region = QString()
    );
    
    void addAliasToTerminal(const QString& terminalName, const QString& alias);
    QStringList getAliasesOfTerminal(const QString& terminalName) const;
    
    // Route management
    void addRoute(
        const QString& routeId,
        const QString& startTerminal,
        const QString& endTerminal,
        TransportationMode mode,
        const QVariantMap& attributes = QVariantMap()
    );
    
    QVariantMap getEdgeByMode(const QString& startTerminal, 
                              const QString& endTerminal, 
                              TransportationMode mode) const;
    
    // Region operations
    QStringList getTerminalsByRegion(const QString& region) const;
    QList<QVariantMap> getRoutesBetweenRegions(const QString& regionA, const QString& regionB) const;
    
    // Auto-connection methods
    void connectTerminalsByInterfaceModes();
    void connectTerminalsInRegionByMode(const QString& region);
    void connectRegionsByMode(TransportationMode mode);
    
    // Route weight management
    void changeRouteWeight(
        const QString& startTerminal,
        const QString& endTerminal,
        TransportationMode mode,
        const QVariantMap& newAttributes
    );
    
    // Terminal access
    Terminal* getTerminal(const QString& terminalName) const;
    bool terminalExists(const QString& terminalName) const;
    bool removeTerminal(const QString& terminalName);
    
    // Graph queries
    int getTerminalCount() const;
    QMap<QString, QStringList> getAllTerminalNames(bool includeAliases = false) const;
    void clear();
    QVariantMap getTerminalStatus(const QString& terminalName = QString()) const;

    const QString& getPathToTerminalsDirectory() const;
    
    // Path finding
    QList<PathSegment> findShortestPath(
        const QString& startTerminal,
        const QString& endTerminal,
        TransportationMode mode = TransportationMode::Truck
    ) const;
    
    QList<PathSegment> findShortestPathWithinRegions(
        const QString& startTerminal,
        const QString& endTerminal,
        const QStringList& allowedRegions,
        TransportationMode mode = TransportationMode::Truck
    ) const;
    
    QList<Path> findTopNShortestPaths(
        const QString& startTerminal,
        const QString& endTerminal,
        int n = 5,
        TransportationMode mode = TransportationMode::Truck,
        bool skipSameModeTerminalDelaysAndCosts = true
    ) const;
    
    // Serialization
    QJsonObject serializeGraph() const;
    static TerminalGraph* deserializeGraph(
        const QJsonObject& graphData, 
        const QString& pathToTerminalsDirectory = QString()
    );
    
    void saveToFile(const QString& filepath) const;
    static TerminalGraph* loadFromFile(
        const QString& filepath,
        const QString& pathToTerminalsDirectory = QString()
    );
    
private:
    // Graph representation
    class GraphImpl;
    GraphImpl* m_graph;
    
    // Terminal management
    QString getCanonicalName(const QString& terminalName) const;
    QHash<QString, QString> m_terminalAliases;         // alias -> canonical
    QHash<QString, QSet<QString>> m_canonicalToAliases; // canonical -> set of aliases
    QHash<QString, Terminal*> m_terminals;             // canonical -> Terminal*
    
    // Path for terminal storage
    QString m_pathToTerminalsDirectory;
    
    // Cost function parameters
    QVariantMap m_costFunctionParametersWeights;
    QVariantMap m_defaultLinkAttributes;
    
    // Thread safety
    mutable QMutex m_mutex;
    
    // Cost function
    double computeCost(const QVariantMap& parameters, 
                       const QVariantMap& weights, 
                       TransportationMode mode) const;

    QList<TerminalGraph::PathSegment> findShortestPathWithExclusions(
        const QString& startTerminal,
        const QString& endTerminal,
        TransportationMode mode,
        const QSet<QPair<QString, QString>>& edgesToExclude,
        const QSet<QString>& nodesToExclude) const;
};

} // namespace TerminalSim
