#pragma once

#include <QString>
#include <QMetaEnum>
#include <QObject>

namespace TerminalSim {

// Define the namespace as a Qt namespace to enable meta-object features
Q_NAMESPACE_EXPORT(Q_CORE_EXPORT)

/**
 * @brief Defines supported transportation modes for terminals
 */
enum class TransportationMode {
    Truck = 0,
    Train = 1,
    Ship = 2
};
Q_ENUM_NS(TransportationMode)

/**
 * @brief Defines interfaces available at terminals for operations
 */
enum class TerminalInterface {
    LAND_SIDE = 0,
    SEA_SIDE = 1,
    RAIL_SIDE = 2
};
Q_ENUM_NS(TerminalInterface)

/**
 * @brief Utility functions for enums
 */
class EnumUtils {
public:
    static QString transportationModeToString(TransportationMode mode);
    static TransportationMode stringToTransportationMode(const QString& str);

    static QString terminalInterfaceToString(TerminalInterface interface);
    static TerminalInterface stringToTerminalInterface(const QString& str);
};

} // namespace TerminalSim
