#pragma once

#include "adapters/formatadapter.h"

class A2lAdapter final : public FormatAdapter {
public:
    FormatId formatId() const override;
    QString formatName() const override;
    QStringList extensions() const override;
    LoadResult load(const QString& path) const override;
};

extern "C" FormatAdapter* createA2lAdapterPlugin();
