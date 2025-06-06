#pragma once
#include "common/common.h"
#include <QJsonObject>
#include <QString>
#include <QVariantMap>
namespace TerminalSim
{
/**
 * @struct PathSegment
 * @brief Represents a segment in a path with enhanced cost metrics
 */
class PathSegment
{
public:
    QString            from;           ///< Starting terminal name
    QString            to;             ///< Ending terminal name
    TransportationMode mode;           ///< Mode of transport
    double             weight;         ///< Cost of the segment
    QString            fromTerminalId; ///< ID of start terminal
    QString            toTerminalId;   ///< ID of end terminal
    QVariantMap        attributes;     ///< Additional attributes

    // Enhanced cost metrics
    QVariantMap estimatedValues; // Raw values by transportation mode
    QVariantMap estimatedCost;   // Computed costs with weights applied

    struct CostDetails
    {
        double      totalCost = 0.0;
        QVariantMap estimatedValues;
        QVariantMap costMap;
    };

    CostDetails estimateTotalCostByWeights(const QHash<QString, double> weights)
    {
        CostDetails costDetails;
        for (auto it = attributes.begin(); it != attributes.end(); ++it)
        {
            QString key = it.key();
            costDetails.estimatedValues.insert(key, it.value());
            double result = it.value().toDouble() * weights.value(key, 0.0);
            costDetails.costMap.insert(key, result);
            costDetails.totalCost += result;
        }
        return costDetails;
    }

    QJsonObject toJson() const
    {
        QJsonObject segmentObj;
        segmentObj["from"]             = from;
        segmentObj["to"]               = to;
        segmentObj["mode"]             = static_cast<int>(mode);
        segmentObj["weight"]           = weight;
        segmentObj["from_terminal_id"] = fromTerminalId;
        segmentObj["to_terminal_id"]   = toTerminalId;

        // Add attributes
        QJsonObject attrsObj;
        for (auto it = attributes.begin(); it != attributes.end(); ++it)
        {
            attrsObj[it.key()] = QJsonValue::fromVariant(it.value());
        }

        // Add estimated values
        QJsonObject valuesObj;
        for (auto it = estimatedValues.begin(); it != estimatedValues.end();
             ++it)
        {
            valuesObj[it.key()] = QJsonValue::fromVariant(it.value());
        }
        attrsObj["estimated_values"] = valuesObj;

        // Add estimated costs
        QJsonObject costsObj;
        for (auto it = estimatedCost.begin(); it != estimatedCost.end(); ++it)
        {
            costsObj[it.key()] = QJsonValue::fromVariant(it.value());
        }
        attrsObj["estimated_cost"] = costsObj;
        segmentObj["attributes"]   = attrsObj;

        return segmentObj;
    }
};
} // namespace TerminalSim
