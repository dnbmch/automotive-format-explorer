#include "core/formatlist.h"

#include <QFileInfo>

const FormatEntry* formatForPath(const FormatList& formats, const QString& path) {
    const QString suffix = QFileInfo(path).suffix();
    for (const FormatEntry& format : formats) {
        for (const QString& extension : format.extensions) {
            if (extension.compare(suffix, Qt::CaseInsensitive) == 0) {
                return &format;
            }
        }
    }
    return nullptr;
}

QStringList fileDialogFilters(const FormatList& formats) {
    QStringList filters;
    QStringList allPatterns;
    for (const FormatEntry& format : formats) {
        QStringList patterns;
        for (const QString& extension : format.extensions) {
            patterns.push_back(QStringLiteral("*.") + extension);
        }
        allPatterns += patterns;
        filters.push_back(QStringLiteral("%1 files (%2)")
                              .arg(formatDisplayName(format.id), patterns.join(u' ')));
    }
    filters.prepend(QStringLiteral("Automotive files (%1)").arg(allPatterns.join(u' ')));
    filters.push_back(QStringLiteral("All files (*)"));
    return filters;
}

QFileInfoList supportedFiles(const FormatList& formats, const QDir& dir) {
    QFileInfoList supported;
    for (const QFileInfo& entry : dir.entryInfoList(QDir::Files, QDir::Name)) {
        if (formatForPath(formats, entry.fileName())) {
            supported.push_back(entry);
        }
    }
    return supported;
}
