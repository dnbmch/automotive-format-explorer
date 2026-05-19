#pragma once

#include "adapters/formatadapter.h"

#include <memory>
#include <vector>

class FormatRegistry {
public:
    void registerAdapter(std::unique_ptr<FormatAdapter> adapter);
    const FormatAdapter* adapterForPath(const QString& path) const;

private:
    std::vector<std::unique_ptr<FormatAdapter>> _adapters;
};
