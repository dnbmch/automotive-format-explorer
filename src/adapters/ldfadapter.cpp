#include "adapters/ldfadapter.h"

#include "sessions/ldfdocumentsession.h"

#pragma push_macro("signals")
#undef signals
#include "ldf/extract.h"
#pragma pop_macro("signals")

#include <QFileInfo>

FormatId LdfAdapter::formatId() const {
    return FormatId::LDF;
}

QString LdfAdapter::formatName() const {
    return QStringLiteral("LDF");
}

QStringList LdfAdapter::extensions() const {
    return {QStringLiteral("ldf")};
}

LoadResult LdfAdapter::load(const QString& path) const {
    QList<DiagnosticMessage> diagnostics;

    auto raw = ldffile::Loader::readLdfFile(path.toStdString());
    if (!raw) {
        diagnostics.push_back(DiagnosticMessage{
            DiagnosticSeverity::Error,
            QStringLiteral("Failed to load LDF"),
            QStringLiteral("The file could not be parsed: %1").arg(path),
        });
        return LoadResult{nullptr, diagnostics};
    }

    ldf::LdfFile document = ldf::extract::extractFile(raw.get());
    auto session = std::make_unique<LdfDocumentSession>(
        QFileInfo(path).fileName(),
        path,
        std::move(document),
        diagnostics);

    return LoadResult{std::move(session), diagnostics};
}

extern "C" FormatAdapter* createLdfAdapterPlugin() {
    return new LdfAdapter();
}
