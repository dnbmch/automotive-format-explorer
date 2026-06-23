#include "core/formatregistry.h"

#include <QDebug>
#include <QFileInfo>

void FormatRegistry::registerAdapter(std::unique_ptr<FormatAdapter> adapter) {
    const FormatId formatId = adapter->formatId();
    for (const auto& existing : _adapters) {
        if (existing->formatId() == formatId) {
            qWarning() << "FormatRegistry: ignoring duplicate adapter for format"
                       << formatDisplayName(formatId);
            return;
        }
    }
    _adapters.push_back(std::move(adapter));
}

const FormatAdapter* FormatRegistry::adapterForPath(const QString& path) const {
    const QString suffix = QFileInfo(path).suffix().toLower();
    for (const auto& adapter : _adapters) {
        const QStringList extensions = adapter->extensions();
        for (const QString& extension : extensions) {
            if (extension.compare(suffix, Qt::CaseInsensitive) == 0) {
                return adapter.get();
            }
        }
    }
    return nullptr;
}
