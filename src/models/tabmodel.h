#pragma once

#include "core/documenttab.h"

#include <QAbstractListModel>

#include <memory>
#include <vector>

class TabModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum Roles {
        TitleRole = Qt::UserRole + 1,
        FormatRole,
        SourcePathRole,
        HasDiagnosticsRole,
        DiagnosticsRole
    };

    explicit TabModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Appends the tab as the last row. It stays owned here.
    DocumentTab* addTab(std::unique_ptr<DocumentTab> tab);
    // Removes the row and hands its tab to the caller.
    std::unique_ptr<DocumentTab> takeTab(int index);
    DocumentTab* tabAt(int index) const;
    // The tab's row, or -1 when the model does not hold it.
    int indexOf(const DocumentTab* tab) const;

private:
    std::vector<std::unique_ptr<DocumentTab>> _tabs;
};
