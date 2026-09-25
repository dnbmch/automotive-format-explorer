#pragma once

#include "adapters/formatadapter.h"
#include "core/formatid.h"

#include <QDir>
#include <QFileInfoList>
#include <QStringList>

#include <memory>
#include <vector>

// One supported format: its identity, the file suffixes it claims (lower case,
// without the dot) and the adapter that loads them.
struct FormatEntry {
    FormatId id;
    QStringList extensions;
    std::unique_ptr<FormatAdapter> adapter;
};

// The formats an application supports, in file-dialog order. The application
// composes it and hands ownership to AppController.
using FormatList = std::vector<FormatEntry>;

// The entry claiming path's suffix, compared case-insensitively; null when no
// format claims it.
const FormatEntry* formatForPath(const FormatList& formats, const QString& path);

// One filter over every supported suffix, one per format, then all files.
QStringList fileDialogFilters(const FormatList& formats);

// The supported files directly inside dir, ordered by name.
QFileInfoList supportedFiles(const FormatList& formats, const QDir& dir);
