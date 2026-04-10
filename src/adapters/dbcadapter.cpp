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

FormatId DbcAdapter::formatId() const {
    return FormatId::DBC;
}

QString DbcAdapter::formatName() const {
    return QStringLiteral("DBC");
}

QStringList DbcAdapter::extensions() const {
    return {QStringLiteral("dbc")};
}

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
    auto session = std::make_unique<DbcDocumentSession>(
        QFileInfo(path).fileName(),
        path,
        std::move(document),
        diagnostics);

    return LoadResult{std::move(session), diagnostics};
}

extern "C" FormatAdapter* createDbcAdapterPlugin() {
    return new DbcAdapter();
}
