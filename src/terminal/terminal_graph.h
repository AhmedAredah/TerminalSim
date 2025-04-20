/**
 * @file terminal_graph.h
 * @brief Defines the TerminalGraph class and supporting structures
 * @author Ahmed Aredah
 * @date 2025-03-21
 *
 * This file contains the declaration of the TerminalGraph class along with
 * supporting structures such as EdgeIdentifier and PathFindingContext. The
 * TerminalGraph class represents a network of terminals connected by routes,
 * and supports operations such as path finding, terminal management, route
 * management, and serialization.
 */

#pragma once

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QMutex>
#include <QObject>
#include <QSet>
#include <QString>

#include "common/common.h"
#include "terminal/terminal.h"
#include "terminal_graph_impl.h"
#include "terminal_path.h"
#include "terminal_path_segment.h"

namespace TerminalSim
{

/**
 * @struct EdgeIdentifier
 * @brief Uniquely identifies an edge in the terminal graph
 *
 * An EdgeIdentifier is used to uniquely identify an edge in the terminal graph
 * based on its source terminal, destination terminal, and transportation mode.
 * This structure is primarily used for edge exclusion during path finding
 * operations.
 */
struct EdgeIdentifier
{
    QString            from; ///< Source terminal name
    QString            to;   ///< Destination terminal name
    TransportationMode mode; ///< Transportation mode used on this edge

    /**
     * @brief Equality operator for EdgeIdentifier
     * @param other Another EdgeIdentifier to compare with
     * @return True if both EdgeIdentifiers are equal, false otherwise
     */
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

/**
 * @brief Hash function for EdgeIdentifier
 * @param edge The EdgeIdentifier to hash
 * @param seed Initial seed value for the hash
 * @return A hash value for the EdgeIdentifier
 *
 * This function is required to use EdgeIdentifier in QSet or QHash containers.
 */
inline uint qHash(const EdgeIdentifier &edge, uint seed = 0)
{
    seed ^= qHash(edge.from, seed);
    seed ^= qHash(edge.to, seed);
    seed ^= static_cast<uint>(edge.mode); // safe fallback
    return seed;
}

/**
 * @struct PathFindingContext
 * @brief Context information used during path finding operations
 *
 * This structure stores the context information needed during path finding
 * operations, including start and end terminals, transportation mode, terminal
 * pointers, and caches for terminal information to reduce redundant
 * calculations.
 */
struct PathFindingContext
{
    QString            startCanonical; ///< Canonical name of start terminal
    QString            endCanonical;   ///< Canonical name of end terminal
    TransportationMode mode;           ///< Transportation mode for path finding
    bool skipDelays; ///< Whether to skip same-mode delays at intermediate
                     ///< terminals
    QHash<QString, Terminal *>
                  termPointers; ///< Cache of terminal pointers for fast lookup
    QSet<QString> foundPathSignatures; ///< Set of found path signatures to
                                       ///< avoid duplicates
    bool isValid; ///< Whether the context is valid (terminals exist, etc.)

    /**
     * @struct TermInfo
     * @brief Cached terminal information to reduce redundant calculations
     *
     * This nested structure caches terminal handling time and cost information
     * to avoid recalculating these values during path finding operations.
     */
    struct TermInfo
    {
        double handlingTime; ///< Cached container handling time at terminal
        double cost;         ///< Cached container cost at terminal
    };

    QHash<QString, TermInfo>
        termInfoCache; ///< Cache of terminal information for fast lookup
};

/**
 * @class TerminalGraph
 * @brief Manages a network of terminals and routes
 *
 * This class represents a graph of terminals connected
 * by routes, supporting path finding, terminal and route management,
 * and serialization operations. It provides a comprehensive set of
 * methods for building and querying transportation networks.
 *
 * The class uses a thread-safe implementation to allow concurrent
 * access from multiple threads.
 */
class TerminalGraph : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Constructs a terminal graph
     * @param dir Directory path for terminal storage (optional)
     *
     * Initializes a new terminal graph with default cost function parameters
     * and link attributes. If a directory path is provided, it will be used
     * for terminal storage.
     */
    explicit TerminalGraph(const QString &dir = QString());

    /**
     * @brief Destructor, cleans up resources
     *
     * Releases all allocated terminal instances and other resources.
     */
    ~TerminalGraph() override;

    // Link management
    /**
     * @brief Sets default attributes for links
     * @param attrs Attributes to set as default
     *
     * Updates the default attributes used for new links/routes. These
     * attributes will be applied to any new routes added to the graph.
     */
    void setLinkDefaultAttributes(const QVariantMap &attrs);

    /**
     * @brief Sets cost function parameters
     * @param params Weights for cost computation
     *
     * Updates the weights used in the cost function for computing route costs.
     * These weights determine the relative importance of different factors
     * like travel time, distance, and cost in path finding.
     */
    void setCostFunctionParameters(const QVariantMap &params);

    // Terminal management
    /**
     * @brief Adds a terminal to the graph
     * @param names List of terminal names (first is canonical)
     * @param terminalDisplayName Display name for the terminal
     * @param config Custom configuration for the terminal
     * @param interfaces Terminal interfaces and modes
     * @param region Region the terminal belongs to (optional)
     * @return Pointer to the newly created Terminal
     *
     * Creates and adds a new terminal to the graph with the given names,
     * display name, configuration, interfaces, and region. The first name
     * in the names list is considered the canonical name for the terminal.
     * All other names are treated as aliases.
     */
    Terminal *addTerminal(
        const QStringList &names, const QString &terminalDisplayName,
        const QVariantMap                                       &config,
        const QMap<TerminalInterface, QSet<TransportationMode>> &interfaces,
        const QString &region = QString());

    /**
     * @brief Adds multiple terminals to the graph simultaneously
     * @param terminalsList List of terminal configurations
     * @return QMap of canonical names to Terminal pointers
     *
     * Creates and adds multiple terminals in one operation. Each terminal
     * configuration in the list should be a QVariantMap with the following
     * keys:
     * - "terminal_names": a QString or QStringList of terminal names (first is
     * canonical)
     * - "display_name": a QString display name for the terminal
     * - "terminal_interfaces": a QVariantMap of interfaces and modes
     * - "custom_config": a QVariantMap of terminal configuration
     * - "region": (optional) a QString region name
     */
    QMap<QString, Terminal *>
    addTerminals(const QList<QVariantMap> &terminalsList);

    /**
     * @brief Adds an alias to an existing terminal
     * @param name Terminal name to alias
     * @param alias New alias to add
     *
     * Adds a new alias to an existing terminal. The terminal is identified
     * by either its canonical name or an existing alias.
     */
    void addAliasToTerminal(const QString &name, const QString &alias);

    /**
     * @brief Gets aliases for a terminal
     * @param name Terminal name to query
     * @return List of aliases
     *
     * Returns a list of all aliases for the given terminal, identified by
     * either its canonical name or an existing alias.
     */
    QStringList getAliasesOfTerminal(const QString &name) const;

    // Route management
    /**
     * @brief Adds a route between terminals
     * @param id Unique route identifier
     * @param start Starting terminal name
     * @param end Ending terminal name
     * @param mode Transportation mode
     * @param attrs Route attributes (optional)
     * @return Pair of start and end terminal canonical names
     *
     * Adds a route between two terminals with the given mode and attributes.
     * If attributes are not provided, default link attributes will be used.
     * The terminals can be identified by either their canonical names or
     * aliases.
     */
    QPair<QString, QString> addRoute(const QString &id, const QString &start,
                                     const QString     &end,
                                     TransportationMode mode,
                                     const QVariantMap &attrs = QVariantMap());

    /**
     * @brief Adds multiple routes between terminals simultaneously
     * @param routesList List of route configurations
     * @return QList of pairs with start and end terminal canonical names
     *
     * Adds multiple routes with attributes to the graph in one operation.
     * Each route configuration in the list should be a QVariantMap with the
     * following keys:
     * - "route_id": a QString unique route identifier
     * - "start_terminal": a QString starting terminal name or alias
     * - "end_terminal": a QString ending terminal name or alias
     * - "mode": an int or QString transportation mode
     * - "attributes": (optional) a QVariantMap of route attributes
     */
    QList<QPair<QString, QString>>
    addRoutes(const QList<QVariantMap> &routesList);

    /**
     * @brief Gets edge attributes by mode
     * @param start Starting terminal name
     * @param end Ending terminal name
     * @param mode Transportation mode
     * @return Edge attributes, or empty if not found
     *
     * Retrieves the attributes of a specific edge (route) between two terminals
     * with the given transportation mode. The terminals can be identified by
     * either their canonical names or aliases.
     */
    QVariantMap getEdgeByMode(const QString &start, const QString &end,
                              TransportationMode mode) const;

    // Region operations
    /**
     * @brief Gets terminals in a region
     * @param region Region name
     * @return List of terminal names
     *
     * Returns a list of all terminal canonical names in the specified region.
     */
    QStringList getTerminalsByRegion(const QString &region) const;

    /**
     * @brief Gets routes between two regions
     * @param regionA First region name
     * @param regionB Second region name
     * @return List of route details
     *
     * Returns a list of all routes connecting terminals in two different
     * regions. Each route detail is a QVariantMap containing information about
     * the route.
     */
    QList<QVariantMap> getRoutesBetweenRegions(const QString &regionA,
                                               const QString &regionB) const;

    // Auto-connection methods
    /**
     * @brief Connects terminals by interface modes
     *
     * Automatically creates bidirectional routes between terminals that share
     * common interface modes. This is useful for quickly setting up a fully
     * connected network of terminals.
     */
    void connectTerminalsByInterfaceModes();

    /**
     * @brief Connects terminals in a region by mode
     * @param region Region name
     *
     * Automatically creates bidirectional routes between all terminals in the
     * specified region that share common transportation modes.
     */
    void connectTerminalsInRegionByMode(const QString &region);

    /**
     * @brief Connects regions by a specific mode
     * @param mode Transportation mode
     *
     * Automatically creates bidirectional routes between terminals in different
     * regions that support the specified transportation mode.
     */
    void connectRegionsByMode(TransportationMode mode);

    /**
     * @brief Changes weight of a route
     * @param start Starting terminal name
     * @param end Ending terminal name
     * @param mode Transportation mode
     * @param attrs New attributes to apply
     *
     * Updates the attributes of a specific route between two terminals with the
     * given transportation mode. The attributes determine the cost of the route
     * for path finding purposes.
     */
    void changeRouteWeight(const QString &start, const QString &end,
                           TransportationMode mode, const QVariantMap &attrs);

    // Terminal access
    /**
     * @brief Gets a terminal by name
     * @param name Terminal name
     * @return Pointer to terminal
     * @throws std::invalid_argument if terminal not found
     *
     * Retrieves a terminal by its name or alias. Throws an exception if the
     * terminal is not found.
     */
    Terminal *getTerminal(const QString &name) const;

    /**
     * @brief Checks if a terminal exists
     * @param name Terminal name
     * @return True if exists, false otherwise
     *
     * Checks if a terminal with the given name or alias exists in the graph.
     */
    bool terminalExists(const QString &name) const;

    /**
     * @brief Removes a terminal from the graph
     * @param name Terminal name
     * @return True if removed, false if not found
     *
     * Removes a terminal and all its associated routes from the graph.
     * The terminal can be identified by either its canonical name or an alias.
     */
    bool removeTerminal(const QString &name);

    // Graph queries
    /**
     * @brief Gets the number of terminals
     * @return Terminal count
     *
     * Returns the number of terminals in the graph.
     */
    int getTerminalCount() const;

    /**
     * @brief Gets all terminal names
     * @param includeAliases Include aliases if true
     * @return Map of canonical names to alias lists
     *
     * Returns a map of all terminal canonical names to their alias lists.
     * If includeAliases is false, the alias lists will be empty.
     */
    QMap<QString, QStringList>
    getAllTerminalNames(bool includeAliases = false) const;

    /**
     * @brief Clears the graph
     *
     * Removes all terminals, routes, and other data from the graph,
     * effectively resetting it to an empty state.
     */
    void clear();

    /**
     * @brief Gets status of a terminal or all terminals
     * @param name Terminal name, empty for all
     * @return Status details
     *
     * Returns a QVariantMap containing status information for a specific
     * terminal or all terminals if no name is provided. The status includes
     * container count, available capacity, max capacity, region, and aliases.
     */
    QVariantMap getTerminalStatus(const QString &name = QString()) const;

    /**
     * @brief Gets the terminal directory path
     * @return Directory path
     *
     * Returns the directory path used for terminal storage.
     */
    const QString &getPathToTerminalsDirectory() const;

    // Path finding
    /**
     * @brief Finds the shortest path between terminals
     * @param start Starting terminal name
     * @param end Ending terminal name
     * @param mode Transportation mode (default: Any)
     * @return List of path segments
     * @throws std::invalid_argument if terminals not found
     * @throws std::runtime_error if no path exists
     *
     * Finds the shortest path between two terminals using the specified
     * transportation mode. If mode is TransportationMode::Any, all modes
     * will be considered. The terminals can be identified by either their
     * canonical names or aliases.
     */
    QList<PathSegment>
    findShortestPath(const QString &start, const QString &end,
                     TransportationMode mode = TransportationMode::Any) const;

    /**
     * @brief Finds shortest path within regions
     * @param start Starting terminal name
     * @param end Ending terminal name
     * @param regions Allowed regions
     * @param mode Transportation mode (default: Any)
     * @return List of path segments
     * @throws std::invalid_argument if terminals not found or not in allowed
     * regions
     * @throws std::runtime_error if no path exists within allowed regions
     *
     * Finds the shortest path between two terminals that stays within the
     * specified regions. If mode is TransportationMode::Any, all modes
     * will be considered. The terminals can be identified by either their
     * canonical names or aliases.
     */
    QList<PathSegment> findShortestPathWithinRegions(
        const QString &start, const QString &end, const QStringList &regions,
        TransportationMode mode = TransportationMode::Any) const;

    /**
     * @brief Finds top N shortest paths
     * @param start Starting terminal name
     * @param end Ending terminal name
     * @param n Number of paths to find (default: 5)
     * @param mode Transportation mode (default: Any)
     * @param skipDelays Skip same-mode delays if true (default: true)
     * @return List of paths
     *
     * Finds the top N shortest paths between two terminals using the specified
     * transportation mode. This method uses a custom path-finding algorithm
     * that combines direct paths, shortest paths, and alternative paths to find
     * diverse routing options.
     *
     * If skipDelays is true, terminal handling delays will be skipped for
     * consecutive segments with the same transportation mode, simulating
     * seamless transfers.
     *
     * The terminals can be identified by either their canonical names or
     * aliases.
     */
    QList<Path>
    findTopNShortestPaths(const QString &start, const QString &end, int n = 5,
                          TransportationMode mode = TransportationMode::Any,
                          bool               skipDelays = true) const;

    // Serialization
    /**
     * @brief Serializes the graph to JSON
     * @return JSON object representing the graph
     *
     * Converts the entire graph, including terminals, routes, aliases, and
     * cost parameters, to a JSON object for storage or transmission.
     */
    QJsonObject serializeGraph() const;

    /**
     * @brief Deserializes a graph from JSON
     * @param data JSON data to deserialize
     * @param dir Directory path for terminal storage (optional)
     * @return Pointer to new TerminalGraph
     * @throws std::exception if deserialization fails
     *
     * Creates a new TerminalGraph from a JSON object previously created
     * by serializeGraph(). If a directory path is provided, it will be
     * used for terminal storage.
     */
    static TerminalGraph *deserializeGraph(const QJsonObject &data,
                                           const QString     &dir = QString());

    /**
     * @brief Saves the graph to a file
     * @param filepath Path to save the file
     * @throws std::runtime_error if file cannot be opened
     *
     * Serializes the graph to JSON and saves it to a file at the specified
     * path.
     */
    void saveToFile(const QString &filepath) const;

    /**
     * @brief Loads a graph from a file
     * @param filepath Path to load from
     * @param dir Directory path for terminal storage (optional)
     * @return Pointer to loaded TerminalGraph
     * @throws std::runtime_error if file cannot be opened
     * @throws std::exception if deserialization fails
     *
     * Loads a graph from a JSON file at the specified path. If a directory
     * path is provided, it will be used for terminal storage.
     */
    static TerminalGraph *loadFromFile(const QString &filepath,
                                       const QString &dir = QString());

private:
    GraphImpl *m_graph; ///< Graph implementation

    // Terminal management
    /**
     * @brief Gets the canonical name for a terminal
     * @param name Terminal name or alias
     * @return Canonical name
     *
     * Resolves a terminal name or alias to its canonical name.
     * If the name is not found in the aliases map, it is returned as is.
     */
    QString getCanonicalName(const QString &name) const;

    QHash<QString, QString> m_terminalAliases; ///< Maps alias -> canonical name
    QHash<QString, QSet<QString>>
        m_canonicalToAliases; ///< Maps canonical name -> set of aliases
    QHash<QString, Terminal *>
        m_terminals; ///< Maps canonical name -> Terminal pointer

    QString     m_pathToTerminalsDirectory;      ///< Storage path for terminals
    QVariantMap m_costFunctionParametersWeights; ///< Weights for cost function
    QVariantMap m_defaultLinkAttributes; ///< Default attributes for new links
    mutable QMutex m_mutex;              ///< Mutex for thread safety

    // Cost function
    /**
     * @brief Computes cost for a path segment
     * @param params Parameters for cost calculation
     * @param weights Weights for cost factors
     * @param mode Transportation mode
     * @return Computed cost
     *
     * Computes the cost of a path segment based on its parameters, the
     * global cost weights, and the specific transportation mode.
     */
    double computeCost(const QVariantMap &params, const QVariantMap &weights,
                       TransportationMode mode) const;

    // Path finding with exclusions
    /**
     * @brief Finds shortest path with exclusions
     * @param start Starting terminal name
     * @param end Ending terminal name
     * @param requestedMode Transportation mode to consider
     * @param edgesToExclude Set of edges to exclude
     * @param nodesToExclude Set of nodes to exclude
     * @return List of path segments
     * @throws std::invalid_argument if terminals not found or excluded
     * @throws std::runtime_error if no path exists with exclusions
     *
     * Finds the shortest path between two terminals, excluding specific edges
     * and nodes. This is used for finding alternative paths in the
     * findTopNShortestPaths method.
     */
    QList<PathSegment> findShortestPathWithExclusions(
        const QString &start, const QString &end,
        TransportationMode requestedMode =
            TransportationMode::Any, // Keep this to filter available edges
        const QSet<EdgeIdentifier> &edgesToExclude = QSet<EdgeIdentifier>(),
        const QSet<QString>        &nodesToExclude = QSet<QString>()) const;

    /**
     * @brief Initializes context for path finding
     * @param start Starting terminal name
     * @param end Ending terminal name
     * @param mode Transportation mode
     * @param skipDelays Whether to skip same-mode delays
     * @return Initialized PathFindingContext
     *
     * Creates and initializes a PathFindingContext object with the given
     * parameters. This is used by the findTopNShortestPaths method to avoid
     * redundant calculations.
     */
    PathFindingContext initializePathFindingContext(const QString     &start,
                                                    const QString     &end,
                                                    TransportationMode mode,
                                                    bool skipDelays) const;

    /**
     * @brief Generates a signature for a path
     * @param segments Path segments
     * @return Path signature
     *
     * Generates a unique string signature for a path based on its segments.
     * This is used to detect duplicate paths in the findTopNShortestPaths
     * method.
     */
    QString generatePathSignature(const QList<PathSegment> &segments) const;

    /**
     * @brief Gets terminal info with caching
     * @param context Path finding context
     * @param name Terminal name
     * @return Reference to terminal info
     *
     * Retrieves or calculates terminal information (handling time and cost)
     * for a specific terminal, using a cache to avoid redundant calculations.
     */
    const PathFindingContext::TermInfo &getTermInfo(PathFindingContext &context,
                                                    const QString &name) const;

    /**
     * @brief Determines if terminal costs should be skipped
     * @param context Path finding context
     * @param terminalIndex Index of terminal in path
     * @param segments Path segments
     * @return True if costs should be skipped
     *
     * Determines whether terminal handling costs should be skipped for a
     * specific terminal in a path. Costs are skipped for consecutive segments
     * with the same transportation mode if skipDelays is true.
     */
    bool shouldSkipCosts(PathFindingContext &context, int terminalIndex,
                         const QList<PathSegment> &segments) const;

    /**
     * @brief Builds path details from segments
     * @param context Path finding context
     * @param segments Path segments
     * @param pathId Path identifier
     * @return Complete path with details
     *
     * Builds a complete Path object from a list of path segments, including
     * calculating terminal information, costs, and other details.
     */
    Path buildPathDetails(PathFindingContext       &context,
                          const QList<PathSegment> &segments, int pathId) const;

    /**
     * @brief Finds direct paths between terminals
     * @param context Path finding context
     * @param maxPaths Maximum number of paths to find
     * @return List of direct paths
     *
     * Finds direct paths (single-segment paths) between the start and end
     * terminals. This is used as part of the findTopNShortestPaths algorithm.
     */
    QList<Path> findDirectPaths(PathFindingContext &context,
                                int                 maxPaths) const;

    /**
     * @brief Finds and adds shortest path to results
     * @param context Path finding context
     * @param result List of paths to append to
     *
     * Finds the shortest path between the start and end terminals and adds it
     * to the result list if it's not already there. This is used as part of the
     * findTopNShortestPaths algorithm.
     */
    void findAndAddShortestPath(PathFindingContext &context,
                                QList<Path>        &result) const;

    /**
     * @brief Checks if result has multi-segment paths
     * @param paths List of paths
     * @return True if any path has multiple segments
     *
     * Checks if the list of paths contains any multi-segment paths.
     * This is used to determine which strategy to use for finding
     * additional paths in the findTopNShortestPaths method.
     */
    bool hasMultiSegmentPaths(const QList<Path> &paths) const;

    /**
     * @brief Finds additional paths by edge exclusion
     * @param context Path finding context
     * @param result List of paths to append to
     * @param n Target number of paths
     *
     * Finds additional paths by excluding edges from existing paths and
     * finding alternative routes. This is used as part of the
     * findTopNShortestPaths algorithm when multi-segment paths exist.
     */
    void findAdditionalPathsByEdgeExclusion(PathFindingContext &context,
                                            QList<Path> &result, int n) const;

    /**
     * @brief Finds additional paths via intermediate terminals
     * @param context Path finding context
     * @param result List of paths to append to
     * @param n Target number of paths
     *
     * Finds additional paths by routing through intermediate terminals.
     * This is used as part of the findTopNShortestPaths algorithm when
     * no multi-segment paths exist.
     */
    void findAdditionalPathsViaIntermediates(PathFindingContext &context,
                                             QList<Path> &result, int n) const;

    /**
     * @brief Sorts and finalizes paths
     * @param result List of paths to sort and finalize
     * @param n Target number of paths
     * @return Sorted and finalized list of paths
     *
     * Sorts paths by total cost, truncates to the target number of paths,
     * and updates path IDs to match the sorted order. This is used as the
     * final step in the findTopNShortestPaths algorithm.
     */
    QList<Path> sortAndFinalizePaths(QList<Path> &result, int n) const;

    QList<PathSegment> findShortestPathWithPerturbedCosts(
        const QString &start, const QString &end, TransportationMode mode,
        const QMap<EdgeIdentifier, double> &costMultipliers) const;
};

} // namespace TerminalSim
