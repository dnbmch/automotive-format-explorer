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

    return _sections.size();
}

QVariant DetailModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= _sections.size()) {
        return {};
    }

    const DetailSection& section = _sections.at(index.row());
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

// The cards, the producer and the cleared text change together inside the
// reset, so an observer of the reset already reads the new selection.
void DetailModel::setSelection(QList<DetailSection> sections, std::function<QString()> rawJson) {
    beginResetModel();
    _sections = std::move(sections);
    _raw_json = std::move(rawJson);
    _raw_json_text.reset();
    endResetModel();
    emit rawJsonChanged();
}

bool DetailModel::rawJsonAvailable() const {
    return static_cast<bool>(_raw_json);
}

QString DetailModel::rawJsonText() const {
    if (!_raw_json) {
        return {};
    }
    if (!_raw_json_text) {
        _raw_json_text = _raw_json();
    }
    return *_raw_json_text;
}
