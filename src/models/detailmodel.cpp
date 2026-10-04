#include "models/detailmodel.h"

#include <QPointer>
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
//
// Observers run synchronously and may select again or destroy the model. The
// selection waits in the model while the pre-reset observers run: one that
// selects again replaces it, and the one reset under way installs the newest,
// so no reset nests in another. Nothing is touched or announced after an
// observer destroyed the model.
void DetailModel::setSelection(QList<DetailSection> sections, std::function<QString()> rawJson) {
    const bool resetUnderWay = _arriving.has_value();
    _arriving = Selection{std::move(sections), std::move(rawJson)};
    if (resetUnderWay) {
        return;
    }
    QPointer<DetailModel> self(this);
    beginResetModel();
    if (!self) {
        return;
    }
    _sections = std::move(_arriving->sections);
    _raw_json = std::move(_arriving->rawJson);
    _arriving.reset();
    _raw_json_text.reset();
    endResetModel();
    if (self) {
        emit rawJsonChanged();
    }
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
