#pragma once

#include "adapters/formatadapter.h"

class LdfAdapter final : public FormatAdapter {
public:
    LoadResult load(const QString& path) const override;
};
