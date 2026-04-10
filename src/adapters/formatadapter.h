#pragma once

#include "core/diagnostics.h"
#include "core/formatid.h"

#include <QString>
#include <QStringList>
#include <QList>

#include <memory>

class DocumentSession;

struct LoadResult {
    std::unique_ptr<DocumentSession> session;
    QList<DiagnosticMessage> diagnostics;
};

class FormatAdapter {
public:
    virtual ~FormatAdapter() = default;

    virtual FormatId formatId() const = 0;
    virtual QString formatName() const = 0;
    virtual QStringList extensions() const = 0;
    virtual LoadResult load(const QString& path) const = 0;
};
