#include "core/appcontroller.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLibrary>

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
}

TabModel* AppController::tabModel() {
    return &tabModel_;
}

TreeModel* AppController::currentTreeModel() {
    DocumentSession* session = tabModel_.sessionAt(currentTabIndex_);
    return session ? session->treeModel() : &emptyTreeModel_;
}

DetailModel* AppController::currentDetailModel() {
    DocumentSession* session = tabModel_.sessionAt(currentTabIndex_);
    return session ? session->detailModel() : &emptyDetailModel_;
}

int AppController::currentTabIndex() const {
    return currentTabIndex_;
}

void AppController::setCurrentTabIndex(int index) {
    if (index < -1 || index >= tabModel_.rowCount()) {
        return;
    }

    if (currentTabIndex_ == index) {
        return;
    }

    currentTabIndex_ = index;
    emit currentTabIndexChanged();
    emit currentSessionChanged();
}

QString AppController::lastError() const {
    return lastError_;
}

void AppController::openFile(const QUrl& fileUrl) {
    clearLastError();

    const QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
    if (path.isEmpty()) {
        setLastError(QStringLiteral("No file was selected."));
        return;
    }

    const FormatAdapter* adapter = ensureAdapterForPath(path);
    if (!adapter) {
        if (lastError_.isEmpty()) {
            setLastError(QStringLiteral("No adapter is registered for %1").arg(path));
        }
        return;
    }

    LoadResult result = adapter->load(path);
    if (!result.session) {
        if (!result.diagnostics.isEmpty()) {
            setLastError(result.diagnostics.first().detail);
        } else {
            setLastError(QStringLiteral("Failed to load %1").arg(path));
        }
        return;
    }

    const QString name = result.session->displayName();
    const int newIndex = tabModel_.addSession(std::move(result.session));
    setCurrentTabIndex(newIndex);
    emit fileLoaded(name);
}

void AppController::closeTab(int index) {
    if (index < 0 || index >= tabModel_.rowCount()) {
        return;
    }

    const int previousCurrentIndex = currentTabIndex_;
    tabModel_.closeSession(index);
    if (tabModel_.rowCount() == 0) {
        if (currentTabIndex_ != -1) {
            currentTabIndex_ = -1;
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
        const int newIndex = qMin(index, tabModel_.rowCount() - 1);
        if (currentTabIndex_ != newIndex) {
            currentTabIndex_ = newIndex;
            emit currentTabIndexChanged();
        }
        emit currentSessionChanged();
    }
}

void AppController::selectCurrentNode(qulonglong nodeKey) {
    DocumentSession* session = tabModel_.sessionAt(currentTabIndex_);
    if (!session) {
        return;
    }

    session->selectNode(static_cast<quint64>(nodeKey));
}

void AppController::clearLastError() {
    if (lastError_.isEmpty()) {
        return;
    }

    lastError_.clear();
    emit lastErrorChanged();
}

const FormatAdapter* AppController::ensureAdapterForPath(const QString& path) {
    const FormatAdapter* adapter = formatRegistry_.adapterForPath(path);
    if (adapter) {
        return adapter;
    }

    if (!loadBackendForPath(path)) {
        return nullptr;
    }

    return formatRegistry_.adapterForPath(path);
}

bool AppController::loadBackendForPath(const QString& path) {
    const BackendSpec* spec = backendSpecForPath(path);
    if (!spec) {
        setLastError(QStringLiteral("No backend is implemented for %1 files yet.").arg(QFileInfo(path).suffix().toUpper()));
        return false;
    }

    // Already loaded for this format
    if (formatRegistry_.adapterForPath(path)) {
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

    formatRegistry_.registerAdapter(std::move(adapter));
    loadedBackends_.push_back(std::move(library));
    return true;
}

bool AppController::startupLoading() const {
    return startupLoading_;
}

void AppController::setStartupLoading(bool loading) {
    if (startupLoading_ == loading) {
        return;
    }

    startupLoading_ = loading;
    emit startupLoadingChanged();
}

QString AppController::startupStatusText() const {
    return startupStatusText_;
}

void AppController::setStartupStatusText(const QString& text) {
    if (startupStatusText_ == text) {
        return;
    }

    startupStatusText_ = text;
    emit startupStatusTextChanged();
}

void AppController::setLastError(const QString& errorText) {
    if (lastError_ == errorText) {
        return;
    }

    lastError_ = errorText;
    emit lastErrorChanged();
}
