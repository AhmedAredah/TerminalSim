#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QVariantMap>
#include "common/common.h"

namespace TerminalSim {

class GraphImpl {
public:
    /**
     * @struct InternalEdge
     * @brief Represents an edge in the graph
     *
     * Defines a connection between terminals with
     * attributes and mode.
     */
    struct InternalEdge {
        QString to;              ///< Destination terminal
        QString routeId;         ///< Unique route ID
        TransportationMode mode; ///< Transport mode
        QVariantMap attributes;  ///< Edge attributes

        /**
         * @brief Equality operator
         * @param other Edge to compare with
         * @return True if edges are equal
         */
        bool operator==(const InternalEdge& other) const {
            return to == other.to &&
                   routeId == other.routeId &&
                   mode == other.mode;
        }
    };

    QHash<QString, QList<InternalEdge>> adjacencyList; ///< Graph edges
    QHash<QString, QVariantMap> nodeAttributes; ///< Node attributes

    /**
     * @brief Gets edges between two nodes
     * @param from Starting node
     * @param to Ending node
     * @return List of edges
     */
    QList<InternalEdge> getEdges(const QString& from,
                                 const QString& to) const;

    /**
     * @brief Finds an edge by mode
     * @param from Starting node
     * @param to Ending node
     * @param mode Transport mode
     * @return Pointer to edge, or nullptr
     */
    InternalEdge* findEdge(const QString& from,
                           const QString& to,
                           TransportationMode mode);

    /**
     * @brief Adds an edge to the graph
     * @param from Starting node
     * @param to Ending node
     * @param routeId Route identifier
     * @param mode Transport mode
     * @param attrs Edge attributes
     */
    void addEdge(const QString& from,
                 const QString& to,
                 const QString& routeId,
                 TransportationMode mode,
                 const QVariantMap& attrs);

    /**
     * @brief Removes a node and its edges
     * @param node Node to remove
     */
    void removeNode(const QString& node);

    /**
     * @brief Clears the graph
     */
    void clear();

    /**
     * @brief Gets all nodes in the graph
     * @return List of node names
     */
    QStringList getNodes() const;

    /**
     * @brief Sets a node attribute
     * @param node Node name
     * @param key Attribute key
     * @param value Attribute value
     */
    void setNodeAttribute(const QString& node,
                          const QString& key,
                          const QVariant& value);

    /**
     * @brief Gets a node attribute
     * @param node Node name
     * @param key Attribute key
     * @return Attribute value
     */
    QVariant getNodeAttribute(const QString& node,
                              const QString& key) const;
};

} // namespace TerminalSim
