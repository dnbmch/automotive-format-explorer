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
    _root = std::move(root);
    if (!_root) {
        _root = std::make_unique<TreeItem>();
    }
    endResetModel();
}

TreeItem* TreeModel::rootItem() {
    return _root.get();
}

const TreeItem* TreeModel::rootItem() const {
    return _root.get();
}

QModelIndex TreeModel::indexForNodeKey(qulonglong nodeKey) const {
    if (nodeKey == 0 || !_root) {
        return {};
    }

    // BFS to find the node with matching key.
    struct Frame { TreeItem* item; QModelIndex parentIdx; };
    std::vector<Frame> stack;
    stack.push_back({_root.get(), {}});

    while (!stack.empty()) {
        auto [item, parentIdx] = stack.back();
        stack.pop_back();

        for (int i = 0; i < static_cast<int>(item->children.size()); ++i) {
            TreeItem* child = item->children[static_cast<size_t>(i)].get();
            QModelIndex childIdx = index(i, 0, parentIdx);
            if (child->nodeKey == nodeKey) {
                return childIdx;
            }
            if (!child->children.empty()) {
                stack.push_back({child, childIdx});
            }
        }
    }

    return {};
}

TreeItem* TreeModel::itemForIndex(const QModelIndex& index) const {
    if (!index.isValid()) {
        return _root.get();
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
