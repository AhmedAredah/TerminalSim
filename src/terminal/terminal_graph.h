#pragma once

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QMutex>
#include <QObject>
#include <QSet>
#include <QString>

#include "common.h"
#include "terminal/terminal.h"
#include "terminal_path.h"
#include "terminal_path_segment.h"

// Include the new Graph library
#include <Algorithms.h>
#include <Graph.h>

namespace TerminalSim
{

/**
 * @struct EdgeIdentifier
 * @brief Uniquely identifies an edge in the terminal graph
 */
struct EdgeIdentifier
{
    QString            from;
    QString            to;
    TransportationMode mode;

    EdgeIdentifier(const QString &from, const QString &to,
                   TransportationMode mode)
        : from(from)
        , to(to)
        , mode(mode)
    {
    }

    bool operator==(const EdgeIdentifier &other) const
    {
        return from == other.from && to == other.to && mode == other.mode;
    }

    bool operator<(const EdgeIdentifier &other) const
    {
        if (from != other.from)
            return from < other.from;
        if (to != other.to)
            return to < other.to;
        return static_cast<int>(mode) < static_cast<int>(other.mode);
    }
};

inline uint qHash(const EdgeIdentifier &edge, uint seed = 0)
{
    seed ^= qHash(edge.from, seed);
    seed ^= qHash(edge.to, seed);
    seed ^= static_cast<uint>(edge.mode);
    return seed;
}

/**
 * @class TerminalGraph
 * @brief Manages a network of terminals and routes using k-shortest paths
 * algorithm
 */
class TerminalGraph : public QObject
{
    Q_OBJECT

public:
    explicit TerminalGraph(const QString &dir = QString());
    ~TerminalGraph() override;

    // Configuration
    void setCostFunctionParameters(const QVariantMap &params);
    void setLinkDefaultAttributes(const QVariantMap &attrs);

    // Terminal management
    Terminal *addTerminal(const QVariantMap &terminalData);

    QMap<QString, Terminal *>
    addTerminals(const QList<QVariantMap> &terminalsList);

    void        addAliasToTerminal(const QString &name, const QString &alias);
    QStringList getAliasesOfTerminal(const QString &name) const;

    // Route management
    QPair<QString, QString> addRoute(const QString &id, const QString &start,
                                     const QString     &end,
                                     TransportationMode mode,
                                     const QVariantMap &attrs = QVariantMap());

    QList<QPair<QString, QString>>
    addRoutes(const QList<QVariantMap> &routesList);

    // Terminal access
    Terminal *getTerminal(const QString &name) const;
    bool      terminalExists(const QString &name) const;
    bool      removeTerminal(const QString &name);

    // Graph queries
    int getTerminalCount() const;
    QMap<QString, QStringList>
                getAllTerminalNames(bool includeAliases = false) const;
    QVariantMap getTerminalStatus(const QString &name = QString()) const;
    void        clear();

    // Path finding
    QList<PathSegment>
    findShortestPath(const QString &start, const QString &end,
                     TransportationMode mode = TransportationMode::Any);

    QList<Path>
    findTopNShortestPaths(const QString &start, const QString &end, int n = 5,
                          TransportationMode mode = TransportationMode::Any,
                          bool               skipDelays = true);

private:
    // New Graph library representation - using QString for vertex IDs and
    // double for weights
    using GraphType           = GraphLib::Graph<QString, double>;
    using GraphAlgorithmsType = GraphLib::GraphAlgorithms<QString, double>;
    using EdgeType            = GraphLib::Edge<QString, double>;
    using EdgePathType        = typename GraphAlgorithmsType::EdgePath;
    using EdgePathInfoType    = typename GraphAlgorithmsType::EdgePathInfo;

    // The graph object
    GraphType m_graph;

    // Edge data
    struct EdgeData
    {
        QString            routeId;
        TransportationMode mode;
        QVariantMap        attributes;
    };
    struct TerminalDetails
    {
        double handlingTime;
        double handlingCost;
    };

    QHash<EdgeIdentifier, QList<EdgeData>> m_edgeData;

    QHash<QString, QString>       m_terminalAliases;
    QHash<QString, QSet<QString>> m_canonicalToAliases;
    QHash<QString, Terminal *>    m_terminals;
    QHash<QString, QVariantMap>   m_nodeAttributes;
    QHash<QString, TerminalDetails> m_terminalData;

    QString        m_pathToTerminalsDirectory;
    QVariantMap    m_costFunctionParametersWeights;
    QVariantMap    m_defaultLinkAttributes;
    mutable QMutex m_mutex;

    // Helper methods
    QString getCanonicalName(const QString &name) const;

    double computeCost(const QVariantMap &params, const QVariantMap &weights,
                       TransportationMode mode) const;

    // Convert between GraphLib edge path and TerminalSim path
    Path convertEdgePathToTerminalPath(const EdgePathInfoType &pathInfo,
                                       int pathId, TransportationMode mode,
                                       bool skipDelays) const;

    // Update graph for specific mode
    void updateGraph(TransportationMode mode);

    // Build a path segment with detailed costs
    void buildPathSegment(PathSegment &segment, bool isStart, bool isEnd,
                          bool skipStartTerminal, bool skipEndTerminal,
                          const QString &from, const QString &to,
                          TransportationMode mode,
                          const QVariantMap &attributes) const;

    QPair<QString, QString> addRouteInternal(const QString     &id,
                                             const QString     &start,
                                             const QString     &end,
                                             TransportationMode mode,
                                             const QVariantMap &attrs);

    Terminal *addTerminalInternal(const QVariantMap &terminalData);
};

} // namespace TerminalSim
