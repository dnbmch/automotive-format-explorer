#include "core/appcontroller.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLibrary>
#include <QtConcurrent/QtConcurrentRun>

#ifdef BACKENDS_STATIC
extern "C" FormatAdapter* createA2lAdapterPlugin();
extern "C" FormatAdapter* createDbcAdapterPlugin();
extern "C" FormatAdapter* createLdfAdapterPlugin();
#endif

namespace {

using CreateAdapterFn = FormatAdapter* (*)();

struct BackendSpec {
    FormatId formatId;
    QString extension;
    QString libraryBaseName;
    const char* createSymbol;
};

QString sharedLibraryFileName(const QString& baseName) {
#if defined(Q_OS_WIN)
    return baseName + QStringLiteral(".dll");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("lib") + baseName + QStringLiteral(".dylib");
#else
    return QStringLiteral("lib") + baseName + QStringLiteral(".so");
#endif
}

const BackendSpec* backendSpecForPath(const QString& path) {
    static const BackendSpec specs[] = {
        {FormatId::A2L, QStringLiteral("a2l"), QStringLiteral("explorer-a2l-backend"), "createA2lAdapterPlugin"},
        {FormatId::DBC, QStringLiteral("dbc"), QStringLiteral("explorer-dbc-backend"), "createDbcAdapterPlugin"},
        {FormatId::LDF, QStringLiteral("ldf"), QStringLiteral("explorer-ldf-backend"), "createLdfAdapterPlugin"},
    };

    const QString suffix = QFileInfo(path).suffix().toLower();
    for (const auto& spec : specs) {
        if (spec.extension == suffix) {
            return &spec;
        }
    }

    return nullptr;
}

} // namespace

AppController::AppController(QObject* parent)
    : QObject(parent) {
    connect(&_load_watcher, &QFutureWatcher<std::shared_ptr<LoadResult>>::finished,
            this, &AppController::onLoadFinished);

#ifdef BACKENDS_STATIC
    _format_registry.registerAdapter(std::unique_ptr<FormatAdapter>(createA2lAdapterPlugin()));
    _format_registry.registerAdapter(std::unique_ptr<FormatAdapter>(createDbcAdapterPlugin()));
    _format_registry.registerAdapter(std::unique_ptr<FormatAdapter>(createLdfAdapterPlugin()));
#endif
}

TabModel* AppController::tabModel() {
    return &_tab_model;
}

TreeModel* AppController::currentTreeModel() {
    DocumentSession* session = _tab_model.sessionAt(_current_tab_index);
    return session ? session->treeModel() : &_empty_tree_model;
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
    emit currentSessionChanged();
}

QString AppController::lastError() const {
    return _last_error;
}

void AppController::openFile(const QUrl& fileUrl) {
    clearLastError();

    if (_file_loading) {
        setLastError(QStringLiteral("Another file is already loading."));
        return;
    }

    const QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
    if (path.isEmpty()) {
        setLastError(QStringLiteral("No file was selected."));
        return;
    }

    const FormatAdapter* adapter = ensureAdapterForPath(path);
    if (!adapter) {
        if (_last_error.isEmpty()) {
            setLastError(QStringLiteral("No adapter is registered for %1").arg(path));
        }
        return;
    }

    setFileLoading(true);

    QThread* mainThread = thread();
    auto future = QtConcurrent::run([adapter, path, mainThread]() -> std::shared_ptr<LoadResult> {
        auto r = std::make_shared<LoadResult>(adapter->load(path));
        if (r->session)
            r->session->moveModelsToThread(mainThread);
        return r;
    });
    _load_watcher.setFuture(future);
}

void AppController::onLoadFinished() {
    setFileLoading(false);

    auto result = _load_watcher.result();
    if (!result || !result->session) {
        if (result && !result->diagnostics.isEmpty()) {
            setLastError(result->diagnostics.first().detail);
        } else {
            setLastError(QStringLiteral("Failed to load file."));
        }
        return;
    }

    const QString name = result->session->displayName();
    const int newIndex = _tab_model.addSession(std::move(result->session));
    setCurrentTabIndex(newIndex);
    emit fileLoaded(name);
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

const FormatAdapter* AppController::ensureAdapterForPath(const QString& path) {
    const FormatAdapter* adapter = _format_registry.adapterForPath(path);
    if (adapter) {
        return adapter;
    }

    if (!loadBackendForPath(path)) {
        return nullptr;
    }

    return _format_registry.adapterForPath(path);
}

bool AppController::loadBackendForPath(const QString& path) {
    const BackendSpec* spec = backendSpecForPath(path);
    if (!spec) {
        setLastError(QStringLiteral("No backend is implemented for %1 files yet.").arg(QFileInfo(path).suffix().toUpper()));
        return false;
    }

    // Already loaded for this format
    if (_format_registry.adapterForPath(path)) {
        return true;
    }

    return loadBackend(spec->formatId, spec->libraryBaseName, spec->createSymbol);
}

bool AppController::loadBackend(FormatId formatId, const QString& libraryBaseName, const char* createSymbol) {
    auto library = std::make_unique<QLibrary>(
        QDir(QCoreApplication::applicationDirPath()).filePath(sharedLibraryFileName(libraryBaseName)));

    if (!library->load()) {
        setLastError(QStringLiteral("Failed to load %1 backend library: %2")
                         .arg(formatDisplayName(formatId), library->errorString()));
        return false;
    }

    const auto createAdapter = reinterpret_cast<CreateAdapterFn>(library->resolve(createSymbol));
    if (!createAdapter) {
        setLastError(QStringLiteral("Failed to resolve %1 backend factory: %2")
                         .arg(formatDisplayName(formatId), library->errorString()));
        return false;
    }

    std::unique_ptr<FormatAdapter> adapter(createAdapter());
    if (!adapter) {
        setLastError(QStringLiteral("The %1 backend factory returned no adapter.").arg(formatDisplayName(formatId)));
        return false;
    }

    _format_registry.registerAdapter(std::move(adapter));
    _loaded_backends.push_back(std::move(library));
    return true;
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

void AppController::setLastError(const QString& errorText) {
    if (_last_error == errorText) {
        return;
    }

    _last_error = errorText;
    emit lastErrorChanged();
}
