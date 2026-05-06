#include "common.h"

#include <QMetaEnum>
#include <QString>
#include <stdexcept>
#include "LogCategories.h"

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

std::optional<TransportationMode>
EnumUtils::tryParseTransportationMode(const QString& str) {
    const QMetaEnum metaEnum = getTransportationModeEnum();
    bool ok = false;
    const QString trimmed = str.trimmed();
    
    // Try to convert string to enum value
    int value = metaEnum.keyToValue(trimmed.toUtf8().constData(), &ok);
    
    if (ok) {
        return static_cast<TransportationMode>(value);
    }

    const QString normalized = trimmed.toLower();
    if (normalized == QStringLiteral("any"))
        return TransportationMode::Any;
    if (normalized == QStringLiteral("ship"))
        return TransportationMode::Ship;
    if (normalized == QStringLiteral("truck"))
        return TransportationMode::Truck;
    if (normalized == QStringLiteral("train"))
        return TransportationMode::Train;
    
    // Try to convert from number if string contains a number
    value = trimmed.toInt(&ok);
    if (ok) {
        switch (static_cast<TransportationMode>(value)) {
            case TransportationMode::Any:
            case TransportationMode::Ship:
            case TransportationMode::Truck:
            case TransportationMode::Train:
                return static_cast<TransportationMode>(value);
        }
    }
    
    return std::nullopt;
}

TransportationMode EnumUtils::parseTransportationMode(const QString& str) {
    const auto parsed = tryParseTransportationMode(str);
    if (parsed)
        return *parsed;

    qCWarning(lcCommon) << "Invalid TransportationMode string:" << str;
    throw std::invalid_argument(
        QString("Invalid TransportationMode: %1").arg(str).toStdString());
}

TransportationMode EnumUtils::stringToTransportationMode(const QString& str) {
    return parseTransportationMode(str);
}

QString EnumUtils::terminalInterfaceToString(TerminalInterface interface) {
    const QMetaEnum metaEnum = getTerminalInterfaceEnum();
    return QString(metaEnum.valueToKey(static_cast<int>(interface)));
}

std::optional<TerminalInterface>
EnumUtils::tryParseTerminalInterface(const QString& str) {
    const QMetaEnum metaEnum = getTerminalInterfaceEnum();
    bool ok = false;
    const QString trimmed = str.trimmed();
    
    // Try to convert string to enum value
    int value = metaEnum.keyToValue(trimmed.toUtf8().constData(), &ok);
    
    if (ok) {
        return static_cast<TerminalInterface>(value);
    }

    const QString normalized = trimmed.toLower();
    if (normalized == QStringLiteral("land_side")
        || normalized == QStringLiteral("land"))
        return TerminalInterface::LAND_SIDE;
    if (normalized == QStringLiteral("sea_side")
        || normalized == QStringLiteral("sea"))
        return TerminalInterface::SEA_SIDE;
    if (normalized == QStringLiteral("air_side")
        || normalized == QStringLiteral("air"))
        return TerminalInterface::AIR_SIDE;
    
    // Try to convert from number if string contains a number
    value = trimmed.toInt(&ok);
    if (ok && value >= 0
        && value <= static_cast<int>(TerminalInterface::AIR_SIDE))
    {
        return static_cast<TerminalInterface>(value);
    }

    return std::nullopt;
}

TerminalInterface EnumUtils::parseTerminalInterface(const QString& str) {
    const auto parsed = tryParseTerminalInterface(str);
    if (parsed)
        return *parsed;

    qCWarning(lcCommon) << "Invalid TerminalInterface string:" << str;
    throw std::invalid_argument(
        QString("Invalid TerminalInterface: %1").arg(str).toStdString());
}

TerminalInterface EnumUtils::stringToTerminalInterface(const QString& str) {
    return parseTerminalInterface(str);
}

// Helper function to check if a transportation
// mode can use a terminal interface
bool canModeUseInterface(TransportationMode mode,
                         TerminalInterface interface) {
    switch (mode) {
        case TransportationMode::Truck:
            return interface == TerminalInterface::LAND_SIDE;
            
        case TransportationMode::Train:
            return interface == TerminalInterface::LAND_SIDE;

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

        case TerminalInterface::AIR_SIDE:
            return QObject::tr("Air_side interface for train operations");

        default:
            return QObject::tr("Unknown terminal interface");
    }
}

} // namespace TerminalSim
