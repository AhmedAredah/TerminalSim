#pragma once

#include <QList>
#include <QVariantMap>
#include "terminal_path_segment.h"

namespace TerminalSim {

/**
 * @struct Path
 * @brief Represents a complete path in the graph
 *
 * Contains all segments and cost details for a path
 * between terminals.
 */
struct Path {
    int pathId;               ///< Unique path identifier
    double totalPathCost;     ///< Total cost of the path
    double totalEdgeCosts;    ///< Sum of edge costs
    double totalTerminalCosts;///< Sum of terminal costs
    QList<QVariantMap> terminalsInPath; ///< Terminals in path
    QList<PathSegment> segments; ///< Path segments
};

} // namespace TerminalSim
