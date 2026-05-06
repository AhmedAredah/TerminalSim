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
 * @brief Represents a complete path with detailed cost breakdowns
 *
 * Contains all segments and cost details for a path
 * between terminals.
 */
struct Path
{
    QString            pathUid;            ///< Stable identifier for the path
    int                pathId;             ///< Unique path identifier
    int                rank = 0;           ///< Zero-based authoritative rank
    QString            startTerminal;      ///< Canonical start terminal id
    QString            endTerminal;        ///< Canonical end terminal id
    int                requestedMode = 0;  ///< Requested discovery mode
    int                requestedTopN = 0;  ///< Requested top-N
    bool skipSameModeTerminalDelaysAndCosts = true;
    double             rankingCost = 0.0;  ///< Scalar used for ordering
    double             totalPathCost;      ///< Total cost of the path
    double             totalEdgeCosts;     ///< Sum of edge costs
    double             totalTerminalCosts; ///< Sum of terminal costs
    double             weightedTerminalDelayTotal = 0.0;
    double             weightedTerminalDirectCostTotal = 0.0;
    double             rawTerminalDelayTotal = 0.0;
    double             rawTerminalCostTotal = 0.0;
    QList<QVariantMap> terminalsInPath;    ///< Terminals in path
    QList<PathSegment> segments;           ///< Path segments

    // Detailed cost breakdowns
    QVariantMap costBreakdown; // Total costs by category

    QJsonObject toJson() const
    {
        QJsonObject pathObj;
        pathObj["path_uid"]             = pathUid;
        pathObj["path_id"]              = pathId;
        pathObj["rank"]                 = rank;
        pathObj["start_terminal"]       = startTerminal;
        pathObj["end_terminal"]         = endTerminal;
        pathObj["requested_mode"]       = requestedMode;
        pathObj["ranking_cost"]         = rankingCost;
        pathObj["total_path_cost"]      = totalPathCost;
        pathObj["weighted_edge_cost_total"] = totalEdgeCosts;
        pathObj["weighted_terminal_cost_total"] = totalTerminalCosts;
        pathObj["weighted_terminal_delay_total"] = weightedTerminalDelayTotal;
        pathObj["weighted_terminal_direct_cost_total"] =
            weightedTerminalDirectCostTotal;
        pathObj["raw_terminal_delay_total"] = rawTerminalDelayTotal;
        pathObj["raw_terminal_cost_total"]  = rawTerminalCostTotal;

        // Transitional aliases until CargoNetSim Phase 4 stops reading
        // the legacy names.
        pathObj["total_edge_costs"]     = totalEdgeCosts;
        pathObj["total_terminal_costs"] = totalTerminalCosts;

        QJsonObject discoveryContext;
        discoveryContext["start_terminal"] = startTerminal;
        discoveryContext["end_terminal"] = endTerminal;
        discoveryContext["requested_mode"] = requestedMode;
        discoveryContext["requested_top_n"] = requestedTopN;
        discoveryContext["skip_same_mode_terminal_delays_and_costs"] =
            skipSameModeTerminalDelaysAndCosts;
        pathObj["discovery_context"] = discoveryContext;

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

        // Add segments
        QJsonArray segmentsArray;
        for (const PathSegment &segment : segments)
        {
            segmentsArray.append(segment.toJson());
        }
        pathObj["segments"] = segmentsArray;

        // Add cost breakdown
        QJsonObject breakdownObj;
        for (auto it = costBreakdown.begin(); it != costBreakdown.end(); ++it)
        {
            breakdownObj[it.key()] = QJsonValue::fromVariant(it.value());
        }
        pathObj["cost_breakdown"] = breakdownObj;

        return pathObj;
    }
};
} // namespace TerminalSim
