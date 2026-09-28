#include "core/documenttab.h"

#include "models/treemodel.h"

DocumentTab::DocumentTab(std::unique_ptr<DocumentSession> session)
    : _session(std::move(session)) {
    _tree_filter.setSourceModel(_session->treeModel());
}

DocumentSession* DocumentTab::session() const {
    return _session.get();
}

TreeFilterModel* DocumentTab::treeModel() {
    return &_tree_filter;
}

QString DocumentTab::filterText() const {
    return _tree_filter.filterText();
}

QVariantList DocumentTab::expandedKeys() const {
    QVariantList keys;
    keys.reserve(_navigation.expandedKeys.size());
    for (const quint64 key : _navigation.expandedKeys) {
        keys << QVariant::fromValue<qulonglong>(key);
    }
    return keys;
}

qulonglong DocumentTab::currentKey() const {
    return _navigation.currentKey;
}

qreal DocumentTab::contentY() const {
    return _navigation.contentY;
}

void DocumentTab::saveNavigation(const QVariantList& expandedKeys, qulonglong currentKey,
                                 qreal contentY) {
    Navigation navigation;
    navigation.expandedKeys.reserve(expandedKeys.size());
    for (const QVariant& key : expandedKeys) {
        navigation.expandedKeys << key.toULongLong();
    }
    navigation.currentKey = currentKey;
    navigation.contentY = contentY;
    _navigation = std::move(navigation);
    emit navigationChanged();
}

void DocumentTab::setFilterText(const QString& text) {
    const bool entering = _tree_filter.filterText().isEmpty();
    if (_tree_filter.filterText() == text) {
        return;
    }

    const bool clearing = text.isEmpty();
    if (entering) {
        _pre_filter = _navigation;
    } else if (clearing) {
        _navigation = *_pre_filter;
        _pre_filter.reset();
    }
    _tree_filter.setFilterText(text);
    emit filterTextChanged();
    if (clearing) {
        emit navigationChanged();
    }
}
