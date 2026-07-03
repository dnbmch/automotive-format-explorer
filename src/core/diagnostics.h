#pragma once

#include <QString>
#include <QList>

enum class DiagnosticSeverity {
    Warning,
    Error
};

struct DiagnosticMessage {
    DiagnosticSeverity severity = DiagnosticSeverity::Warning;
    QString title;
    QString detail;
};

inline bool hasWarnings(const QList<DiagnosticMessage>& diagnostics) {
    for (const DiagnosticMessage& message : diagnostics) {
        if (message.severity == DiagnosticSeverity::Warning) {
            return true;
        }
    }
    return false;
}
