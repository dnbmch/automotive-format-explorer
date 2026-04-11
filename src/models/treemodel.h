#pragma once

#include "core/treeitem.h"

#include <QAbstractItemModel>

#include <memory>

class TreeModel : public QAbstractItemModel {
    Q_OBJECT

public:
    enum Roles {
        TitleRole = Qt::UserRole + 1,
        SubtitleRole,
        IconKeyRole,
        SelectableRole,
        NodeKeyRole,
        SemanticKindRole
    };

    explicit TreeModel(QObject* parent = nullptr);

    QModelIndex index(int row, int column, const QModelIndex& parent = {}) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setRoot(std::unique_ptr<TreeItem> root);
    TreeItem* rootItem();
    const TreeItem* rootItem() const;

    Q_INVOKABLE QModelIndex indexForNodeKey(qulonglong nodeKey) const;

private:
    TreeItem* itemForIndex(const QModelIndex& index) const;
    int rowForItem(const TreeItem* item) const;

    std::unique_ptr<TreeItem> root_;
};
