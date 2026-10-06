#pragma once

#include "adapters/formatadapter.h"

class Mdf4Adapter final : public FormatAdapter {
public:
    LoadResult load(const QString& path, const std::atomic<bool>& cancel) const override;
};
