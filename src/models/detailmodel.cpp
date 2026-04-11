#include "models/detailmodel.h"

#include <QVariantList>
#include <QVariantMap>

DetailModel::DetailModel(QObject* parent)
    : QAbstractListModel(parent) {
}

int DetailModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }

    return sections_.size();
}

QVariant DetailModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= sections_.size()) {
        return {};
    }

    const DetailSection& section = sections_.at(index.row());
    switch (role) {
    case TitleRole:
    case Qt::DisplayRole:
        return section.title;
    case FieldsRole: {
        QVariantList fields;
        for (const DetailField& field : section.fields) {
            QVariantMap item;
            item.insert(QStringLiteral("key"), field.key);
            item.insert(QStringLiteral("value"), field.value);
            fields.push_back(item);
        }
        return fields;
    }
    default:
        return {};
    }
}

QHash<int, QByteArray> DetailModel::roleNames() const {
    return {
        {TitleRole, "title"},
        {FieldsRole, "fields"},
    };
}

void DetailModel::setSections(QList<DetailSection> sections) {
    beginResetModel();
    sections_ = std::move(sections);
    endResetModel();
}

QString DetailModel::rawJsonText() const {
    return rawJsonText_;
}

void DetailModel::setRawJsonText(const QString& json) {
    if (rawJsonText_ == json) {
        return;
    }
    rawJsonText_ = json;
    emit rawJsonTextChanged();
}
