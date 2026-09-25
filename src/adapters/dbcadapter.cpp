#include "adapters/dbcadapter.h"

#include "sessions/dbcdocumentsession.h"

#pragma push_macro("signals")
#undef signals
#include "dbc/dbcfile.h"
#pragma pop_macro("signals")

#include <QFileInfo>

namespace dbc::extract {
dbc::DbcFile extractFile(dbcfile::DbcFile* file);
}

namespace {
DiagnosticMessage toDiagnostic(const dbc::Diagnostic& d) {
    DiagnosticMessage message;
    message.severity = d.severity() == dbc::DROPPED ? DiagnosticSeverity::Error
                                                    : DiagnosticSeverity::Warning;
    message.title = QString::fromStdString(d.message());
    if (!d.location().empty()) {
        message.detail = QStringLiteral("at %1").arg(QString::fromStdString(d.location()));
    }
    return message;
}
} // namespace

LoadResult DbcAdapter::load(const QString& path) const {
    QList<DiagnosticMessage> diagnostics;

    auto raw = dbcfile::Loader::readDbcFile(path.toStdString());
    if (!raw) {
        diagnostics.push_back(DiagnosticMessage{
            DiagnosticSeverity::Error,
            QStringLiteral("Failed to load DBC"),
            QStringLiteral("The file could not be parsed: %1").arg(path),
        });
        return LoadResult{nullptr, diagnostics};
    }

    dbc::DbcFile document = dbc::extract::extractFile(raw.get());
    for (const dbc::Diagnostic& d : document.diagnostics()) {
        diagnostics.push_back(toDiagnostic(d));
    }

    auto session = std::make_unique<DbcDocumentSession>(
        QFileInfo(path).fileName(),
        path,
        std::move(document),
        diagnostics);

    return LoadResult{std::move(session), diagnostics};
}
