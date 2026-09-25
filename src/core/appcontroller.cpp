#include "core/appcontroller.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QtConcurrent/QtConcurrentRun>

AppController::AppController(FormatList formats, QObject* parent)
    : QObject(parent),
      _formats(std::move(formats)) {
    _empty_tree_filter.setSourceModel(&_empty_tree_model);
    connect(&_load_watcher, &QFutureWatcher<LoadResult>::finished,
            this, &AppController::onLoadFinished);
}

AppController::~AppController() {
    shutdown();
}

TabModel* AppController::tabModel() {
    return &_tab_model;
}

TreeFilterModel* AppController::currentTreeModel() {
    DocumentSession* session = _tab_model.sessionAt(_current_tab_index);
    if (!session) {
        return &_empty_tree_filter;
    }

    auto& filter = _tree_filters[session];
    if (!filter) {
        filter = std::make_unique<TreeFilterModel>();
        filter->setSourceModel(session->treeModel());
    }
    return filter.get();
}

DetailModel* AppController::currentDetailModel() {
    DocumentSession* session = _tab_model.sessionAt(_current_tab_index);
    return session ? session->detailModel() : &_empty_detail_model;
}

QUrl AppController::centerPanelSource() {
    DocumentSession* session = _tab_model.sessionAt(_current_tab_index);
    return session ? session->centerPanelSource() : QUrl();
}

QAbstractListModel* AppController::centerPanelModel() {
    DocumentSession* session = _tab_model.sessionAt(_current_tab_index);
    return session ? session->centerPanelModel() : nullptr;
}

int AppController::currentTabIndex() const {
    return _current_tab_index;
}

void AppController::setCurrentTabIndex(int index) {
    if (index < -1 || index >= _tab_model.rowCount()) {
        return;
    }

    if (_current_tab_index == index) {
        return;
    }

    _current_tab_index = index;
    emit currentTabIndexChanged();
    if (_shut_down) {
        return;
    }
    emit currentSessionChanged();
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

    // The adapter stays owned by _formats; shutdown() joins this task before
    // the controller's members are destroyed. The session's models are handed
    // to this thread before the result is published.
    const FormatAdapter* adapter = format->adapter.get();
    QThread* owner = thread();
    _load_watcher.setFuture(QtConcurrent::run([adapter, path, owner]() {
        LoadResult result = adapter->load(path);
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
    const int newIndex = _tab_model.addSession(std::move(result.session));
    if (!_shut_down) {
        setCurrentTabIndex(newIndex);
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
    // still owns is waited for here; its worker never needs this thread's event
    // loop. The result, finished or still queued for delivery, is destroyed on
    // this thread, which owns its models. Shutdown notifies no one.
    disconnect(&_load_watcher, nullptr, this, nullptr);
    if (!_file_loading) {
        return;
    }
    QFuture<LoadResult> pending = _load_watcher.future();
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

void AppController::closeTab(int index) {
    if (index < 0 || index >= _tab_model.rowCount()) {
        return;
    }

    const int previousCurrentIndex = _current_tab_index;
    _tree_filters.erase(_tab_model.sessionAt(index));
    _tab_model.closeSession(index);
    if (_tab_model.rowCount() == 0) {
        if (_current_tab_index != -1) {
            _current_tab_index = -1;
            emit currentTabIndexChanged();
            emit currentSessionChanged();
        }
        return;
    }

    if (previousCurrentIndex > index) {
        setCurrentTabIndex(previousCurrentIndex - 1);
        return;
    }

    if (previousCurrentIndex == index) {
        const int newIndex = qMin(index, _tab_model.rowCount() - 1);
        if (_current_tab_index != newIndex) {
            _current_tab_index = newIndex;
            emit currentTabIndexChanged();
        }
        emit currentSessionChanged();
    }
}

void AppController::selectCurrentNode(qulonglong nodeKey) {
    DocumentSession* session = _tab_model.sessionAt(_current_tab_index);
    if (!session) {
        return;
    }

    session->selectNode(static_cast<quint64>(nodeKey));
}

void AppController::clearLastError() {
    if (_last_error.isEmpty()) {
        return;
    }

    _last_error.clear();
    emit lastErrorChanged();
}

bool AppController::startupLoading() const {
    return _startup_loading;
}

void AppController::setStartupLoading(bool loading) {
    if (_startup_loading == loading) {
        return;
    }

    _startup_loading = loading;
    emit startupLoadingChanged();
}

QString AppController::startupStatusText() const {
    return _startup_status_text;
}

void AppController::setStartupStatusText(const QString& text) {
    if (_startup_status_text == text) {
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
