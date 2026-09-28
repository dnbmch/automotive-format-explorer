#pragma once

#include "models/treefiltermodel.h"
#include "sessions/documentsession.h"

#include <QList>
#include <QObject>
#include <QVariantList>

#include <memory>
#include <optional>

// One open file: its session, the filter over the session's tree, and the tree
// navigation the nav panel last saved for it. QML reaches a tab only through
// AppController.currentTab, never as the result of an invokable, which would
// hand it to the JavaScript engine.
class DocumentTab : public QObject {
    Q_OBJECT
    Q_PROPERTY(TreeFilterModel* treeModel READ treeModel CONSTANT)
    Q_PROPERTY(QString filterText READ filterText NOTIFY filterTextChanged)
    Q_PROPERTY(QVariantList expandedKeys READ expandedKeys NOTIFY navigationChanged)
    Q_PROPERTY(qulonglong currentKey READ currentKey NOTIFY navigationChanged)
    Q_PROPERTY(qreal contentY READ contentY NOTIFY navigationChanged)

public:
    explicit DocumentTab(std::unique_ptr<DocumentSession> session);

    DocumentSession* session() const;
    TreeFilterModel* treeModel();
    QString filterText() const;
    // Keys of the expanded rows in row order, so parents precede children.
    QVariantList expandedKeys() const;
    qulonglong currentKey() const;
    qreal contentY() const;

    // Replaces the navigation with the view the nav panel shows for this tab.
    Q_INVOKABLE void saveNavigation(const QVariantList& expandedKeys, qulonglong currentKey,
                                    qreal contentY);
    // Entering a filter keeps the navigation as the pre-filter snapshot; changing
    // a filter keeps that snapshot; clearing the filter makes it the navigation.
    Q_INVOKABLE void setFilterText(const QString& text);

signals:
    void filterTextChanged();
    void navigationChanged();

private:
    struct Navigation {
        QList<quint64> expandedKeys;
        quint64 currentKey = 0;
        qreal contentY = 0;
    };

    std::unique_ptr<DocumentSession> _session;
    TreeFilterModel _tree_filter;   // declared after the session: destroyed first
    Navigation _navigation;
    std::optional<Navigation> _pre_filter;   // held exactly while the filter is non-empty
};
