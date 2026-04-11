#pragma once

#include "core/formatregistry.h"
#include "models/detailmodel.h"
#include "models/tabmodel.h"
#include "models/treemodel.h"

#include <QAbstractListModel>
#include <QFutureWatcher>
#include <QObject>
#include <QLibrary>
#include <memory>
#include <QUrl>

class AppController : public QObject {
    Q_OBJECT
    Q_PROPERTY(TabModel* tabModel READ tabModel CONSTANT)
    Q_PROPERTY(TreeModel* currentTreeModel READ currentTreeModel NOTIFY currentSessionChanged)
    Q_PROPERTY(DetailModel* currentDetailModel READ currentDetailModel NOTIFY currentSessionChanged)
    Q_PROPERTY(QUrl centerPanelSource READ centerPanelSource NOTIFY currentSessionChanged)
    Q_PROPERTY(QAbstractListModel* centerPanelModel READ centerPanelModel NOTIFY currentSessionChanged)
    Q_PROPERTY(int currentTabIndex READ currentTabIndex WRITE setCurrentTabIndex NOTIFY currentTabIndexChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(bool fileLoading READ fileLoading NOTIFY fileLoadingChanged)
    Q_PROPERTY(bool startupLoading READ startupLoading WRITE setStartupLoading NOTIFY startupLoadingChanged)
    Q_PROPERTY(QString startupStatusText READ startupStatusText WRITE setStartupStatusText NOTIFY startupStatusTextChanged)

public:
    explicit AppController(QObject* parent = nullptr);

    TabModel* tabModel();
    TreeModel* currentTreeModel();
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
    const FormatAdapter* ensureAdapterForPath(const QString& path);
    bool loadBackendForPath(const QString& path);
    bool loadBackend(FormatId formatId, const QString& libraryBaseName, const char* createSymbol);
    void setLastError(const QString& errorText);
    void onLoadFinished();
    void setFileLoading(bool loading);

    FormatRegistry formatRegistry_;
    TabModel tabModel_;
    TreeModel emptyTreeModel_;
    DetailModel emptyDetailModel_;
    std::vector<std::unique_ptr<QLibrary>> loadedBackends_;
    int currentTabIndex_ = -1;
    QString lastError_;
    bool fileLoading_ = false;
    QFutureWatcher<std::shared_ptr<LoadResult>> loadWatcher_;
    bool startupLoading_ = true;
    QString startupStatusText_ = QStringLiteral("Loading\u2026");
};
