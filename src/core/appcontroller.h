#pragma once

#include "core/formatlist.h"
#include "models/detailmodel.h"
#include "models/tabmodel.h"
#include "models/treefiltermodel.h"
#include "models/treemodel.h"

#include <QAbstractListModel>
#include <QFutureWatcher>
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <memory>
#include <unordered_map>
#include <QUrl>

class AppController : public QObject {
    Q_OBJECT
    Q_PROPERTY(TabModel* tabModel READ tabModel CONSTANT)
    Q_PROPERTY(TreeFilterModel* currentTreeModel READ currentTreeModel NOTIFY currentSessionChanged)
    Q_PROPERTY(DetailModel* currentDetailModel READ currentDetailModel NOTIFY currentSessionChanged)
    Q_PROPERTY(QUrl centerPanelSource READ centerPanelSource NOTIFY currentSessionChanged)
    Q_PROPERTY(QAbstractListModel* centerPanelModel READ centerPanelModel NOTIFY currentSessionChanged)
    Q_PROPERTY(int currentTabIndex READ currentTabIndex WRITE setCurrentTabIndex NOTIFY currentTabIndexChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(bool fileLoading READ fileLoading NOTIFY fileLoadingChanged)
    Q_PROPERTY(bool startupLoading READ startupLoading WRITE setStartupLoading NOTIFY startupLoadingChanged)
    Q_PROPERTY(QString startupStatusText READ startupStatusText WRITE setStartupStatusText NOTIFY startupStatusTextChanged)
    Q_PROPERTY(QVariantList sampleFiles READ sampleFiles CONSTANT)
    Q_PROPERTY(QStringList fileDialogFilters READ fileDialogFilters CONSTANT)

public:
    explicit AppController(FormatList formats, QObject* parent = nullptr);
    ~AppController() override;

    TabModel* tabModel();
    TreeFilterModel* currentTreeModel();
    DetailModel* currentDetailModel();
    QUrl centerPanelSource();
    QAbstractListModel* centerPanelModel();

    int currentTabIndex() const;
    void setCurrentTabIndex(int index);

    QString lastError() const;

    bool fileLoading() const;

    bool startupLoading() const;
    void setStartupLoading(bool loading);

    QString startupStatusText() const;
    void setStartupStatusText(const QString& text);

    QVariantList sampleFiles() const;
    QStringList fileDialogFilters() const;

    // Stops accepting opens and suppresses delivery of a pending load, then
    // waits for that load on this thread and destroys any result it produced.
    // The parse itself is not interruptible. Emits nothing; afterwards
    // fileLoading() is false. Idempotent, including from inside a notification;
    // the destructor calls it.
    void shutdown();

    Q_INVOKABLE void openFile(const QUrl& fileUrl);
    Q_INVOKABLE void closeTab(int index);
    Q_INVOKABLE void selectCurrentNode(qulonglong nodeKey);
    Q_INVOKABLE void clearLastError();

signals:
    void currentSessionChanged();
    void currentTabIndexChanged();
    void lastErrorChanged();
    void fileLoadingChanged();
    void startupLoadingChanged();
    void startupStatusTextChanged();
    void fileLoaded(const QString& displayName);

private:
    void setLastError(const QString& errorText);
    void onLoadFinished();
    void setFileLoading(bool loading);

    const FormatList _formats;
    TabModel _tab_model;
    TreeModel _empty_tree_model;
    TreeFilterModel _empty_tree_filter;
    DetailModel _empty_detail_model;
    std::unordered_map<DocumentSession*, std::unique_ptr<TreeFilterModel>> _tree_filters;
    int _current_tab_index = -1;
    QString _last_error;
    bool _file_loading = false;   // _load_watcher holds an owned, untaken result
    QFutureWatcher<LoadResult> _load_watcher;
    bool _shut_down = false;
    bool _startup_loading = true;
    QString _startup_status_text = QStringLiteral("Loading\u2026");
};
