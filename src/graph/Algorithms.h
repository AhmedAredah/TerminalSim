#pragma once

#include "Graph.h"
#include <QDebug>
#include <algorithm>
#include <limits>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace GraphLib
{

template <typename VertexIdType, typename WeightType> class GraphAlgorithms
{
public:
    using GraphType = Graph<VertexIdType, WeightType>;
    using EdgeType  = typename GraphType::EdgeType;
    using EdgePath  = std::vector<EdgeType>;
    using EdgePathInfo =
        std::pair<EdgePath, WeightType>; // Path of edges and total weight

    /**
     * @brief Find the shortest path using Dijkstra's algorithm
     * @param graph Input graph
     * @param source Source vertex id
     * @param target Target vertex id
     * @param mode Filter edges by transportation mode (Any by default)
     * @return Path information or std::nullopt if no path exists
     */
    static std::optional<EdgePathInfo>
    dijkstraShortestPath(const GraphType &graph, const VertexIdType &source,
                         const VertexIdType             &target,
                         TerminalSim::TransportationMode mode =
                             TerminalSim::TransportationMode::Any)
    {
        // Priority queue for vertices to visit (weight, vertex)
        using QueueItem = std::pair<WeightType, VertexIdType>;
        std::priority_queue<QueueItem, std::vector<QueueItem>,
                            std::greater<QueueItem>>
            pq;

        // Maps for distances and previous edges (not just vertices)
        std::unordered_map<VertexIdType, WeightType> distance;
        std::unordered_map<VertexIdType, EdgeType>   previousEdge;
        std::unordered_set<VertexIdType>             visited;

        // Initialize distances with infinity
        const WeightType infinity = std::numeric_limits<WeightType>::max();
        for (const auto &vertex : graph.vertices())
        {
            distance[vertex] = infinity;
        }

        // Source vertex has 0 distance
        distance[source] = 0;
        pq.push(std::make_pair(0, source));

        // Early exit if source or target don't exist in distances map
        if (distance.find(source) == distance.end()
            || distance.find(target) == distance.end())
        {
            qDebug() << "Source or target vertex doesn't exist in the graph";
            return std::nullopt;
        }

        while (!pq.empty())
        {
            auto [dist, current] = pq.top();
            pq.pop();

            // Skip if already visited
            if (visited.find(current) != visited.end())
            {
                continue;
            }

            // Mark as visited
            visited.insert(current);

            // Early termination if reached target
            if (current == target)
            {
                break;
            }

            // Process outgoing edges
            auto edges = graph.outgoingEdges(current);
            for (const auto &edge : edges)
            {
                // Skip edges that don't match the transportation mode (if
                // specified)
                if (mode != TerminalSim::TransportationMode::Any
                    && edge.mode() != mode
                    && edge.mode() != TerminalSim::TransportationMode::Any)
                {
                    continue;
                }

                // Relax the edge
                VertexIdType next    = edge.target();
                WeightType   newDist = dist + edge.weight();

                if (newDist < distance[next])
                {
                    distance[next] = newDist;
                    previousEdge[next] =
                        edge; // Store the edge, not just the vertex
                    pq.push(std::make_pair(newDist, next));
                }
            }
        }

        // If we didn't reach the target, no path exists
        if (distance[target] == infinity)
        {
            qDebug() << "No path found from" << source << "to" << target;
            return std::nullopt;
        }

        // Reconstruct the path as a sequence of edges
        EdgePath     edgePath;
        VertexIdType current = target;
        while (current != source)
        {
            const EdgeType &edge = previousEdge[current];
            edgePath.push_back(edge);
            current = edge.source(); // Move to previous vertex
        }

        // Reverse to get source-to-target order
        std::reverse(edgePath.begin(), edgePath.end());

        return std::make_pair(edgePath, distance[target]);
    }

    /**
     * @brief Find the k shortest paths using Yen's algorithm
     * @param graph Input graph
     * @param source Source vertex id
     * @param target Target vertex id
     * @param k Number of shortest paths to find
     * @param mode Filter edges by transportation mode (Any by default)
     * @return Vector of path information (up to k paths)
     */
    static std::vector<EdgePathInfo>
    kShortestPaths(const GraphType &graph, const VertexIdType &source,
                   const VertexIdType &target, size_t k,
                   TerminalSim::TransportationMode mode =
                       TerminalSim::TransportationMode::Any)
    {
        std::vector<EdgePathInfo> kPaths;

        // Find the first shortest path using Dijkstra
        auto firstPath = dijkstraShortestPath(graph, source, target, mode);
        if (!firstPath.has_value())
        {
            qDebug() << "No path exists from" << source << "to" << target;
            return kPaths;
        }

        kPaths.push_back(firstPath.value());

        // Priority queue for candidate paths
        std::priority_queue<
            EdgePathInfo, std::vector<EdgePathInfo>,
            std::function<bool(const EdgePathInfo &, const EdgePathInfo &)>>
            candidates([](const EdgePathInfo &a, const EdgePathInfo &b) {
                return a.second > b.second; // Min heap by path weight
            });

        // For each of the k-1 shortest paths
        for (size_t i = 1; i < k; ++i)
        {
            const EdgePath &prevPath = kPaths.back().first;

            // For each edge in the previous path
            for (size_t j = 0; j < prevPath.size(); ++j)
            {
                VertexIdType spurNode = prevPath[j].source();
                EdgePath     rootPath(prevPath.begin(), prevPath.begin() + j);

                // Get the corresponding vertex path for modification
                auto rootVertexPath = edgePathToVertexPath(rootPath, source);

                // Create a modified graph
                GraphType modifiedGraph = createModifiedGraph(
                    graph, rootVertexPath, rootPath, kPaths, source);

                // Find the shortest path from spur node to target
                auto spurPath =
                    dijkstraShortestPath(modifiedGraph, spurNode, target, mode);
                if (!spurPath.has_value())
                {
                    continue;
                }

                // Create a total path by concatenating root path and spur path
                EdgePath totalPath = rootPath;
                totalPath.insert(totalPath.end(),
                                 spurPath.value().first.begin(),
                                 spurPath.value().first.end());

                // Calculate the total weight
                WeightType totalWeight = calculateEdgePathWeight(totalPath);

                // Add the candidate path
                EdgePathInfo candidate = std::make_pair(totalPath, totalWeight);

                // Check if this path is already in kPaths
                bool isDuplicate = false;
                for (const auto &existingPath : kPaths)
                {
                    if (areEdgePathsEqual(existingPath.first, totalPath))
                    {
                        isDuplicate = true;
                        break;
                    }
                }

                if (!isDuplicate)
                {
                    candidates.push(candidate);
                }
            }

            // If no more candidates are available, break
            if (candidates.empty())
            {
                break;
            }

            // Add the best candidate to kPaths
            kPaths.push_back(candidates.top());
            candidates.pop();
        }

        return kPaths;
    }

    static std::vector<EdgePathInfo>
    kShortestPathsModified(const GraphType &graph, const VertexIdType &source,
                           const VertexIdType &target, size_t k,
                           TerminalSim::TransportationMode mode =
                               TerminalSim::TransportationMode::Any)
    {
        std::vector<EdgePathInfo> kPaths;

        // Find the first shortest path using Dijkstra
        auto firstPath = dijkstraShortestPath(graph, source, target, mode);
        if (!firstPath.has_value())
        {
            qDebug() << "No path exists from" << source << "to" << target;
            return kPaths;
        }

        kPaths.push_back(firstPath.value());

        // Priority queue for candidate paths
        std::priority_queue<
            EdgePathInfo, std::vector<EdgePathInfo>,
            std::function<bool(const EdgePathInfo &, const EdgePathInfo &)>>
            candidates([](const EdgePathInfo &a, const EdgePathInfo &b) {
                return a.second > b.second; // Min heap by path weight
            });

        // Track all candidate paths we've seen to avoid duplicates
        std::set<std::vector<std::tuple<VertexIdType, VertexIdType,
                                        TerminalSim::TransportationMode>>>
            seenPathSignatures;

        // Add signature of first path
        seenPathSignatures.insert(getPathSignature(firstPath.value().first));

        // For each of the k-1 shortest paths
        for (size_t i = 1; i < k; ++i)
        {
            // If we need to process more than one path to find candidates, do
            // so
            size_t pathsToProcess = std::min(
                kPaths.size(), size_t(3)); // Process up to 3 recent paths

            for (size_t pathIdx = 0; pathIdx < pathsToProcess; ++pathIdx)
            {
                const EdgePath &prevPath =
                    kPaths[kPaths.size() - 1 - pathIdx].first;

                // For each potential deviation point in the path
                for (size_t j = 0; j < prevPath.size(); ++j)
                {
                    VertexIdType spurNode = prevPath[j].source();
                    EdgePath rootPath(prevPath.begin(), prevPath.begin() + j);

                    // Create a modified graph that encourages diversity
                    GraphType modifiedGraph = createDiverseModifiedGraph(
                        graph, spurNode, rootPath, kPaths, source);

                    // Find the shortest path from spur node to target
                    auto spurPath = dijkstraShortestPath(
                        modifiedGraph, spurNode, target, mode);
                    if (!spurPath.has_value())
                    {
                        continue;
                    }

                    // Create total path
                    EdgePath totalPath = rootPath;
                    totalPath.insert(totalPath.end(),
                                     spurPath.value().first.begin(),
                                     spurPath.value().first.end());

                    // Calculate total weight
                    WeightType totalWeight = calculateEdgePathWeight(totalPath);

                    // Create candidate
                    EdgePathInfo candidate =
                        std::make_pair(totalPath, totalWeight);

                    // Get path signature for duplicate checking
                    auto pathSignature = getPathSignature(totalPath);

                    // Only add if we haven't seen this path signature before
                    if (seenPathSignatures.find(pathSignature)
                        == seenPathSignatures.end())
                    {
                        candidates.push(candidate);
                        seenPathSignatures.insert(pathSignature);
                    }
                }
            }

            // If no more candidates are available, break
            if (candidates.empty())
            {
                break;
            }

            // Add the best candidate to kPaths
            kPaths.push_back(candidates.top());
            candidates.pop();
        }

        return kPaths;
    }

private:
    /**
     * @brief Convert an edge path to a vertex path
     * @param edgePath Path of edges
     * @param source Starting vertex
     * @return Path of vertices
     */
    static std::vector<VertexIdType>
    edgePathToVertexPath(const EdgePath &edgePath, const VertexIdType &source)
    {
        std::vector<VertexIdType> vertexPath;

        if (edgePath.empty())
        {
            vertexPath.push_back(source);
            return vertexPath;
        }

        vertexPath.push_back(edgePath[0].source());
        for (const auto &edge : edgePath)
        {
            vertexPath.push_back(edge.target());
        }

        return vertexPath;
    }

    /**
     * @brief Check if two edge paths are equal
     * @param path1 First path
     * @param path2 Second path
     * @return true if paths contain the same edges in the same order
     */
    static bool areEdgePathsEqual(const EdgePath &path1, const EdgePath &path2)
    {
        if (path1.size() != path2.size())
        {
            return false;
        }

        for (size_t i = 0; i < path1.size(); ++i)
        {
            const auto &edge1 = path1[i];
            const auto &edge2 = path2[i];

            if (edge1.source() != edge2.source()
                || edge1.target() != edge2.target()
                || edge1.weight() != edge2.weight()
                || edge1.mode() != edge2.mode())
            {
                return false;
            }
        }

        return true;
    }

    /**
     * @brief Calculate the total weight of an edge path
     * @param edgePath Path of edges
     * @return Total weight
     */
    static WeightType calculateEdgePathWeight(const EdgePath &edgePath)
    {
        WeightType totalWeight = 0;
        for (const auto &edge : edgePath)
        {
            totalWeight += edge.weight();
        }
        return totalWeight;
    }

    /**
     * @brief Create a modified graph for Yen's algorithm
     * @param originalGraph Original graph
     * @param rootVertexPath Vertex path to exclude from the graph
     * @param rootEdgePath Edge path to exclude from the graph
     * @param kPaths Previously found k shortest paths
     * @param source Source vertex id
     * @return Modified graph
     */
    static GraphType
    createModifiedGraph(const GraphType                 &originalGraph,
                        const std::vector<VertexIdType> &rootVertexPath,
                        const EdgePath                  &rootEdgePath,
                        const std::vector<EdgePathInfo> &kPaths,
                        const VertexIdType              &source)
    {
        GraphType modifiedGraph;

        // Add all vertices
        for (const auto &vertex : originalGraph.vertices())
        {
            modifiedGraph.addVertex(vertex);
        }

        // Get the spur node (where we start the new deviation)
        VertexIdType spurNode;
        if (!rootVertexPath.empty())
        {
            spurNode = rootVertexPath.back();
        }
        else if (!rootEdgePath.empty())
        {
            spurNode = rootEdgePath[0].source();
        }
        else
        {
            spurNode = source; // Default to source if both paths are empty
        }

        // Add edges, excluding those in rootPath
        for (const auto &sourceVertex : originalGraph.vertices())
        {
            auto edges = originalGraph.outgoingEdges(sourceVertex);

            for (const auto &edge : edges)
            {
                // Skip edges in rootPath
                bool skipEdge = false;

                // Check if edge is in rootEdgePath
                for (const auto &rootEdge : rootEdgePath)
                {
                    if (edge == rootEdge)
                    {
                        skipEdge = true;
                        break;
                    }
                }

                // Skip edges that would lead to a duplicate path
                if (!skipEdge && sourceVertex == spurNode)
                {
                    for (const auto &path : kPaths)
                    {
                        // Check if the current edge matches the next edge in a
                        // known path with the same prefix
                        if (path.first.size() > rootEdgePath.size())
                        {
                            bool samePrefix = true;
                            for (size_t i = 0; i < rootEdgePath.size(); ++i)
                            {
                                const auto &pathEdge = path.first[i];
                                const auto &rootEdge = rootEdgePath[i];

                                if (pathEdge.source() != rootEdge.source()
                                    || pathEdge.target() != rootEdge.target()
                                    || pathEdge.weight() != rootEdge.weight()
                                    || pathEdge.mode() != rootEdge.mode())
                                {
                                    samePrefix = false;
                                    break;
                                }
                            }

                            if (samePrefix
                                && path.first[rootEdgePath.size()].source()
                                       == edge.source()
                                && path.first[rootEdgePath.size()].target()
                                       == edge.target()
                                && path.first[rootEdgePath.size()].mode()
                                       == edge.mode())
                            {
                                skipEdge = true;
                                break;
                            }
                        }
                    }
                }

                if (!skipEdge)
                {
                    modifiedGraph.addEdge(edge.source(), edge.target(),
                                          edge.weight(), edge.mode());
                }
            }
        }

        return modifiedGraph;
    }

    static std::vector<
        std::tuple<VertexIdType, VertexIdType, TerminalSim::TransportationMode>>
    getPathSignature(const EdgePath &path)
    {
        std::vector<std::tuple<VertexIdType, VertexIdType,
                               TerminalSim::TransportationMode>>
            signature;
        for (const auto &edge : path)
        {
            signature.push_back(
                std::make_tuple(edge.source(), edge.target(), edge.mode()));
        }
        return signature;
    }

    /**
     * @brief Create a modified graph that encourages path diversity
     */
    static GraphType createDiverseModifiedGraph(
        const GraphType &originalGraph, const VertexIdType &spurNode,
        const EdgePath &rootPath, const std::vector<EdgePathInfo> &kPaths,
        const VertexIdType &source)
    {
        GraphType modifiedGraph = originalGraph.deepCopy();

        // Get the vertices in the root path
        std::vector<VertexIdType> rootVertices;
        if (!rootPath.empty())
        {
            rootVertices.push_back(rootPath[0].source());
            for (const auto &edge : rootPath)
            {
                rootVertices.push_back(edge.target());
            }
        }
        else
        {
            rootVertices.push_back(source);
        }

        // Remove edges that would create cycles with the root path
        for (size_t i = 0; i < rootVertices.size() - 1; ++i)
        {
            modifiedGraph.removeEdge(rootVertices[i + 1], rootVertices[i],
                                     TerminalSim::TransportationMode::Any);
        }

        // Remove edges from the root path to prevent reuse
        for (const auto &edge : rootPath)
        {
            modifiedGraph.removeEdge(edge.source(), edge.target(), edge.mode());
        }

        // Remove duplicate starting edges from spur node that would lead to
        // already found paths
        for (const auto &path : kPaths)
        {
            // Check if this path shares the same root path
            bool sameRoot = true;
            if (path.first.size() >= rootPath.size())
            {
                for (size_t i = 0; i < rootPath.size(); ++i)
                {
                    if (!(path.first[i] == rootPath[i]))
                    {
                        sameRoot = false;
                        break;
                    }
                }

                // If same root and path is long enough, remove the next edge to
                // avoid duplication
                if (sameRoot && path.first.size() > rootPath.size())
                {
                    const auto &nextEdge = path.first[rootPath.size()];
                    // Only remove if this edge starts from our spur node
                    if (nextEdge.source() == spurNode)
                    {
                        modifiedGraph.removeEdge(nextEdge.source(),
                                                 nextEdge.target(),
                                                 nextEdge.mode());
                    }
                }
            }
        }

        // Identify bottleneck edges and give them special treatment
        std::map<std::tuple<VertexIdType, VertexIdType,
                            TerminalSim::TransportationMode>,
                 int>
            edgeUsageCount;

        // Count usage of edges in existing paths
        for (const auto &path : kPaths)
        {
            for (const auto &edge : path.first)
            {
                edgeUsageCount[std::make_tuple(edge.source(), edge.target(),
                                               edge.mode())]++;
            }
        }

        // For edges that are already part of many paths (bottlenecks),
        // we want to penalize them slightly but not remove them
        for (auto it = modifiedGraph.vertices().begin();
             it != modifiedGraph.vertices().end(); ++it)
        {
            auto edges = modifiedGraph.outgoingEdges(*it);
            for (const auto &edge : edges)
            {
                auto edgeTuple =
                    std::make_tuple(edge.source(), edge.target(), edge.mode());
                auto usageIt = edgeUsageCount.find(edgeTuple);

                if (usageIt != edgeUsageCount.end() && usageIt->second > 2)
                {
                    // This is a commonly used edge - a potential bottleneck
                    // Check if there are alternative paths from this vertex
                    auto allOutgoing =
                        modifiedGraph.outgoingEdges(edge.source());
                    bool hasAlternativePaths = false;

                    for (const auto &altEdge : allOutgoing)
                    {
                        if (altEdge.target() != edge.target()
                            || altEdge.mode() != edge.mode())
                        {
                            hasAlternativePaths = true;
                            break;
                        }
                    }

                    if (hasAlternativePaths)
                    {
                        // If there are alternative paths, we can penalize this
                        // edge to encourage diversity
                        modifiedGraph.removeEdge(edge.source(), edge.target(),
                                                 edge.mode());

                        // Add it back with slightly higher weight
                        WeightType newWeight =
                            edge.weight() * (1.0 + (usageIt->second * 0.05));
                        modifiedGraph.addEdge(edge.source(), edge.target(),
                                              newWeight, edge.mode());
                    }
                }
            }
        }

        return modifiedGraph;
    }
};

} // namespace GraphLib
