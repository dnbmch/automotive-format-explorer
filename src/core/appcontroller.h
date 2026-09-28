#pragma once

#include "core/documenttab.h"
#include "core/formatlist.h"
#include "models/detailmodel.h"
#include "models/tabmodel.h"

#include <QAbstractListModel>
#include <QFutureWatcher>
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <memory>
#include <QUrl>

class AppController : public QObject {
    Q_OBJECT
    Q_PROPERTY(TabModel* tabModel READ tabModel CONSTANT)
    Q_PROPERTY(DocumentTab* currentTab READ currentTab NOTIFY currentSessionChanged)
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
    DocumentTab* currentTab() const;
    DetailModel* currentDetailModel();
    QUrl centerPanelSource();
    QAbstractListModel* centerPanelModel();

    // The current tab's row, or -1 when no tab is current.
    int currentTabIndex() const;
    // Makes the tab at `index` current; -1 makes no tab current.
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
    // fileLoading() is false and every public action does nothing. Idempotent,
    // including from inside a notification; the destructor calls it.
    void shutdown();

    Q_INVOKABLE void openFile(const QUrl& fileUrl);
    // Closes the tab at `index`. A current tab hands over to its successor,
    // else its predecessor; the closed tab is destroyed after the notifications.
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
    void makeCurrent(DocumentTab* tab);
    void announceCurrentTab();
    DocumentTab* insertTab(std::unique_ptr<DocumentTab> tab);
    std::unique_ptr<DocumentTab> removeTab(int index);
    void deferSwitch(DocumentTab* tab);
    void deferClose(DocumentTab* tab);

    const FormatList _formats;
    TabModel _tab_model;
    DetailModel _empty_detail_model;
    DocumentTab* _current_tab = nullptr;
    // What observers were last told: the tab and row behind
    // currentTabIndexChanged, and the tab behind currentSessionChanged. Read
    // only while not shut down.
    const DocumentTab* _announced_row_tab = nullptr;
    int _announced_row = -1;
    const DocumentTab* _announced_tab = nullptr;
    // True while the tab model inserts or removes a row.
    bool _in_row_change = false;
    QString _last_error;
    bool _file_loading = false;   // _load_watcher holds an owned, untaken result
    QFutureWatcher<LoadResult> _load_watcher;
    bool _shut_down = false;
    bool _startup_loading = true;
    QString _startup_status_text = QStringLiteral("Loading…");
};
