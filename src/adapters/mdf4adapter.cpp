#include "adapters/mdf4adapter.h"

#include "sessions/mdf4documentsession.h"

#pragma push_macro("signals")
#undef signals
#include "mdf4/reader.h"
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

// The file is opened and indexed once, with the reader's default limits; the
// reader observes `cancel` while it opens, and a cancelled opening yields a
// session whose one diagnostic says so. The session's tree, details, time axes
// and every scan use that one reader, which lives as long as any of its handles.
LoadResult Mdf4Adapter::load(const QString& path, const std::atomic<bool>& cancel) const {
    auto reader = std::make_shared<mdf4::Reader>(path.toStdString(), mdf4::Limits{}, &cancel);
    std::shared_ptr<const mdf4::File> metadata(reader, &reader->metadata());

    QList<DiagnosticMessage> diagnostics;
    for (const mdf4::Diagnostic& diagnostic : metadata->diagnostics()) {
        diagnostics.push_back(toDiagnostic(diagnostic));
    }

    auto session = std::make_unique<Mdf4DocumentSession>(
        QFileInfo(path).fileName(),
        path,
        std::move(metadata),
        [reader](std::uint32_t group, std::uint32_t channel, std::uint64_t first,
                 std::uint64_t count, const mdf4::Control& control,
                 const mdf4::Visitor& visitor) {
            return reader->scan(group, channel, first, count, control, visitor);
        },
        [reader](std::uint32_t group) { return reader->axis(group); },
        diagnostics);
    return LoadResult{std::move(session), diagnostics};
}
