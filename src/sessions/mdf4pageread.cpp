#include "sessions/mdf4pageread.h"

#include <algorithm>
#include <utility>

namespace mdf4page {
namespace {

// A Byte channel of fixed (cn_type 0) or MLSD (5) width never delivers more
// than its declared Bytes, three characters each; any other cell takes the
// general cap.
std::size_t cellBytes(const mdf4::Channel& channel) {
    if (channel.binary() && (channel.cn_type() == 0 || channel.cn_type() == 5)) {
        return std::min(kCellBytes, 3 * static_cast<std::size_t>(channel.bit_count() / 8));
    }
    return kCellBytes;
}

// Reads `channel` through `kind` over the page's samples into `column`; the
// page's own lane also delivers the rows' times.
mdf4::ScanResult readLane(const PagePlan& plan, Lane::Kind kind, std::uint32_t channel, std::size_t column,
                          bool own, TablePageBuilder& builder, const ScanFunction& scan,
                          const ScanTextFunction& scanText, const ScanBytesFunction& scanBytes,
                          const mdf4::Control& control) {
    switch (kind) {
    case Lane::Kind::Values:
        return scan(plan.group, channel, plan.first, plan.rows, control,
                    [&](const mdf4::SampleChunk& chunk) {
                        if (own) {
                            builder.addTime(chunk.time, chunk.size);
                        }
                        builder.addValues(column, chunk.value, chunk.size);
                        return mdf4::Visit::Continue;
                    });
    case Lane::Kind::Text:
        return scanText(plan.group, channel, plan.first, plan.rows, control,
                        [&](const mdf4::TextChunk& chunk) {
                            if (own) {
                                builder.addTime(chunk.time, chunk.size);
                            }
                            builder.addText(column, chunk.text, chunk.size);
                            return mdf4::Visit::Continue;
                        });
    case Lane::Kind::Bytes:
        break;
    }
    return scanBytes(plan.group, channel, plan.first, plan.rows, control,
                     [&](const mdf4::BytesChunk& chunk) {
                         if (own) {
                             builder.addTime(chunk.time, chunk.size);
                         }
                         builder.addBytes(column, chunk.value, chunk.size);
                         return mdf4::Visit::Continue;
                     });
}

} // namespace

std::vector<TableColumnSpec> PagePlan::columns() const {
    std::vector<TableColumnSpec> specs;
    specs.reserve(members.size() + 1);
    for (const Lane& lane : members) {
        const bool values = lane.kind == Lane::Kind::Values;
        specs.push_back({values ? TableColumn::Kind::Values : TableColumn::Kind::Text, lane.cellBytes});
    }
    specs.push_back({TableColumn::Kind::Text, cellBytes});
    return specs;
}

std::string channelTitle(const mdf4::ChannelGroup& group, int index) {
    const mdf4::Channel& channel = group.channels(index);
    const std::string& name = channel.name();
    if (channel.has_member_of()) {
        const std::string separator = group.path_separator().empty() ? "." : group.path_separator();
        const std::string& prefix = group.channels(static_cast<int>(channel.member_of())).name();
        if (name.size() > prefix.size() + separator.size() && name.compare(0, prefix.size(), prefix) == 0 &&
            name.compare(prefix.size(), separator.size(), separator) == 0) {
            return name.substr(prefix.size() + separator.size());
        }
    }
    return name.empty() ? "Channel " + std::to_string(index + 1) : name;
}

PagePlan pagePlan(const mdf4::File& document, std::uint32_t group, std::uint32_t channel,
                  std::uint64_t first) {
    const mdf4::ChannelGroup& channels = document.groups(static_cast<int>(group));
    const mdf4::Channel& own = channels.channels(static_cast<int>(channel));
    PagePlan plan;
    plan.group = group;
    plan.channel = channel;
    plan.kind = own.textual() ? Lane::Kind::Text : Lane::Kind::Bytes;
    plan.cellBytes = cellBytes(own);
    plan.first = first;
    const std::uint64_t stated = own.sample_count();
    plan.rows = first < stated ? std::min(kPageSamples, stated - first) : 0;
    for (int index = 0; index < channels.channels_size(); ++index) {
        const mdf4::Channel& member = channels.channels(index);
        if (!member.has_member_of() || member.member_of() != channel || !member.decodable() ||
            member.has_array()) {
            continue;
        }
        Lane lane;
        lane.kind = member.textual() ? Lane::Kind::Text
                    : member.binary() ? Lane::Kind::Bytes
                                      : Lane::Kind::Values;
        lane.channel = static_cast<std::uint32_t>(index);
        lane.cellBytes = cellBytes(member);
        lane.name = channelTitle(channels, index);
        plan.members.push_back(std::move(lane));
    }
    plan.values = own.textual() ? "Texts" : plan.members.empty() ? "Bytes" : "Frames";
    return plan;
}

std::uint64_t reservation(const PagePlan& plan) {
    return TablePageBuilder::reservation(plan.rows, plan.columns());
}

PageResult readPage(const PagePlan& plan, const ScanFunction& scan, const ScanTextFunction& scanText,
                    const ScanBytesFunction& scanBytes, const mdf4::Control& control) {
    TablePageBuilder builder(plan.first, plan.rows, plan.columns());
    PageResult result;
    result.scan = readLane(plan, plan.kind, plan.channel, plan.members.size(), true, builder, scan, scanText,
                           scanBytes, control);
    if (result.scan.outcome.status != mdf4::Status::Ok) {
        return result;
    }
    std::string note;
    for (std::size_t column = 0; column < plan.members.size(); ++column) {
        const Lane& lane = plan.members[column];
        mdf4::ScanResult read = readLane(plan, lane.kind, lane.channel, column, false, builder, scan,
                                         scanText, scanBytes, control);
        const mdf4::Outcome& outcome = read.outcome;
        if (outcome.status == mdf4::Status::Cancelled || outcome.status == mdf4::Status::SourceChanged) {
            result.scan = std::move(read);
            return result;
        }
        if (outcome.status != mdf4::Status::Ok) {
            builder.clearColumn(column);
            note += note.empty() ? "" : "  ·  ";
            note += lane.name + " could not be read: " + outcome.message;
            if (!outcome.location.empty()) {
                note += " (" + outcome.location + ")";
            }
        }
    }
    result.page = builder.finish(result.scan.invalidValues, std::move(note));
    return result;
}

} // namespace mdf4page
