#include "models/treemodel.h"

TreeModel::TreeModel(QObject* parent)
    : QAbstractItemModel(parent),
      root_(std::make_unique<TreeItem>()) {
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
    if (!childItem || !childItem->parent || childItem->parent == root_.get()) {
        return {};
    }

    TreeItem* parentItem = childItem->parent;
    return createIndex(rowForItem(parentItem), 0, parentItem);
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
    root_ = std::move(root);
    if (!root_) {
        root_ = std::make_unique<TreeItem>();
    }
    endResetModel();
}

TreeItem* TreeModel::rootItem() {
    return root_.get();
}

const TreeItem* TreeModel::rootItem() const {
    return root_.get();
}

TreeItem* TreeModel::itemForIndex(const QModelIndex& index) const {
    if (!index.isValid()) {
        return root_.get();
    }

    return static_cast<TreeItem*>(index.internalPointer());
}

int TreeModel::rowForItem(const TreeItem* item) const {
    if (!item || !item->parent) {
        return 0;
    }

    const auto& siblings = item->parent->children;
    for (std::size_t i = 0; i < siblings.size(); ++i) {
        if (siblings[i].get() == item) {
            return static_cast<int>(i);
        }
    }

    return 0;
}
