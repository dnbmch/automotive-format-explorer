#include "core/formatregistry.h"

#include <QFileInfo>

void FormatRegistry::registerAdapter(std::unique_ptr<FormatAdapter> adapter) {
    adapters_.push_back(std::move(adapter));
}

const FormatAdapter* FormatRegistry::adapterForPath(const QString& path) const {
    const QString suffix = QFileInfo(path).suffix().toLower();
    for (const auto& adapter : adapters_) {
        const QStringList extensions = adapter->extensions();
        for (const QString& extension : extensions) {
            if (extension.compare(suffix, Qt::CaseInsensitive) == 0) {
                return adapter.get();
            }
        }
    }
    return nullptr;
}
