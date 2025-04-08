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
enum class TransportationMode
{
    Any   = -1, /**< Any transportation mode */
    Ship  = 0,  /**< Maritime vessel transportation */
    Truck = 1,  /**< Road-based truck transportation */
    Train = 2,  /**< Rail-based train transportation */
};
Q_ENUM_NS(TransportationMode)

/**
 * @brief Defines interfaces available at terminals for operations
 */
enum class TerminalInterface
{
    LAND_SIDE, /**< Interface for land-based
       transportation (e.g., trucks, trains)
     */
    SEA_SIDE,  /**< Interface for maritime transportation
        (e.g., ships, barges) */
    AIR_SIDE   /**< Interface for air transportation
                 (e.g., cargo planes) */
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
