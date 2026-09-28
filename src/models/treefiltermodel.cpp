#include "models/treefiltermodel.h"

TreeFilterModel::TreeFilterModel(QObject* parent)
    : QSortFilterProxyModel(parent) {
    setRecursiveFilteringEnabled(true);
    setAutoAcceptChildRows(true);
}

QString TreeFilterModel::filterText() const {
    return _filter_text;
}

void TreeFilterModel::setFilterText(const QString& text) {
    if (_filter_text == text) {
        return;
    }

    _filter_text = text;
    invalidate();
}

int TreeFilterModel::nodeKeyRole() const {
    return TreeModel::NodeKeyRole;
}

QModelIndex TreeFilterModel::indexForNodeKey(qulonglong nodeKey) const {
    auto* tree = static_cast<TreeModel*>(sourceModel());
    return mapFromSource(tree->indexForNodeKey(nodeKey));
}

bool TreeFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const {
    if (_filter_text.isEmpty()) {
        return true;
    }

    const QModelIndex idx = sourceModel()->index(sourceRow, 0, sourceParent);
    return idx.data(TreeModel::TitleRole).toString().contains(_filter_text, Qt::CaseInsensitive)
        || idx.data(TreeModel::SubtitleRole).toString().contains(_filter_text, Qt::CaseInsensitive);
}
