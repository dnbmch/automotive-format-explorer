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
