#include "adapters/mdf4adapter.h"

#include "sessions/mdf4documentsession.h"

#pragma push_macro("signals")
#undef signals
#include "mdf4/extract.h"
#pragma pop_macro("signals")

#include <QFileInfo>

namespace {

DiagnosticMessage toDiagnostic(const mdf4::Diagnostic& diagnostic) {
    DiagnosticMessage message;
    message.severity = diagnostic.severity() == mdf4::DROPPED
        ? DiagnosticSeverity::Error
        : DiagnosticSeverity::Warning;
    message.title = QString::fromStdString(diagnostic.message());
    if (!diagnostic.location().empty()) {
        message.detail = QStringLiteral("at %1")
                             .arg(QString::fromStdString(diagnostic.location()));
    }
    return message;
}

} // namespace

FormatId Mdf4Adapter::formatId() const {
    return FormatId::MDF4;
}

QString Mdf4Adapter::formatName() const {
    return QStringLiteral("MDF4");
}

QStringList Mdf4Adapter::extensions() const {
    return {QStringLiteral("mf4")};
}

LoadResult Mdf4Adapter::load(const QString& path) const {
    QList<DiagnosticMessage> diagnostics;
    mdf4::File document = mdf4::extract::extractFile(path.toStdString());
    for (const mdf4::Diagnostic& diagnostic : document.diagnostics()) {
        diagnostics.push_back(toDiagnostic(diagnostic));
    }

    auto session = std::make_unique<Mdf4DocumentSession>(
        QFileInfo(path).fileName(),
        path,
        std::move(document),
        diagnostics);
    return LoadResult{std::move(session), diagnostics};
}

extern "C" FormatAdapter* createMdf4AdapterPlugin() {
    return new Mdf4Adapter();
}
