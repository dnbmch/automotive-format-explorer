#pragma once

#include <QString>

// An address or id as engineers write it: "0x", then upper-case digits,
// zero-padded to `width` digits.
inline QString hexText(quint64 value, int width = 0) {
    return QStringLiteral("0x") + QStringLiteral("%1").arg(value, width, 16, QChar('0')).toUpper();
}
