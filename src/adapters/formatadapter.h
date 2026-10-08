#pragma once

#include "core/diagnostics.h"

#include <QString>
#include <QList>

#include <atomic>
#include <memory>

class DocumentSession;

struct LoadResult {
    std::unique_ptr<DocumentSession> session;
    QList<DiagnosticMessage> diagnostics;
    // A retained diagnostic session can describe a failed opening. Ordinary
    // recoverable diagnostics leave this empty; they do not make an open fail.
    QString openingError = {};
};

// Loads one file into an owning session. Format identity and the suffixes an
// adapter serves belong to the application's FormatList entry. `cancel`, set
// from any thread while load() runs, asks the load to stop early; an adapter
// whose parser cannot stop ignores it.
class FormatAdapter {
public:
    virtual ~FormatAdapter() = default;

    virtual LoadResult load(const QString& path, const std::atomic<bool>& cancel) const = 0;
};
