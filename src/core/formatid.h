#pragma once

#include <QString>

enum class FormatId {
    Unknown,
    A2L,
    DBC,
    LDF,
    // Reserved for planned explorer read-back of workspace-written files
    // (mdf4-writer / tdms-writer output); no backend yet.
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
