#include "models/tabmodel.h"

#include "core/diagnostics.h"

#include <QVariantList>
#include <QVariantMap>

#include <algorithm>

TabModel::TabModel(QObject* parent)
    : QAbstractListModel(parent) {
}

int TabModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }

    return static_cast<int>(_tabs.size());
}

QVariant TabModel::data(const QModelIndex& index, int role) const {
    const DocumentTab* tab = tabAt(index.row());
    if (!tab) {
        return {};
    }
    const DocumentSession* session = tab->session();

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

DocumentTab* TabModel::addTab(std::unique_ptr<DocumentTab> tab) {
    DocumentTab* added = tab.get();
    const int row = static_cast<int>(_tabs.size());
    beginInsertRows({}, row, row);
    _tabs.push_back(std::move(tab));
    endInsertRows();
    return added;
}

std::unique_ptr<DocumentTab> TabModel::takeTab(int index) {
    beginRemoveRows({}, index, index);
    std::unique_ptr<DocumentTab> tab = std::move(_tabs[static_cast<std::size_t>(index)]);
    _tabs.erase(_tabs.begin() + index);
    endRemoveRows();
    return tab;
}

DocumentTab* TabModel::tabAt(int index) const {
    if (index < 0 || index >= static_cast<int>(_tabs.size())) {
        return nullptr;
    }

    return _tabs[static_cast<std::size_t>(index)].get();
}

int TabModel::indexOf(const DocumentTab* tab) const {
    const auto it = std::find_if(_tabs.begin(), _tabs.end(),
                                 [tab](const std::unique_ptr<DocumentTab>& held) {
                                     return held.get() == tab;
                                 });
    return !tab || it == _tabs.end() ? -1 : static_cast<int>(it - _tabs.begin());
}
