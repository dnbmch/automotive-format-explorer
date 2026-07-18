#pragma once

#include "models/treemodel.h"

#include <QSortFilterProxyModel>

// Per-session filter proxy over a TreeModel. Matches title or subtitle
// case-insensitively; ancestors of a match stay visible (recursive filtering)
// and descendants of a match are auto-accepted so filtering to a message
// keeps its signals in view.
class TreeFilterModel : public QSortFilterProxyModel {
    Q_OBJECT
    Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filterTextChanged)
    Q_PROPERTY(int nodeKeyRole READ nodeKeyRole CONSTANT)

public:
    explicit TreeFilterModel(QObject* parent = nullptr);

    QString filterText() const;
    void setFilterText(const QString& text);

    int nodeKeyRole() const;

    Q_INVOKABLE QModelIndex indexForNodeKey(qulonglong nodeKey) const;

signals:
    void filterTextChanged();

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

private:
    QString _filter_text;
};
