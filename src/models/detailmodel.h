#pragma once

#include "core/detailsection.h"

#include <QAbstractListModel>

#include <functional>
#include <optional>

class DetailModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(bool rawJsonAvailable READ rawJsonAvailable NOTIFY rawJsonChanged)
    Q_PROPERTY(QString rawJsonText READ rawJsonText NOTIFY rawJsonChanged)

public:
    enum Roles {
        TitleRole = Qt::UserRole + 1,
        FieldsRole
    };

    explicit DetailModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Shows one selection: its cards and, when the entity has a raw form, a
    // producer of its raw JSON. The producer runs on this thread at the first
    // read of rawJsonText after the selection; its text, even an empty one, is
    // kept until the next selection. An observer of any notification this makes
    // may call it again or destroy the model; the newest selection is shown.
    void setSelection(QList<DetailSection> sections, std::function<QString()> rawJson);

    // Whether the selection has a raw form; answering serializes nothing.
    bool rawJsonAvailable() const;
    QString rawJsonText() const;

signals:
    void rawJsonChanged();

private:
    struct Selection {
        QList<DetailSection> sections;
        std::function<QString()> rawJson;
    };

    QList<DetailSection> _sections;
    std::function<QString()> _raw_json;
    mutable std::optional<QString> _raw_json_text;
    // The selection a reset is about to install, while its pre-reset observers run.
    std::optional<Selection> _arriving;
};
