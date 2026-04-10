#pragma once

#include <QString>

enum class FormatId {
    Unknown,
    A2L,
    DBC,
    LDF,
    MDF4,
    TDMS
};

inline QString formatDisplayName(FormatId format) {
    switch (format) {
    case FormatId::A2L:
        return QStringLiteral("A2L");
    case FormatId::DBC:
        return QStringLiteral("DBC");
    case FormatId::LDF:
        return QStringLiteral("LDF");
    case FormatId::MDF4:
        return QStringLiteral("MDF4");
    case FormatId::TDMS:
        return QStringLiteral("TDMS");
    case FormatId::Unknown:
    default:
        return QStringLiteral("Unknown");
    }
}
