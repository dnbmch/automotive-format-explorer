#pragma once

#include "core/detailpresenter.h"
#include "sessions/presentertext.h"

#pragma push_macro("signals")
#undef signals
#include "mdf4/mdf4.pb.h"
#pragma pop_macro("signals")

class Mdf4DetailPresenter final : public DetailPresenter {
public:
    explicit Mdf4DetailPresenter(const mdf4::File& document)
        : _document(document) {
    }

    QList<DetailSection> buildDetails(const NodeBinding& binding) const override;
    QString buildRawJson(const NodeBinding& binding) const override;

private:
    QList<DetailSection> fileDetails() const;
    QList<DetailSection> groupDetails(const Mdf4Path& path) const;
    QList<DetailSection> channelDetails(const Mdf4Path& path) const;

    const mdf4::File& _document;
};
