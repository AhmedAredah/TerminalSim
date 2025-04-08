#pragma once
#include "common/common.h"
#include <QJsonObject>
#include <QString>
#include <QVariantMap>

namespace TerminalSim
{
/**
 * @struct PathSegment
 * @brief Represents a segment in a path
 *
 * Defines a single hop between terminals in a path,
 * including mode and weight.
 */
struct PathSegment
{
    QString            from;           ///< Starting terminal name
    QString            to;             ///< Ending terminal name
    TransportationMode mode;           ///< Mode of transport
    double             weight;         ///< Cost of the segment
    QString            fromTerminalId; ///< ID of start terminal
    QString            toTerminalId;   ///< ID of end terminal
    QVariantMap        attributes;     ///< Additional attributes

    /**
     * Converts this PathSegment to a QJsonObject
     * @return JSON representation of the segment
     */
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
        segmentObj["attributes"] = attrsObj;

        return segmentObj;
    }
};
} // namespace TerminalSim
