#include "terminal_graph_impl.h"

namespace TerminalSim {

QList<GraphImpl::InternalEdge>
GraphImpl::getEdges(const QString& from,
                                   const QString& to) const
{
    QList<InternalEdge> result;
    if (!adjacencyList.contains(from)) {
        return result;
    }

    const QList<InternalEdge>& edges = adjacencyList[from];
    for (const InternalEdge& edge : edges) {
        if (edge.to == to) {
            result.append(edge);
        }
    }
    return result;
}

GraphImpl::InternalEdge*
GraphImpl::findEdge(const QString& from,
                                   const QString& to,
                                   TransportationMode mode)
{
    if (!adjacencyList.contains(from)) {
        return nullptr;
    }

    QList<InternalEdge>& edges = adjacencyList[from];
    for (InternalEdge& edge : edges) {
        if (edge.to == to && edge.mode == mode) {
            return &edge;
        }
    }
    return nullptr;
}

void GraphImpl::addEdge(const QString& from,
                                       const QString& to,
                                       const QString& routeId,
                                       TransportationMode mode,
                                       const QVariantMap& attrs)
{
    if (!adjacencyList.contains(from)) {
        adjacencyList[from] = QList<InternalEdge>();
    }
    if (!adjacencyList.contains(to)) {
        adjacencyList[to] = QList<InternalEdge>();
    }

    InternalEdge edge{to, routeId, mode, attrs};
    QList<InternalEdge>& edges = adjacencyList[from];
    for (int i = 0; i < edges.size(); ++i) {
        if (edges[i].to == to && edges[i].mode == mode) {
            edges[i] = edge;
            return;
        }
    }
    edges.append(edge);
}

void GraphImpl::removeNode(const QString& node)
{
    if (!adjacencyList.contains(node)) {
        return;
    }

    for (auto it = adjacencyList.begin(); it != adjacencyList.end(); ++it) {
        if (it.key() == node) continue;
        QList<InternalEdge>& edges = it.value();
        for (int i = edges.size() - 1; i >= 0; --i) {
            if (edges[i].to == node) {
                edges.removeAt(i);
            }
        }
    }
    adjacencyList.remove(node);
    nodeAttributes.remove(node);
}

void GraphImpl::clear()
{
    adjacencyList.clear();
    nodeAttributes.clear();
}

QStringList GraphImpl::getNodes() const
{
    return adjacencyList.keys();
}

void GraphImpl::setNodeAttribute(const QString& node,
                                                const QString& key,
                                                const QVariant& value)
{
    if (!adjacencyList.contains(node)) {
        adjacencyList[node] = QList<InternalEdge>();
    }
    if (!nodeAttributes.contains(node)) {
        nodeAttributes[node] = QVariantMap();
    }
    nodeAttributes[node][key] = value;
}

QVariant GraphImpl::getNodeAttribute(const QString& node,
                                                    const QString& key) const
{
    if (!nodeAttributes.contains(node)) {
        return QVariant();
    }
    return nodeAttributes[node].value(key);
}

} // namespace TerminalSim
