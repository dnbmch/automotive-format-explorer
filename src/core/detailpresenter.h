#pragma once

#include "core/detailsection.h"
#include "core/noderegistry.h"

class DetailPresenter {
public:
    virtual ~DetailPresenter() = default;
    virtual QList<DetailSection> buildDetails(const NodeBinding& binding) const = 0;
    virtual QString buildRawJson(const NodeBinding& binding) const { Q_UNUSED(binding); return {}; }
};
