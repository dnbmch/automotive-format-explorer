#pragma once

#include "core/detailsection.h"

#include <QAbstractListModel>

class DetailModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum Roles {
        TitleRole = Qt::UserRole + 1,
        FieldsRole
    };

    explicit DetailModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setSections(QList<DetailSection> sections);

private:
    QList<DetailSection> sections_;
};
