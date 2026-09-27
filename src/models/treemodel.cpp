#include "models/treemodel.h"

TreeModel::TreeModel(QObject* parent)
    : QAbstractItemModel(parent),
      _root(std::make_unique<TreeItem>()) {
}

QModelIndex TreeModel::index(int row, int column, const QModelIndex& parent) const {
    if (column != 0 || row < 0) {
        return {};
    }

    TreeItem* parentItem = itemForIndex(parent);
    if (!parentItem) {
        return {};
    }

    if (row >= static_cast<int>(parentItem->children.size())) {
        return {};
    }

    return createIndex(row, column, parentItem->children[static_cast<std::size_t>(row)].get());
}

QModelIndex TreeModel::parent(const QModelIndex& child) const {
    if (!child.isValid()) {
        return {};
    }

    TreeItem* childItem = itemForIndex(child);
    if (!childItem || !childItem->parent || childItem->parent == _root.get()) {
        return {};
    }

    TreeItem* parentItem = childItem->parent;
    return createIndex(parentItem->row, 0, parentItem);
}

int TreeModel::rowCount(const QModelIndex& parent) const {
    TreeItem* parentItem = itemForIndex(parent);
    if (!parentItem) {
        return 0;
    }

    return static_cast<int>(parentItem->children.size());
}

int TreeModel::columnCount(const QModelIndex& parent) const {
    Q_UNUSED(parent);
    return 1;
}

QVariant TreeModel::data(const QModelIndex& index, int role) const {
    TreeItem* item = itemForIndex(index);
    if (!item) {
        return {};
    }

    switch (role) {
    case Qt::DisplayRole:
    case TitleRole:
        return item->title;
    case SubtitleRole:
        return item->subtitle;
    case IconKeyRole:
        return item->iconKey;
    case SelectableRole:
        return item->selectable;
    case NodeKeyRole:
        return QVariant::fromValue<qulonglong>(item->nodeKey);
    case SemanticKindRole:
        return static_cast<int>(item->semanticKind);
    default:
        return {};
    }
}

QHash<int, QByteArray> TreeModel::roleNames() const {
    return {
        {TitleRole, "title"},
        {SubtitleRole, "subtitle"},
        {IconKeyRole, "iconKey"},
        {SelectableRole, "selectable"},
        {NodeKeyRole, "nodeKey"},
        {SemanticKindRole, "semanticKind"},
    };
}

void TreeModel::setRoot(std::unique_ptr<TreeItem> root) {
    beginResetModel();
    _root = std::move(root);
    _items_by_key.clear();
    fileItems(_root.get());
    endResetModel();
}

QModelIndex TreeModel::indexForNodeKey(qulonglong nodeKey) const {
    if (nodeKey == 0 || nodeKey >= _items_by_key.size() || !_items_by_key[nodeKey]) {
        return {};
    }

    TreeItem* item = _items_by_key[nodeKey];
    return createIndex(item->row, 0, item);
}

TreeItem* TreeModel::itemForIndex(const QModelIndex& index) const {
    if (!index.isValid()) {
        return _root.get();
    }

    return static_cast<TreeItem*>(index.internalPointer());
}

// Records every row's position under its parent and files it by key, once per
// installed tree, so no lookup walks the tree.
void TreeModel::fileItems(TreeItem* parent) {
    for (std::size_t i = 0; i < parent->children.size(); ++i) {
        TreeItem* child = parent->children[i].get();
        child->row = static_cast<int>(i);
        if (child->nodeKey >= _items_by_key.size()) {
            _items_by_key.resize(child->nodeKey + 1, nullptr);
        }
        _items_by_key[child->nodeKey] = child;
        fileItems(child);
    }
}
