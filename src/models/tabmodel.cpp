#include "models/tabmodel.h"

TabModel::TabModel(QObject* parent)
    : QAbstractListModel(parent) {
}

int TabModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }

    return static_cast<int>(sessions_.size());
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
    case HasWarningsRole:
        return session->hasWarnings();
    default:
        return {};
    }
}

QHash<int, QByteArray> TabModel::roleNames() const {
    return {
        {TitleRole, "title"},
        {FormatRole, "formatName"},
        {SourcePathRole, "sourcePath"},
        {HasWarningsRole, "hasWarnings"},
    };
}

int TabModel::addSession(std::unique_ptr<DocumentSession> session) {
    const int row = static_cast<int>(sessions_.size());
    beginInsertRows({}, row, row);
    sessions_.push_back(std::move(session));
    endInsertRows();
    return row;
}

void TabModel::closeSession(int index) {
    if (index < 0 || index >= static_cast<int>(sessions_.size())) {
        return;
    }

    beginRemoveRows({}, index, index);
    sessions_.erase(sessions_.begin() + index);
    endRemoveRows();
}

DocumentSession* TabModel::sessionAt(int index) {
    if (index < 0 || index >= static_cast<int>(sessions_.size())) {
        return nullptr;
    }

    return sessions_[static_cast<std::size_t>(index)].get();
}

const DocumentSession* TabModel::sessionAt(int index) const {
    if (index < 0 || index >= static_cast<int>(sessions_.size())) {
        return nullptr;
    }

    return sessions_[static_cast<std::size_t>(index)].get();
}
