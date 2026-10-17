#pragma once

// A page of an MDF4 channel as table columns: which lanes to read, planned from
// the metadata on the GUI thread, and the reads themselves on a worker. The
// page's own lane is a text channel's texts or a Byte channel's raw Bytes; a
// structure adds a lane per direct member. Qt-free.

#pragma push_macro("signals")
#undef signals
#include "mdf4/reader.h"
#pragma pop_macro("signals")

#include "models/tabledata.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace mdf4page {

// The reader's operations, in its shapes.
using ScanFunction = std::function<mdf4::ScanResult(
    std::uint32_t group, std::uint32_t channel, std::uint64_t first, std::uint64_t count,
    const mdf4::Control& control, const mdf4::Visitor& visitor)>;
using ScanTextFunction = std::function<mdf4::ScanResult(
    std::uint32_t group, std::uint32_t channel, std::uint64_t first, std::uint64_t count,
    const mdf4::Control& control, const mdf4::TextVisitor& visitor)>;
using ScanBytesFunction = std::function<mdf4::ScanResult(
    std::uint32_t group, std::uint32_t channel, std::uint64_t first, std::uint64_t count,
    const mdf4::Control& control, const mdf4::BytesVisitor& visitor)>;

// One column's read: a channel's values through scan, its texts through
// scanText, or its raw Bytes through scanBytes, shown as hex.
struct Lane {
    enum class Kind { Values, Text, Bytes };
    Kind kind = Kind::Values;
    std::uint32_t channel = 0;
    std::size_t cellBytes = kCellBytes;  // Text, Bytes: the cell cap, its cut marker included
    std::string name;                    // the member's tree title, which the notes use
};

// What one page of a text or Byte channel shows: `rows` samples from `first`, a
// column per member lane, then the channel's own.
struct PagePlan {
    std::uint32_t group = 0;
    std::uint32_t channel = 0;
    Lane::Kind kind = Lane::Kind::Text;  // the page's own lane: Text or Bytes
    std::size_t cellBytes = kCellBytes;  // the own column's cap
    std::uint64_t first = 0;
    std::uint64_t rows = 0;
    // The channel's direct members with a column, in document order.
    std::vector<Lane> members;
    // The rows as the notes name them: "Texts", "Frames" or "Bytes".
    const char* values = "Texts";

    // The members' columns in order, then the page's own.
    std::vector<TableColumnSpec> columns() const;
};

// Channel `index` of `group` as the tree titles it, the one rule for the tree,
// the column headers, the member notes and an array's axis names: a compact
// structure's member by the rest of its name after its parent's name and the
// group's path separator ("." when the group states none), else its name, else
// "Channel <index + 1>".
std::string channelTitle(const mdf4::ChannelGroup& group, int index);

// From the metadata only: the page of the decodable text or Byte channel
// `channel` of `group` that starts at `first`. Its rows are its share of the
// stated count, none past it. A direct member has a column when it is decodable
// and no array: a numeric one (a virtual one included) through scan, a textual
// one through scanText, a Byte channel (a nested structure included) through
// scanBytes.
PagePlan pagePlan(const mdf4::File& document, std::uint32_t group, std::uint32_t channel,
                  std::uint64_t first);

// Bytes the page may hold, admitted before its reads.
std::uint64_t reservation(const PagePlan& plan);

// Only a read whose own lane ended Ok, and no member's Cancelled or
// SourceChanged, carries a page.
struct PageResult {
    mdf4::ScanResult scan;  // the own lane's, or that of the member read that ended the page
    TablePagePtr page;
};

// On a worker: the own lane, then each member's, over the page's samples in
// one task. A member read that ends Error or ResourceLimit leaves its column
// empty (the reader's chunks are provisional until a read returns Ok) and says
// so in the page's note; one that ends Cancelled or SourceChanged ends the
// page. Every read receives `control` unchanged.
PageResult readPage(const PagePlan& plan, const ScanFunction& scan, const ScanTextFunction& scanText,
                    const ScanBytesFunction& scanBytes, const mdf4::Control& control);

} // namespace mdf4page
