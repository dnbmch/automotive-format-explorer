#pragma once

#include "core/diagnostics.h"

#include <QString>
#include <QList>

#include <memory>

class DocumentSession;

struct LoadResult {
    std::unique_ptr<DocumentSession> session;
    QList<DiagnosticMessage> diagnostics;
};

// Loads one file into an owning session. Format identity and the suffixes an
// adapter serves belong to the application's FormatList entry.
class FormatAdapter {
public:
    virtual ~FormatAdapter() = default;

    virtual LoadResult load(const QString& path) const = 0;
};
