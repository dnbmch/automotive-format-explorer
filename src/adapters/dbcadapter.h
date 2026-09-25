#pragma once

#include "adapters/formatadapter.h"

class DbcAdapter final : public FormatAdapter {
public:
    LoadResult load(const QString& path) const override;
};
