#pragma once

#include <QString>

enum class FormatId {
    Unknown,
    A2L,
    DBC,
    LDF,
    // Reserved for the planned explorer read-back of mdf4-writer output
    // (see docs/plans/mdf4_viewer.md); no backend yet.
    MDF4
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
    case FormatId::Unknown:
    default:
        return QStringLiteral("Unknown");
    }
}
