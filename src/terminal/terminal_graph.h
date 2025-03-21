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
#include "terminal_path_segment.h"
#include "terminal_path.h"
#include "terminal_graph_impl.h"

namespace TerminalSim {

/**
 * @class TerminalGraph
 * @brief Manages a network of terminals and routes
 *
 * This class represents a graph of terminals connected
 * by routes, supporting path finding and serialization.
 */
class TerminalGraph : public QObject {
    Q_OBJECT

public:
    /**
     * @brief Constructs a terminal graph
     * @param dir Directory path for terminal storage
     */
    explicit TerminalGraph(const QString& dir = QString());

    /**
     * @brief Destructor, cleans up resources
     */
    ~TerminalGraph() override;

    // Link management
    /**
     * @brief Sets default attributes for links
     * @param attrs Attributes to set as default
     */
    void setLinkDefaultAttributes(const QVariantMap& attrs);

    /**
     * @brief Sets cost function parameters
     * @param params Weights for cost computation
     */
    void setCostFunctionParameters(const QVariantMap& params);

    // Terminal management
    /**
     * @brief Adds a terminal to the graph
     * @param names List of terminal names (first is canonical)
     * @param config Custom configuration for the terminal
     * @param interfaces Terminal interfaces and modes
     * @param region Region the terminal belongs to
     */
    void addTerminal(const QStringList& names,
                     const QVariantMap& config,
                     const QMap<TerminalInterface,
                                QSet<TransportationMode>>& interfaces,
                     const QString& region = QString());

    /**
     * @brief Adds an alias to an existing terminal
     * @param name Terminal name to alias
     * @param alias New alias to add
     */
    void addAliasToTerminal(const QString& name,
                            const QString& alias);

    /**
     * @brief Gets aliases for a terminal
     * @param name Terminal name to query
     * @return List of aliases
     */
    QStringList getAliasesOfTerminal(const QString& name) const;

    // Route management
    /**
     * @brief Adds a route between terminals
     * @param id Unique route identifier
     * @param start Starting terminal name
     * @param end Ending terminal name
     * @param mode Transportation mode
     * @param attrs Route attributes
     */
    void addRoute(const QString& id,
                  const QString& start,
                  const QString& end,
                  TransportationMode mode,
                  const QVariantMap& attrs = QVariantMap());

    /**
     * @brief Gets edge attributes by mode
     * @param start Starting terminal name
     * @param end Ending terminal name
     * @param mode Transportation mode
     * @return Edge attributes, or empty if not found
     */
    QVariantMap getEdgeByMode(const QString& start,
                              const QString& end,
                              TransportationMode mode) const;

    // Region operations
    /**
     * @brief Gets terminals in a region
     * @param region Region name
     * @return List of terminal names
     */
    QStringList getTerminalsByRegion(const QString& region) const;

    /**
     * @brief Gets routes between two regions
     * @param regionA First region name
     * @param regionB Second region name
     * @return List of route details
     */
    QList<QVariantMap> getRoutesBetweenRegions(const QString& regionA,
                                               const QString& regionB) const;

    // Auto-connection methods
    /**
     * @brief Connects terminals by interface modes
     */
    void connectTerminalsByInterfaceModes();

    /**
     * @brief Connects terminals in a region by mode
     * @param region Region name
     */
    void connectTerminalsInRegionByMode(const QString& region);

    /**
     * @brief Connects regions by a specific mode
     * @param mode Transportation mode
     */
    void connectRegionsByMode(TransportationMode mode);

    /**
     * @brief Changes weight of a route
     * @param start Starting terminal name
     * @param end Ending terminal name
     * @param mode Transportation mode
     * @param attrs New attributes to apply
     */
    void changeRouteWeight(const QString& start,
                           const QString& end,
                           TransportationMode mode,
                           const QVariantMap& attrs);

    // Terminal access
    /**
     * @brief Gets a terminal by name
     * @param name Terminal name
     * @return Pointer to terminal, or throws if not found
     */
    Terminal* getTerminal(const QString& name) const;

    /**
     * @brief Checks if a terminal exists
     * @param name Terminal name
     * @return True if exists, false otherwise
     */
    bool terminalExists(const QString& name) const;

    /**
     * @brief Removes a terminal from the graph
     * @param name Terminal name
     * @return True if removed, false if not found
     */
    bool removeTerminal(const QString& name);

    // Graph queries
    /**
     * @brief Gets the number of terminals
     * @return Terminal count
     */
    int getTerminalCount() const;

    /**
     * @brief Gets all terminal names
     * @param includeAliases Include aliases if true
     * @return Map of canonical names to alias lists
     */
    QMap<QString, QStringList>
    getAllTerminalNames(bool includeAliases = false) const;

    /**
     * @brief Clears the graph
     */
    void clear();

    /**
     * @brief Gets status of a terminal or all terminals
     * @param name Terminal name, empty for all
     * @return Status details
     */
    QVariantMap getTerminalStatus(const QString& name = QString()) const;

    /**
     * @brief Gets the terminal directory path
     * @return Directory path
     */
    const QString& getPathToTerminalsDirectory() const;

    // Path finding
    /**
     * @brief Finds the shortest path between terminals
     * @param start Starting terminal name
     * @param end Ending terminal name
     * @param mode Transportation mode (default: Truck)
     * @return List of path segments
     */
    QList<PathSegment> findShortestPath(
        const QString& start,
        const QString& end,
        TransportationMode mode = TransportationMode::Truck) const;

    /**
     * @brief Finds shortest path within regions
     * @param start Starting terminal name
     * @param end Ending terminal name
     * @param regions Allowed regions
     * @param mode Transportation mode (default: Truck)
     * @return List of path segments
     */
    QList<PathSegment> findShortestPathWithinRegions(
        const QString& start,
        const QString& end,
        const QStringList& regions,
        TransportationMode mode = TransportationMode::Truck) const;

    /**
     * @brief Finds top N shortest paths
     * @param start Starting terminal name
     * @param end Ending terminal name
     * @param n Number of paths to find
     * @param mode Transportation mode (default: Truck)
     * @param skipDelays Skip same-mode delays if true
     * @return List of paths
     */
    QList<Path> findTopNShortestPaths(
        const QString& start,
        const QString& end,
        int n = 5,
        TransportationMode mode = TransportationMode::Truck,
        bool skipDelays = true) const;

    // Serialization
    /**
     * @brief Serializes the graph to JSON
     * @return JSON object representing the graph
     */
    QJsonObject serializeGraph() const;

    /**
     * @brief Deserializes a graph from JSON
     * @param data JSON data to deserialize
     * @param dir Directory path for terminal storage
     * @return Pointer to new TerminalGraph
     */
    static TerminalGraph* deserializeGraph(const QJsonObject& data,
                                           const QString& dir = QString());

    /**
     * @brief Saves the graph to a file
     * @param filepath Path to save the file
     */
    void saveToFile(const QString& filepath) const;

    /**
     * @brief Loads a graph from a file
     * @param filepath Path to load from
     * @param dir Directory path for terminal storage
     * @return Pointer to loaded TerminalGraph
     */
    static TerminalGraph* loadFromFile(const QString& filepath,
                                       const QString& dir = QString());

private:
    GraphImpl* m_graph;  // Graph implementation

    // Terminal management
    QString getCanonicalName(const QString& name) const;
    QHash<QString, QString> m_terminalAliases;  // alias -> canonical
    QHash<QString, QSet<QString>> m_canonicalToAliases;  // canonical -> aliases
    QHash<QString, Terminal*> m_terminals;  // canonical -> Terminal*

    QString m_pathToTerminalsDirectory;  // Storage path
    QVariantMap m_costFunctionParametersWeights;  // Cost weights
    QVariantMap m_defaultLinkAttributes;  // Default link attrs
    mutable QMutex m_mutex;  // Thread safety

    // Cost function
    double computeCost(const QVariantMap& params,
                       const QVariantMap& weights,
                       TransportationMode mode) const;

    // Path finding with exclusions
    QList<PathSegment> findShortestPathWithExclusions(
        const QString& start,
        const QString& end,
        TransportationMode mode,
        const QSet<QPair<QString, QString>>& edges,
        const QSet<QString>& nodes) const;
};

} // namespace TerminalSim

