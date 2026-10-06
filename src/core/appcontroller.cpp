#include "core/appcontroller.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QPointer>
#include <QtConcurrent/QtConcurrentRun>

AppController::AppController(FormatList formats, QObject* parent)
    : QObject(parent),
      _formats(std::move(formats)) {
    connect(&_load_watcher, &QFutureWatcher<LoadResult>::finished,
            this, &AppController::onLoadFinished);
}

AppController::~AppController() {
    shutdown();
}

TabModel* AppController::tabModel() {
    return &_tab_model;
}

DocumentTab* AppController::currentTab() const {
    return _current_tab;
}

DetailModel* AppController::currentDetailModel() {
    return _current_tab ? _current_tab->session()->detailModel() : &_empty_detail_model;
}

QUrl AppController::centerPanelSource() {
    return _current_tab ? _current_tab->session()->centerPanelSource() : QUrl();
}

QAbstractListModel* AppController::centerPanelModel() {
    return _current_tab ? _current_tab->session()->centerPanelModel() : nullptr;
}

int AppController::currentTabIndex() const {
    return _tab_model.indexOf(_current_tab);
}

void AppController::setCurrentTabIndex(int index) {
    if (_shut_down || index < -1 || index >= _tab_model.rowCount()) {
        return;
    }

    DocumentTab* tab = _tab_model.tabAt(index);
    if (_in_row_change) {
        deferSwitch(tab);
        return;
    }
    makeCurrent(tab);
}

void AppController::makeCurrent(DocumentTab* tab) {
    _current_tab = tab;
    announceCurrentTab();
}

// Tells observers about the current tab as it stands: its row
// (currentTabIndexChanged), then the tab itself (currentSessionChanged). Each is
// sent only when it differs from what observers were last told, and never after
// shutdown. An observer may switch, close or open tabs from either notification;
// a nested change announces itself, and nothing stale follows it.
void AppController::announceCurrentTab() {
    if (_shut_down) {
        return;
    }
    if (_current_tab != _announced_row_tab || currentTabIndex() != _announced_row) {
        _announced_row_tab = _current_tab;
        _announced_row = currentTabIndex();
        emit currentTabIndexChanged();
        if (_shut_down) {
            return;
        }
    }
    if (_current_tab != _announced_tab) {
        _announced_tab = _current_tab;
        emit currentSessionChanged();
    }
}

// The tab model's row insertion and removal notify observers synchronously.
// Meanwhile tab switches and closes are deferred: another row change inside
// this one would break the model's transaction.
DocumentTab* AppController::insertTab(std::unique_ptr<DocumentTab> tab) {
    _in_row_change = true;
    DocumentTab* inserted = _tab_model.addTab(std::move(tab));
    _in_row_change = false;
    return inserted;
}

std::unique_ptr<DocumentTab> AppController::removeTab(int index) {
    _in_row_change = true;
    std::unique_ptr<DocumentTab> removed = _tab_model.takeTab(index);
    _in_row_change = false;
    return removed;
}

// A deferred request names its tab, not a row, and is revalidated when the event
// loop delivers it: it does nothing once the tab has left the model or the
// controller has shut down. Making no tab current is a request of its own.
void AppController::deferSwitch(DocumentTab* tab) {
    const bool none = tab == nullptr;
    QMetaObject::invokeMethod(this, [this, none, target = QPointer<DocumentTab>(tab)] {
        if (none) {
            setCurrentTabIndex(-1);
        } else if (target && _tab_model.indexOf(target) >= 0) {
            setCurrentTabIndex(_tab_model.indexOf(target));
        }
    }, Qt::QueuedConnection);
}

void AppController::deferClose(DocumentTab* tab) {
    QMetaObject::invokeMethod(this, [this, target = QPointer<DocumentTab>(tab)] {
        if (target) {
            closeTab(_tab_model.indexOf(target));
        }
    }, Qt::QueuedConnection);
}

QString AppController::lastError() const {
    return _last_error;
}

// Notifications call observers synchronously, and an observer may shut the
// controller down or open another file. The pending load is therefore owned
// before it is announced and released before completion is announced, and
// state is re-read after every notification.
void AppController::openFile(const QUrl& fileUrl) {
    if (_shut_down) {
        return;
    }

    clearLastError();
    if (_shut_down) {
        return;
    }

    if (_file_loading) {
        setLastError(QStringLiteral("Another file is already loading."));
        return;
    }

    const QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
    if (path.isEmpty()) {
        setLastError(QStringLiteral("No file was selected."));
        return;
    }

    const FormatEntry* format = formatForPath(_formats, path);
    if (!format) {
        setLastError(QStringLiteral("Unsupported file type: %1").arg(QFileInfo(path).fileName()));
        return;
    }

    // The adapter stays owned by _formats and the cancellation flag by this
    // controller; shutdown() joins this task before the controller's members
    // are destroyed. The session's models are handed to this thread before the
    // result is published.
    const FormatAdapter* adapter = format->adapter.get();
    const std::atomic<bool>* cancel = &_load_cancel;
    QThread* owner = thread();
    _load_watcher.setFuture(QtConcurrent::run([adapter, path, cancel, owner]() {
        LoadResult result = adapter->load(path, *cancel);
        if (result.session) {
            result.session->moveModelsToThread(owner);
        }
        return result;
    }));
    setFileLoading(true);
}

void AppController::onLoadFinished() {
    LoadResult result = _load_watcher.future().takeResult();
    setFileLoading(false);
    if (_shut_down) {
        return;
    }

    if (!result.session) {
        setLastError(result.diagnostics.isEmpty() ? QStringLiteral("Failed to load file.")
                                                  : result.diagnostics.first().detail);
        return;
    }

    const QString name = result.session->displayName();
    // Closes requested while the row is inserted are deferred, so the new tab is
    // still in the model here and is selected by identity, not by its row.
    DocumentTab* tab = insertTab(std::make_unique<DocumentTab>(std::move(result.session)));
    if (!_shut_down) {
        makeCurrent(tab);
    }
    if (!_shut_down) {
        emit fileLoaded(name);
    }
}

void AppController::shutdown() {
    if (_shut_down) {
        return;
    }
    _shut_down = true;

    // No completion reaches the UI after this point. A load the controller
    // still owns is asked to stop and waited for here; its worker never needs
    // this thread's event loop. The result, finished or still queued for
    // delivery, is destroyed on this thread, which owns its models. Shutdown
    // notifies no one.
    disconnect(&_load_watcher, nullptr, this, nullptr);
    if (!_file_loading) {
        return;
    }
    QFuture<LoadResult> pending = _load_watcher.future();
    _load_cancel.store(true);
    pending.waitForFinished();
    const LoadResult undelivered = pending.takeResult();
    _file_loading = false;
}

bool AppController::fileLoading() const {
    return _file_loading;
}

void AppController::setFileLoading(bool loading) {
    if (_file_loading == loading) return;
    _file_loading = loading;
    emit fileLoadingChanged();
}

// The current tab is settled before the row leaves, so the row notifications
// see it. The closed tab stays alive through every notification and is destroyed
// on this thread when they have returned.
void AppController::closeTab(int index) {
    DocumentTab* closing = _shut_down ? nullptr : _tab_model.tabAt(index);
    if (!closing) {
        return;
    }
    if (_in_row_change) {
        deferClose(closing);
        return;
    }

    if (closing == _current_tab) {
        DocumentTab* successor = _tab_model.tabAt(index + 1);
        _current_tab = successor ? successor : _tab_model.tabAt(index - 1);
    }
    const std::unique_ptr<DocumentTab> closed = removeTab(index);
    announceCurrentTab();
}

void AppController::selectCurrentNode(qulonglong nodeKey) {
    if (_shut_down || !_current_tab) {
        return;
    }

    _current_tab->session()->selectNode(static_cast<quint64>(nodeKey));
}

void AppController::clearLastError() {
    if (_shut_down || _last_error.isEmpty()) {
        return;
    }

    _last_error.clear();
    emit lastErrorChanged();
}

bool AppController::startupLoading() const {
    return _startup_loading;
}

void AppController::setStartupLoading(bool loading) {
    if (_shut_down || _startup_loading == loading) {
        return;
    }

    _startup_loading = loading;
    emit startupLoadingChanged();
}

QString AppController::startupStatusText() const {
    return _startup_status_text;
}

void AppController::setStartupStatusText(const QString& text) {
    if (_shut_down || _startup_status_text == text) {
        return;
    }

    _startup_status_text = text;
    emit startupStatusTextChanged();
}

QVariantList AppController::sampleFiles() const {
    // Bundled sample files live next to the executable (release zip), one level
    // up (dev build tree), or under share/ (AppImage). First hit wins.
    const QDir appDir(QCoreApplication::applicationDirPath());
    for (const QString& rel : {QStringLiteral("samples"),
                               QStringLiteral("../samples"),
                               QStringLiteral("../share/automotive-format-explorer/samples")}) {
        const QFileInfoList entries = supportedFiles(_formats, QDir(appDir.filePath(rel)));
        if (entries.isEmpty()) {
            continue;
        }

        QVariantList list;
        for (const auto& entry : entries) {
            list.push_back(QVariantMap{
                {QStringLiteral("title"), entry.fileName()},
                {QStringLiteral("url"), QUrl::fromLocalFile(entry.absoluteFilePath())},
            });
        }
        return list;
    }
    return QVariantList{};
}

QStringList AppController::fileDialogFilters() const {
    return ::fileDialogFilters(_formats);
}

void AppController::setLastError(const QString& errorText) {
    if (_last_error == errorText) {
        return;
    }

    _last_error = errorText;
    emit lastErrorChanged();
}
