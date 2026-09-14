#include "adapters/a2ladapter.h"

#include "sessions/a2ldocumentsession.h"

#pragma push_macro("signals")
#undef signals
#include "a2l/extract.h"
#include "a2l/a2lfile.h"
#pragma pop_macro("signals")

#include <QFileInfo>

namespace {
DiagnosticMessage toDiagnostic(const a2l::Diagnostic& d) {
    DiagnosticMessage message;
    message.severity = d.severity() == a2l::DROPPED ? DiagnosticSeverity::Error
                                                    : DiagnosticSeverity::Warning;
    message.title = QString::fromStdString(d.message());
    if (!d.location().empty()) {
        message.detail = QStringLiteral("at %1").arg(QString::fromStdString(d.location()));
    }
    return message;
}
} // namespace

FormatId A2lAdapter::formatId() const {
    return FormatId::A2L;
}

QString A2lAdapter::formatName() const {
    return QStringLiteral("A2L");
}

QStringList A2lAdapter::extensions() const {
    return {QStringLiteral("a2l")};
}

LoadResult A2lAdapter::load(const QString& path) const {
    QList<DiagnosticMessage> diagnostics;

    auto raw = a2lfile::Loader::readA2lFile(path.toStdString());
    if (!raw) {
        diagnostics.push_back(DiagnosticMessage{
            DiagnosticSeverity::Error,
            QStringLiteral("Failed to load A2L"),
            QStringLiteral("The file could not be parsed: %1").arg(path),
        });
        return LoadResult{nullptr, diagnostics};
    }

    a2l::A2lFile document = a2l::extract::extractFile(raw.get());
    for (const a2l::Diagnostic& d : document.diagnostics()) {
        diagnostics.push_back(toDiagnostic(d));
    }

    auto session = std::make_unique<A2lDocumentSession>(
        QFileInfo(path).fileName(),
        path,
        std::move(document),
        diagnostics);

    return LoadResult{std::move(session), diagnostics};
}

extern "C" FormatAdapter* createA2lAdapterPlugin() {
    return new A2lAdapter();
}
