#include "common.h"

#include <QMetaEnum>
#include <QString>
#include <QDebug>

namespace TerminalSim {

// Define the static MetaEnum instances for conversion utilities
static QMetaEnum getTransportationModeEnum() {
    const QMetaEnum metaEnum = QMetaEnum::fromType<TransportationMode>();
    return metaEnum;
}

static QMetaEnum getTerminalInterfaceEnum() {
    const QMetaEnum metaEnum = QMetaEnum::fromType<TerminalInterface>();
    return metaEnum;
}

QString EnumUtils::transportationModeToString(TransportationMode mode) {
    const QMetaEnum metaEnum = getTransportationModeEnum();
    return QString(metaEnum.valueToKey(static_cast<int>(mode)));
}

TransportationMode EnumUtils::stringToTransportationMode(const QString& str) {
    const QMetaEnum metaEnum = getTransportationModeEnum();
    bool ok = false;
    
    // Try to convert string to enum value
    int value = metaEnum.keyToValue(str.toUtf8().constData(), &ok);
    
    if (ok) {
        return static_cast<TransportationMode>(value);
    }
    
    // Try to convert from number if string contains a number
    value = str.toInt(&ok);
    if (ok && value >= 0 &&
        value <= static_cast<int>(TransportationMode::Ship)) {
        return static_cast<TransportationMode>(value);
    }
    
    // Return default value if conversion fails
    qWarning() << "Invalid TransportationMode string:"
               << str
               << "- defaulting to Truck";
    return TransportationMode::Truck;
}

QString EnumUtils::terminalInterfaceToString(TerminalInterface interface) {
    const QMetaEnum metaEnum = getTerminalInterfaceEnum();
    return QString(metaEnum.valueToKey(static_cast<int>(interface)));
}

TerminalInterface EnumUtils::stringToTerminalInterface(const QString& str) {
    const QMetaEnum metaEnum = getTerminalInterfaceEnum();
    bool ok = false;
    
    // Try to convert string to enum value
    int value = metaEnum.keyToValue(str.toUtf8().constData(), &ok);
    
    if (ok) {
        return static_cast<TerminalInterface>(value);
    }
    
    // Try to convert from number if string contains a number
    value = str.toInt(&ok);
    if (ok && value >= 0 &&
        value <= static_cast<int>(TerminalInterface::RAIL_SIDE)) {
        return static_cast<TerminalInterface>(value);
    }
    
    // Return default value if conversion fails
    qWarning() << "Invalid TerminalInterface string:"
               << str
               << "- defaulting to LAND_SIDE";
    return TerminalInterface::LAND_SIDE;
}

// Helper function to check if a transportation
// mode can use a terminal interface
bool canModeUseInterface(TransportationMode mode,
                         TerminalInterface interface) {
    switch (mode) {
        case TransportationMode::Truck:
            return interface == TerminalInterface::LAND_SIDE;
            
        case TransportationMode::Train:
            return interface == TerminalInterface::RAIL_SIDE;
            
        case TransportationMode::Ship:
            return interface == TerminalInterface::SEA_SIDE;
            
        default:
            return false;
    }
}

// Return a descriptive string for a transportation mode
QString getTransportationModeDescription(TransportationMode mode) {
    switch (mode) {
        case TransportationMode::Truck:
            return QObject::tr("Road transportation via truck");
            
        case TransportationMode::Train:
            return QObject::tr("Rail transportation via train");
            
        case TransportationMode::Ship:
            return QObject::tr("Maritime transportation via ship");
            
        default:
            return QObject::tr("Unknown transportation mode");
    }
}

// Return a descriptive string for a terminal interface
QString getTerminalInterfaceDescription(TerminalInterface interface) {
    switch (interface) {
        case TerminalInterface::LAND_SIDE:
            return QObject::tr("Land-side interface for truck operations");
            
        case TerminalInterface::SEA_SIDE:
            return QObject::tr("Sea-side interface for ship operations");
            
        case TerminalInterface::RAIL_SIDE:
            return QObject::tr("Rail-side interface for train operations");
            
        default:
            return QObject::tr("Unknown terminal interface");
    }
}

} // namespace TerminalSim
