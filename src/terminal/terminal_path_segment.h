#pragma once

#include <QString>
#include <QVariantMap>
#include "common/common.h"

namespace TerminalSim {

/**
 * @struct PathSegment
 * @brief Represents a segment in a path
 *
 * Defines a single hop between terminals in a path,
 * including mode and weight.
 */
struct PathSegment {
    QString from;            ///< Starting terminal name
    QString to;              ///< Ending terminal name
    TransportationMode mode; ///< Mode of transport
    double weight;           ///< Cost of the segment
    QString fromTerminalId;  ///< ID of start terminal
    QString toTerminalId;    ///< ID of end terminal
    QVariantMap attributes;  ///< Additional attributes
};

} // namespace TerminalSim
