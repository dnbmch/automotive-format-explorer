#include "sessions/mdf4arrayread.h"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace mdf4array {
namespace {

constexpr std::uint64_t kSaturated = std::numeric_limits<std::uint64_t>::max();

std::uint64_t saturatingAdd(std::uint64_t a, std::uint64_t b) {
    return a > kSaturated - b ? kSaturated : a + b;
}

std::uint64_t saturatingMultiply(std::uint64_t a, std::uint64_t b) {
    return b != 0 && a > kSaturated / b ? kSaturated : a * b;
}

// One dimension's axis. A channel axis is paired with the array's samples by
// its group: the array's own, a one-cycle group without a time master, or a
// time master on both sides (`time` is the array group's axis).
AxisSource axisSource(const mdf4::File& document, const AxisFunction& axis, std::uint32_t group,
                      const mdf4::Axis& time, const mdf4::ArrayDimension& dimension) {
    AxisSource source;
    if (dimension.axis_from_channel()) {
        const mdf4::ChannelGroup& axisGroup = document.groups(static_cast<int>(dimension.axis_group()));
        const mdf4::Channel& axisChannel = axisGroup.channels(static_cast<int>(dimension.axis_channel()));
        source.kind = AxisSource::Kind::Channel;
        source.group = dimension.axis_group();
        source.channel = dimension.axis_channel();
        // Named as the tree titles a channel.
        source.name = axisChannel.name().empty() ? "Channel " + std::to_string(source.channel + 1)
                                                 : axisChannel.name();
        source.intervals = axisChannel.array().ca_type() == 3;
        source.leftOpen = source.intervals && axisChannel.array().left_open();
        source.labels = dimension.size() + (source.intervals ? 1 : 0);
        source.master = axis(source.group);
        source.cycleCount = axisGroup.cycle_count();
        if (source.group == group) {
            source.pairing = AxisPairing::Same;
        } else if (source.cycleCount == 1 && source.master.kind != mdf4::AxisKind::Master) {
            source.pairing = AxisPairing::Constant;
        } else if (time.kind == mdf4::AxisKind::Master && source.master.kind == mdf4::AxisKind::Master) {
            source.pairing = AxisPairing::CommonMaster;
        } else {
            source.pairing = AxisPairing::None;
        }
    } else if (dimension.axis_values_size() > 0) {
        source.kind = AxisSource::Kind::Fixed;
        source.fixed = dimension.axis_values().data();
        source.labels = static_cast<std::uint64_t>(dimension.axis_values_size());
    }
    return source;
}

bool endsSample(mdf4::Status status) {
    return status == mdf4::Status::Cancelled || status == mdf4::Status::SourceChanged;
}

void indexHeaders(AxisLabels& labels, std::string note) {
    labels = AxisLabels{};
    labels.note = std::move(note);
}

// Labels `axis` for array sample `sample` at `time`. False when one of its
// reads ended Cancelled or SourceChanged, whose outcome is then `ended`.
bool label(const AxisSource& axis, std::uint64_t sample, double time, const ReadArrayFunction& readArray,
           const ScanFunction& scan, const mdf4::Control& control, AxisLabels& labels, mdf4::Outcome& ended) {
    if (axis.kind == AxisSource::Kind::Index) {
        return true;
    }
    if (axis.kind == AxisSource::Kind::Fixed) {
        labels.kind = AxisLabels::Kind::Points;
        labels.values.assign(axis.fixed, axis.fixed + axis.labels);
        return true;
    }
    std::uint64_t index = sample;
    if (axis.pairing == AxisPairing::None) {
        indexHeaders(labels, "no common master with " + axis.name);
        return true;
    }
    if (axis.pairing == AxisPairing::Constant) {
        index = 0;
    }
    if (axis.pairing == AxisPairing::CommonMaster) {
        // The axis sample with the latest time at or before the array
        // sample's, ties to the latest sample.
        bool found = false;
        double latest = 0.0;
        const mdf4::ScanResult scanned =
            scan(axis.master.group, axis.master.channel, 0, axis.cycleCount, control,
                 [&](const mdf4::SampleChunk& chunk) {
                     for (std::size_t i = 0; i < chunk.size; ++i) {
                         if (chunk.time[i] <= time && (!found || chunk.time[i] >= latest)) {
                             found = true;
                             latest = chunk.time[i];
                             index = chunk.firstSample + i;
                         }
                     }
                     return mdf4::Visit::Continue;
                 });
        if (endsSample(scanned.outcome.status)) {
            ended = scanned.outcome;
            return false;
        }
        if (scanned.outcome.status != mdf4::Status::Ok) {
            indexHeaders(labels, scanned.outcome.message);
            return true;
        }
        if (!found) {
            indexHeaders(labels, "no " + axis.name + " sample at or before this one");
            return true;
        }
    }
    mdf4::ReadArrayResult read = readArray(axis.group, axis.channel, index, 1, {0, axis.labels}, control);
    if (endsSample(read.outcome.status)) {
        ended = read.outcome;
        return false;
    }
    if (read.outcome.status != mdf4::Status::Ok || read.samplesRead == 0) {
        indexHeaders(labels, read.outcome.message);
        return true;
    }
    labels.kind = axis.intervals ? AxisLabels::Kind::Intervals : AxisLabels::Kind::Points;
    labels.values = std::move(read.series.value);
    labels.leftOpen = axis.leftOpen;
    return true;
}

} // namespace

ArrayPlan arrayPlan(const mdf4::File& document, const AxisFunction& axis, std::uint32_t group,
                    std::uint32_t channel, std::uint64_t sample, const std::vector<std::uint64_t>& slice) {
    const mdf4::ChannelArray& array =
        document.groups(static_cast<int>(group)).channels(static_cast<int>(channel)).array();
    const int dimensions = array.dimensions_size();
    const mdf4::Axis time = axis(group);
    ArrayPlan plan;
    plan.group = group;
    plan.channel = channel;
    plan.sample = sample;
    if (dimensions == 1) {
        plan.rows = array.dimensions(0).size();
        plan.columns = 1;
        plan.window = {0, plan.rows};
        plan.rowAxis = axisSource(document, axis, group, time, array.dimensions(0));
        return plan;
    }
    std::uint64_t flat = 0;
    for (int d = 0; d < dimensions - 2; ++d) {
        const std::uint64_t size = array.dimensions(d).size();
        const std::size_t at = static_cast<std::size_t>(d);
        const std::uint64_t index = std::min<std::uint64_t>(at < slice.size() ? slice[at] : 0, size - 1);
        plan.slice.push_back(index);
        flat = flat * size + index;
    }
    const mdf4::ArrayDimension& rows = array.dimensions(dimensions - 2);
    const mdf4::ArrayDimension& columns = array.dimensions(dimensions - 1);
    plan.rows = rows.size();
    plan.columns = columns.size();
    plan.window = {flat * plan.rows * plan.columns, plan.rows * plan.columns};
    plan.rowAxis = axisSource(document, axis, group, time, rows);
    plan.columnAxis = axisSource(document, axis, group, time, columns);
    return plan;
}

std::uint64_t ArraySample::bytes() const {
    return sizeof(ArraySample) +
           (values.capacity() + rowLabels.values.capacity() + columnLabels.values.capacity()) * sizeof(double);
}

std::uint64_t reservation(const ArrayPlan& plan) {
    std::uint64_t values = saturatingAdd(saturatingMultiply(plan.rows, plan.columns), 1);
    values = saturatingAdd(saturatingAdd(values, plan.rowAxis.labels), plan.columnAxis.labels);
    return saturatingAdd(sizeof(ArraySample), saturatingMultiply(values, sizeof(double)));
}

ArraySample readArraySample(const ArrayPlan& plan, const ReadArrayFunction& readArray, const ScanFunction& scan,
                            const mdf4::Control& control) {
    ArraySample sample;
    sample.rows = plan.rows;
    sample.columns = plan.columns;
    mdf4::ReadArrayResult read = readArray(plan.group, plan.channel, plan.sample, 1, plan.window, control);
    sample.outcome = read.outcome;
    if (read.outcome.status != mdf4::Status::Ok || read.samplesRead == 0) {
        return sample;
    }
    const double time = read.series.time.front();
    AxisLabels rowLabels;
    AxisLabels columnLabels;
    if (!label(plan.rowAxis, plan.sample, time, readArray, scan, control, rowLabels, sample.outcome) ||
        !label(plan.columnAxis, plan.sample, time, readArray, scan, control, columnLabels, sample.outcome)) {
        return sample;
    }
    sample.samplesRead = read.samplesRead;
    sample.time = time;
    sample.values = std::move(read.series.value);
    sample.rowLabels = std::move(rowLabels);
    sample.columnLabels = std::move(columnLabels);
    return sample;
}

} // namespace mdf4array
