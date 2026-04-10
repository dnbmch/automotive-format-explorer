#pragma once

#include <QString>
#include <QList>

enum class DiagnosticSeverity {
    Info,
    Warning,
    Error
};

struct DiagnosticMessage {
    DiagnosticSeverity severity = DiagnosticSeverity::Info;
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

inline bool hasErrors(const QList<DiagnosticMessage>& diagnostics) {
    for (const DiagnosticMessage& message : diagnostics) {
        if (message.severity == DiagnosticSeverity::Error) {
            return true;
        }
    }
    return false;
}
