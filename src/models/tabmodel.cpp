#include "models/tabmodel.h"

#include "core/diagnostics.h"

#include <QVariantList>
#include <QVariantMap>

TabModel::TabModel(QObject* parent)
    : QAbstractListModel(parent) {
}

int TabModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }

    return static_cast<int>(_sessions.size());
}

QVariant TabModel::data(const QModelIndex& index, int role) const {
    const DocumentSession* session = sessionAt(index.row());
    if (!session) {
        return {};
    }

    switch (role) {
    case TitleRole:
    case Qt::DisplayRole:
        return session->displayName();
    case FormatRole:
        return session->formatName();
    case SourcePathRole:
        return session->sourcePath();
    case HasDiagnosticsRole:
        return session->hasDiagnostics();
    case DiagnosticsRole: {
        QVariantList rows;
        const QList<DiagnosticMessage> diagnostics = session->diagnostics();
        rows.reserve(diagnostics.size());
        for (const DiagnosticMessage& message : diagnostics) {
            rows.append(QVariantMap{
                {QStringLiteral("severity"),
                 message.severity == DiagnosticSeverity::Error ? QStringLiteral("Error")
                                                               : QStringLiteral("Warning")},
                {QStringLiteral("title"), message.title},
                {QStringLiteral("detail"), message.detail},
            });
        }
        return rows;
    }
    default:
        return {};
    }
}

QHash<int, QByteArray> TabModel::roleNames() const {
    return {
        {TitleRole, "title"},
        {FormatRole, "formatName"},
        {SourcePathRole, "sourcePath"},
        {HasDiagnosticsRole, "hasDiagnostics"},
        {DiagnosticsRole, "diagnostics"},
    };
}

int TabModel::addSession(std::unique_ptr<DocumentSession> session) {
    const int row = static_cast<int>(_sessions.size());
    beginInsertRows({}, row, row);
    _sessions.push_back(std::move(session));
    endInsertRows();
    return row;
}

void TabModel::closeSession(int index) {
    if (index < 0 || index >= static_cast<int>(_sessions.size())) {
        return;
    }

    beginRemoveRows({}, index, index);
    _sessions.erase(_sessions.begin() + index);
    endRemoveRows();
}

DocumentSession* TabModel::sessionAt(int index) {
    if (index < 0 || index >= static_cast<int>(_sessions.size())) {
        return nullptr;
    }

    return _sessions[static_cast<std::size_t>(index)].get();
}

const DocumentSession* TabModel::sessionAt(int index) const {
    if (index < 0 || index >= static_cast<int>(_sessions.size())) {
        return nullptr;
    }

    return _sessions[static_cast<std::size_t>(index)].get();
}
