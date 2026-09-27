#pragma once

#include "models/treemodel.h"

#include <QSortFilterProxyModel>

// A tab's filter proxy over its session's TreeModel. Matches title or subtitle
// case-insensitively; ancestors of a match stay visible (recursive filtering)
// and descendants of a match are auto-accepted so filtering to a message
// keeps its signals in view. The owning DocumentTab sets the filter text.
class TreeFilterModel : public QSortFilterProxyModel {
    Q_OBJECT
    Q_PROPERTY(int nodeKeyRole READ nodeKeyRole CONSTANT)

public:
    explicit TreeFilterModel(QObject* parent = nullptr);

    QString filterText() const;
    void setFilterText(const QString& text);

    int nodeKeyRole() const;

    Q_INVOKABLE QModelIndex indexForNodeKey(qulonglong nodeKey) const;

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

private:
    QString _filter_text;
};
