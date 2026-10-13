#pragma once

// One sample of an MDF4 array channel as a matrix: which elements and axis
// labels to read, planned from the metadata on the GUI thread, and the read
// itself on a worker. Qt-free.

#pragma push_macro("signals")
#undef signals
#include "mdf4/reader.h"
#pragma pop_macro("signals")

#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <vector>

namespace mdf4array {

// The reader's operations, in its shapes.
using ScanFunction = std::function<mdf4::ScanResult(
    std::uint32_t group, std::uint32_t channel, std::uint64_t first, std::uint64_t count,
    const mdf4::Control& control, const mdf4::Visitor& visitor)>;
using ReadArrayFunction = std::function<mdf4::ReadArrayResult(
    std::uint32_t group, std::uint32_t channel, std::uint64_t first, std::uint64_t count,
    const mdf4::ElementWindow& elements, const mdf4::Control& control)>;
using AxisFunction = std::function<mdf4::Axis(std::uint32_t group)>;

// Which sample j of an axis channel labels array sample s (ASAM MDF 4.2.0 BS
// p.118, p.128-129; mdf4.proto ArrayDimension).
enum class AxisPairing {
    Same,          // the axis channel is in the array's group: j = s
    Constant,      // a one-cycle axis group without a time master: j = 0
    CommonMaster,  // both groups have a time master: the axis sample with the
                   // latest time at or before the array sample's, ties to the
                   // latest sample
    None,          // index headers
};

// Where the labels of one matrix axis come from.
struct AxisSource {
    enum class Kind { Index, Fixed, Channel };
    Kind kind = Kind::Index;
    std::vector<double> fixed;  // Fixed: the dimension's axis_values
    // Channel: the axis channel, itself a one-dimensional array channel read
    // through readArray, never scan: its first N elements (ca_type 1) or its
    // N + 1 borders (ca_type 3, `intervals`, `leftOpen` from its own
    // Channel.array.left_open). `name` is its tree title, which the notes use.
    std::uint32_t group = 0;
    std::uint32_t channel = 0;
    std::string name;
    bool intervals = false;
    bool leftOpen = false;
    AxisPairing pairing = AxisPairing::Same;
    mdf4::Axis master;             // axis(group): CommonMaster scans its channel
    std::uint64_t cycleCount = 0;  // the axis group's: the scan's length
    std::uint64_t labels = 0;      // label values: Index 0, Fixed N, Channel N or N + 1
};

// What one matrix shows of an array channel's sample. Dimensions are outer
// first. D >= 2: the last two are rows and columns, `slice` holds the D - 2
// outer indices, and the window is the slice's rows x columns elements, since
// k is row-major outer first (reader.h ElementWindow). D = 1: N rows, 1 column.
struct ArrayPlan {
    std::uint32_t group = 0;
    std::uint32_t channel = 0;
    std::uint64_t sample = 0;
    std::vector<std::uint64_t> slice;
    std::uint64_t rows = 0;
    std::uint64_t columns = 0;
    mdf4::ElementWindow window;
    AxisSource rowAxis;
    AxisSource columnAxis;
};

// From the metadata and the source's axes only. `group` and `channel` name a
// decodable array channel; each slice index is clamped to its dimension, a
// missing one is 0.
ArrayPlan arrayPlan(const mdf4::File& document, const AxisFunction& axis, std::uint32_t group,
                    std::uint32_t channel, std::uint64_t sample,
                    const std::vector<std::uint64_t>& slice);

struct AxisLabels {
    enum class Kind { Index, Points, Intervals };
    Kind kind = Kind::Index;
    std::vector<double> values;  // Points: N; Intervals: N + 1 borders
    bool leftOpen = false;       // Intervals: ]a, b] when set, else [a, b[
    std::string note;            // why a channel axis shows index headers
};

// Only an Ok outcome with a sample read carries values and labels.
struct ArraySample {
    mdf4::Outcome outcome;
    std::uint64_t samplesRead = 0;  // 0: the recording ends before the sample
    double time = std::numeric_limits<double>::quiet_NaN();
    std::uint64_t rows = 0;
    std::uint64_t columns = 0;
    std::vector<double> values;  // rows x columns, row-major; NaN = invalid
    AxisLabels rowLabels;
    AxisLabels columnLabels;

    // The structure and its value and label vectors; a note is not counted.
    std::uint64_t bytes() const;
};

// Bytes the sample may hold, admitted before the read: the structure, the
// values, a time and both axes' labels, saturating.
std::uint64_t reservation(const ArrayPlan& plan);

// On a worker: the plan's sample, then each channel axis's labels. An axis
// that cannot be labelled shows index headers with a note; a Cancelled or
// SourceChanged outcome of any read is the sample's.
ArraySample readArraySample(const ArrayPlan& plan, const ReadArrayFunction& readArray,
                            const ScanFunction& scan, const mdf4::Control& control);

} // namespace mdf4array
