// Update Graph.h
#pragma once

#include "Edge.h"
#include <QDebug>
#include <QString>
#include <set>
#include <unordered_map>
#include <vector>

namespace GraphLib
{

template <typename VertexIdType, typename WeightType> class Graph
{
public:
    using EdgeType = Edge<VertexIdType, WeightType>;

    Graph() = default;

    /**
     * @brief Create a deep copy of the graph
     * @return A new Graph object with the same vertices and edges
     */
    Graph<VertexIdType, WeightType> deepCopy() const
    {
        Graph<VertexIdType, WeightType> newGraph;

        // Copy all vertices
        for (const auto &vertex : m_vertices)
        {
            newGraph.addVertex(vertex);
        }

        // Copy all edges
        for (const auto &[source, edgeList] : m_adjacencyList)
        {
            for (const auto &edge : edgeList)
            {
                newGraph.addEdge(edge.source(), edge.target(), edge.weight(),
                                 edge.mode());
            }
        }

        return newGraph;
    }

    /**
     * @brief Add a vertex to the graph
     * @param id Unique identifier for the vertex
     * @return true if vertex was added, false if already exists
     */
    bool addVertex(const VertexIdType &id)
    {
        if (m_adjacencyList.find(id) != m_adjacencyList.end())
        {
            // Handle QString differently from numeric types
            qDebug() << "Vertex already exists:" << id;
            return false;
        }

        m_adjacencyList[id] = std::vector<EdgeType>();
        m_vertices.insert(id);
        return true;
    }

    /**
     * @brief Add an edge to the graph
     * @param source Source vertex id
     * @param target Target vertex id
     * @param weight Edge weight
     * @param mode Transportation mode (cannot be TransportationMode::Any)
     * @return true if edge was added, false if vertices don't exist or mode is
     * Any
     */
    bool addEdge(const VertexIdType &source, const VertexIdType &target,
                 const WeightType               &weight,
                 TerminalSim::TransportationMode mode =
                     TerminalSim::TransportationMode::Any)
    {
        // Reject edges with TransportationMode::Any
        if (mode == TerminalSim::TransportationMode::Any)
        {
            qDebug() << "Cannot add edge with TransportationMode::Any. Specify "
                        "a concrete mode.";
            return false;
        }

        if (m_adjacencyList.find(source) == m_adjacencyList.end()
            || m_adjacencyList.find(target) == m_adjacencyList.end())
        {
            qDebug() << "Source or target vertex doesn't exist";
            return false;
        }

        EdgeType edge(source, target, weight, mode);

        // Check if an edge with the same source, target, AND mode already
        // exists
        bool edgeExists = hasEdge(source, target, mode);

        if (!edgeExists)
        {
            // Only add if this specific edge (with this mode) doesn't exist
            m_adjacencyList[source].push_back(edge);

            // Update the edges set
            m_edges.insert(std::make_tuple(source, target, mode));
            return true;
        }
        else
        {
            qDebug() << "Edge already exists from" << source << "to" << target
                     << "with mode" << static_cast<int>(mode);
            return false;
        }
    }

    /**
     * @brief Check if an edge exists between two vertices
     * @param source Source vertex id
     * @param target Target vertex id
     * @param mode Transportation mode (default: Any)
     * @return true if edge exists, false otherwise
     */
    bool hasEdge(const VertexIdType &source, const VertexIdType &target,
                 TerminalSim::TransportationMode mode =
                     TerminalSim::TransportationMode::Any) const
    {
        if (mode == TerminalSim::TransportationMode::Any)
        {
            // Check if any edge exists between source and target
            for (const auto &edge : m_adjacencyList.at(source))
            {
                if (edge.target() == target && edge.source() == source)
                {
                    return true;
                }
            }
            return false;
        }
        else
        {
            // Check for specific mode
            return m_edges.find(std::make_tuple(source, target, mode))
                   != m_edges.end();
        }
    }

    /**
     * @brief Remove an edge or edges from the graph
     * @param source Source vertex id
     * @param target Target vertex id
     * @param mode Transportation mode (if Any, removes all edges between source
     * and target)
     * @return true if any edge was removed, false if none existed
     */
    bool removeEdge(const VertexIdType &source, const VertexIdType &target,
                    TerminalSim::TransportationMode mode =
                        TerminalSim::TransportationMode::Any)
    {
        // Check if vertices exist
        if (m_adjacencyList.find(source) == m_adjacencyList.end()
            || m_adjacencyList.find(target) == m_adjacencyList.end())
        {
            qDebug() << "Source or target vertex doesn't exist";
            return false;
        }

        auto &edges   = m_adjacencyList[source];
        bool  removed = false;

        if (mode == TerminalSim::TransportationMode::Any)
        {
            // Remove all edges from source to target regardless of mode
            auto it = edges.begin();
            while (it != edges.end())
            {
                if (it->target() == target)
                {
                    // Remove from edges set
                    m_edges.erase(std::make_tuple(source, target, it->mode()));

                    // Remove from adjacency list and update iterator
                    it      = edges.erase(it);
                    removed = true;
                }
                else
                {
                    ++it;
                }
            }
        }
        else
        {
            // Remove specific edge with given mode
            auto it = std::find_if(edges.begin(), edges.end(),
                                   [&target, &mode](const EdgeType &edge) {
                                       return edge.target() == target
                                              && edge.mode() == mode;
                                   });

            if (it != edges.end())
            {
                // Edge found, remove it
                edges.erase(it);

                // Remove from edges set
                m_edges.erase(std::make_tuple(source, target, mode));
                removed = true;
            }
        }

        if (!removed)
        {
            if (mode == TerminalSim::TransportationMode::Any)
            {
                qDebug() << "No edges from" << source << "to" << target
                         << "exist";
            }
            else
            {
                qDebug() << "Edge from" << source << "to" << target
                         << "with mode" << static_cast<int>(mode)
                         << "doesn't exist";
            }
        }

        return removed;
    }

    /**
     * @brief Get all vertices in the graph
     * @return Set of vertex ids
     */
    const std::set<VertexIdType> &vertices() const
    {
        return m_vertices;
    }

    /**
     * @brief Get all edges from a specific vertex
     * @param source Source vertex id
     * @return Vector of edges from the vertex (empty if vertex doesn't exist)
     */
    // Modified to return by value instead of optional reference
    std::vector<EdgeType> outgoingEdges(const VertexIdType &source) const
    {
        auto it = m_adjacencyList.find(source);
        if (it == m_adjacencyList.end())
        {
            return {}; // Return empty vector if vertex doesn't exist
        }
        return it->second;
    }

    /**
     * @brief Get number of vertices in the graph
     * @return Vertex count
     */
    size_t vertexCount() const
    {
        return m_vertices.size();
    }

    /**
     * @brief Get number of edges in the graph
     * @return Edge count
     */
    size_t edgeCount() const
    {
        return m_edges.size();
    }

    /**
     * @brief Print graph structure
     * @param detailed If true, prints edge details
     */
    void print(bool detailed = false) const
    {
        qDebug() << "Graph with" << vertexCount() << "vertices and"
                 << edgeCount() << "edges";

        for (const auto &[source, edges] : m_adjacencyList)
        {
            qDebug() << "Vertex" << source << "connections:";
            for (const auto &edge : edges)
            {
                if (detailed)
                {
                    qDebug() << "  " << edge.toString();
                }
                else
                {
                    qDebug() << "  ->" << edge.target();
                }
            }
        }
    }

private:
    std::unordered_map<VertexIdType, std::vector<EdgeType>> m_adjacencyList;
    std::set<VertexIdType>                                  m_vertices;
    std::set<
        std::tuple<VertexIdType, VertexIdType, TerminalSim::TransportationMode>>
        m_edges;
};

} // namespace GraphLib
