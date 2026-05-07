#pragma once

#include <QString>
#include <QMetaEnum>
#include <QObject>
#include <optional>

namespace TerminalSim {

// Define the namespace as a Qt namespace to enable meta-object features.
// terminal_common is a static library, so the metaobject must not carry any
// dllexport/dllimport decoration. Q_NAMESPACE (without _EXPORT) emits the
// metaobject with internal linkage suitable for static libraries; using
// Q_NAMESPACE_EXPORT(Q_CORE_EXPORT) on MSVC made the symbol resolve as if
// it lived in QtCore.dll and broke linking from consumers.
Q_NAMESPACE

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
    LAND_SIDE = 0, /**< Interface for land-based
       transportation (e.g., trucks, trains)
     */
    SEA_SIDE = 1,  /**< Interface for maritime transportation
        (e.g., ships, barges) */
    AIR_SIDE = 2   /**< Interface for air transportation
                 (e.g., cargo planes) */
};
Q_ENUM_NS(TerminalInterface)

/**
 * @brief Utility functions for enums
 */
class EnumUtils {
public:
    static QString transportationModeToString(TransportationMode mode);
    static std::optional<TransportationMode>
    tryParseTransportationMode(const QString& str);
    static TransportationMode parseTransportationMode(const QString& str);
    static TransportationMode stringToTransportationMode(const QString& str);

    static QString terminalInterfaceToString(TerminalInterface interface);
    static std::optional<TerminalInterface>
    tryParseTerminalInterface(const QString& str);
    static TerminalInterface parseTerminalInterface(const QString& str);
    static TerminalInterface stringToTerminalInterface(const QString& str);
};

} // namespace TerminalSim
