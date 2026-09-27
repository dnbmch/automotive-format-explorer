#pragma once

#include "sessions/presentertext.h"

#pragma push_macro("signals")
#undef signals
#include "mdf4/mdf4.pb.h"
#pragma pop_macro("signals")

enum class Mdf4EntityKind {
    File,
    ChannelGroup,
    Channel
};

// An MDF4 entity a tree row shows: the file, a channel group or a channel.
struct Mdf4Path {
    Mdf4EntityKind kind = Mdf4EntityKind::File;
    int groupIndex = -1;
    int channelIndex = -1;
};

class Mdf4DetailPresenter final {
public:
    explicit Mdf4DetailPresenter(const mdf4::File& document)
        : _document(document) {
    }

    QList<DetailSection> buildDetails(const Mdf4Path& path) const;
    QString buildRawJson(const Mdf4Path& path) const;

private:
    QList<DetailSection> fileDetails() const;
    QList<DetailSection> groupDetails(const Mdf4Path& path) const;
    QList<DetailSection> channelDetails(const Mdf4Path& path) const;

    const mdf4::File& _document;
};
