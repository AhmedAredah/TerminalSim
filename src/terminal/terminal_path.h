#pragma once
#include "terminal_path_segment.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QVariantMap>

namespace TerminalSim
{
/**
 * @struct Path
 * @brief Represents a complete path in the graph
 *
 * Contains all segments and cost details for a path
 * between terminals.
 */
struct Path
{
    int                pathId;             ///< Unique path identifier
    double             totalPathCost;      ///< Total cost of the path
    double             totalEdgeCosts;     ///< Sum of edge costs
    double             totalTerminalCosts; ///< Sum of terminal costs
    QList<QVariantMap> terminalsInPath;    ///< Terminals in path
    QList<PathSegment> segments;           ///< Path segments

    /**
     * Converts this Path to a QJsonObject
     * @return JSON representation of the path
     */
    QJsonObject toJson() const
    {
        QJsonObject pathObj;
        pathObj["path_id"]              = pathId;
        pathObj["total_path_cost"]      = totalPathCost;
        pathObj["total_edge_costs"]     = totalEdgeCosts;
        pathObj["total_terminal_costs"] = totalTerminalCosts;

        // Add terminals in path
        QJsonArray terminalsArray;
        for (const QVariantMap &terminal : terminalsInPath)
        {
            QJsonObject terminalObj;
            for (auto it = terminal.begin(); it != terminal.end(); ++it)
            {
                terminalObj[it.key()] = QJsonValue::fromVariant(it.value());
            }
            terminalsArray.append(terminalObj);
        }
        pathObj["terminals_in_path"] = terminalsArray;

        // Add segments - now using the PathSegment's toJson method
        QJsonArray segmentsArray;
        for (const PathSegment &segment : segments)
        {
            segmentsArray.append(segment.toJson());
        }
        pathObj["segments"] = segmentsArray;

        return pathObj;
    }
};

} // namespace TerminalSim
