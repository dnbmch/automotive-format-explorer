// The array-read unit: which elements and axis labels the matrix of one array
// sample reads, and the read itself. The plan cases run on a document built
// here against fake reader functions that record their requests; the corpus
// cases run the real reader on the ASAM MDF 4.2.0 examples, the directory
// MDF4_EXAMPLES names. Without that directory the corpus cases skip and the
// executable exits 77, so ctest reports Skipped, never a pass.
//
//   MDF4_EXAMPLES=<ASAM_MDF_BS_V4-2-0/BaseStandard/Examples> ./tst_mdf4arrayread
//
// Corpus expectations are those of mdf4-parser's tests/corpus_array.cpp and
// its channel-array plan's fixture table, never this unit's output.

#include "sessions/mdf4arrayread.h"

#include <QDir>
#include <QFileInfo>
#include <QList>
#include <QString>
#include <QTest>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

namespace {

using mdf4array::AxisLabels;
using mdf4array::AxisPairing;
using mdf4array::AxisSource;
using u64 = std::uint64_t;

constexpr u64 kAll = std::numeric_limits<u64>::max();
constexpr double kInf = std::numeric_limits<double>::infinity();

// A request as the reader received it. readArray: group, channel, first and
// count samples, the element window's first and count. scan: group, channel,
// first and count samples.
using ArrayRequest = std::tuple<std::uint32_t, std::uint32_t, u64, u64, u64, u64>;
using ScanRequest = std::tuple<std::uint32_t, std::uint32_t, u64, u64>;

// makeDocument()'s groups, and the channels of its first group.
constexpr std::uint32_t kArrays = 0;
constexpr std::uint32_t kClasses = 1;
constexpr std::uint32_t kSpeeds = 2;
constexpr std::uint32_t kCounts = 3;
constexpr std::uint32_t kCube = 1;
constexpr std::uint32_t kMap = 2;
constexpr std::uint32_t kLocal = 3;
constexpr std::uint32_t kCurve = 4;
constexpr std::uint32_t kHyper = 5;

// Appends a decodable array channel of dimension sizes `sizes`, outer first.
mdf4::ChannelArray* addArray(mdf4::ChannelGroup* group, const char* name, std::uint32_t caType,
                             std::initializer_list<u64> sizes) {
    mdf4::Channel* channel = group->add_channels();
    channel->set_name(name);
    channel->set_sample_count(group->cycle_count());
    channel->set_decodable(true);
    mdf4::ChannelArray* array = channel->mutable_array();
    array->set_ca_type(caType);
    u64 elements = 1;
    for (const u64 size : sizes) {
        array->add_dimensions()->set_size(size);
        elements *= size;
    }
    array->set_element_count(elements);
    return array;
}

// Labels dimension `d` of `array` with the axis channel at (group, channel).
void setAxisChannel(mdf4::ChannelArray* array, int d, std::uint32_t group, std::uint32_t channel) {
    mdf4::ArrayDimension* dimension = array->mutable_dimensions(d);
    dimension->set_axis_from_channel(true);
    dimension->set_axis_group(group);
    dimension->set_axis_channel(channel);
}

void addMaster(mdf4::ChannelGroup* group) {
    mdf4::Channel* master = group->add_channels();
    master->set_name("t");
    master->set_sample_count(group->cycle_count());
    master->set_is_master(true);
    master->set_sync_type(1);
    master->set_cn_type(2);
    master->set_decodable(true);
}

// Arrays: 4 cycles behind its time master; Cube 2 x 3 x 4 and Hyper
// 2 x 3 x 3 x 4 on index axes, Map 3 x 2 on the interval axis Classes and the
// same-group scaling axis Local, Curve 3 x 4 on Speed and Count. Classes: one
// cycle, no master, 3 left-open intervals of 4 borders. Speeds: 3 cycles
// behind a time master. Counts: 4 cycles, no master.
mdf4::File makeDocument() {
    mdf4::File document;
    mdf4::ChannelGroup* arrays = document.add_groups();
    arrays->set_name("Arrays");
    arrays->set_cycle_count(4);
    addMaster(arrays);
    addArray(arrays, "Cube", 0, {2, 3, 4});
    mdf4::ChannelArray* map = addArray(arrays, "Map", 2, {3, 2});
    setAxisChannel(map, 0, kClasses, 0);
    setAxisChannel(map, 1, kArrays, kLocal);
    addArray(arrays, "Local", 1, {2});
    mdf4::ChannelArray* curve = addArray(arrays, "Curve", 2, {3, 4});
    setAxisChannel(curve, 0, kSpeeds, 1);
    setAxisChannel(curve, 1, kCounts, 0);
    addArray(arrays, "Hyper", 0, {2, 3, 3, 4});

    mdf4::ChannelGroup* classes = document.add_groups();
    classes->set_name("Classes");
    classes->set_cycle_count(1);
    addArray(classes, "Classes", 3, {4})->set_left_open(true);

    mdf4::ChannelGroup* speeds = document.add_groups();
    speeds->set_name("Speeds");
    speeds->set_cycle_count(3);
    addMaster(speeds);
    addArray(speeds, "Speed", 1, {3});

    mdf4::ChannelGroup* counts = document.add_groups();
    counts->set_name("Counts");
    counts->set_cycle_count(4);
    addArray(counts, "Count", 1, {4});
    return document;
}

// A fake reader over makeDocument(), recording every request. A group
// delivers its cycle count of samples, at most `available`; Speeds' sample j
// is at speedTimes[j], every other group's at j. An array's element k of
// sample s is 1000 s + k, an axis channel's (ca_type 1 or 3) 10 s + k. A
// request of the failing channel returns its outcome.
struct FakeSource {
    struct Failure {
        std::uint32_t group;
        std::uint32_t channel;
        mdf4::Status status;
        const char* message;
    };

    mdf4::File document = makeDocument();
    u64 available = kAll;
    std::vector<double> speedTimes{0.5, 1.5, 2.5};
    std::optional<Failure> failure;
    QList<ArrayRequest> arrays;
    QList<ScanRequest> scans;

    mdf4array::AxisFunction axis() const {
        return [this](std::uint32_t group) {
            const mdf4::ChannelGroup& source = document.groups(static_cast<int>(group));
            const bool master = source.channels_size() > 0 && source.channels(0).is_master();
            return mdf4::Axis{master ? mdf4::AxisKind::Master : mdf4::AxisKind::SampleIndex, group, 0};
        };
    }

    mdf4array::ArrayPlan plan(std::uint32_t channel, u64 sample, const std::vector<u64>& slice = {}) const {
        return mdf4array::arrayPlan(document, axis(), kArrays, channel, sample, slice);
    }

    mdf4array::ArraySample read(const mdf4array::ArrayPlan& plan) {
        return mdf4array::readArraySample(
            plan,
            [this](std::uint32_t group, std::uint32_t channel, u64 first, u64 count,
                   const mdf4::ElementWindow& window, const mdf4::Control&) {
                return readArray(group, channel, first, count, window);
            },
            [this](std::uint32_t group, std::uint32_t channel, u64 first, u64 count, const mdf4::Control&,
                   const mdf4::Visitor& visitor) { return scan(group, channel, first, count, visitor); },
            mdf4::Control{});
    }

    double time(std::uint32_t group, u64 sample) const {
        return group == kSpeeds ? speedTimes[static_cast<std::size_t>(sample)] : static_cast<double>(sample);
    }

    // The failure's outcome, or Ok with the request clamped to the samples the
    // group delivers.
    bool admit(std::uint32_t group, std::uint32_t channel, u64 first, u64 count, mdf4::ScanResult& result) const {
        if (failure && failure->group == group && failure->channel == channel) {
            result.outcome.status = failure->status;
            result.outcome.message = failure->message;
            return false;
        }
        const u64 samples = std::min<u64>(document.groups(static_cast<int>(group)).cycle_count(), available);
        result.outcome.status = mdf4::Status::Ok;
        result.outcome.message = "complete";
        result.firstSample = std::min(first, samples);
        result.sampleCount = std::min(count, samples - result.firstSample);
        result.samplesRead = result.sampleCount;
        return true;
    }

    mdf4::ReadArrayResult readArray(std::uint32_t group, std::uint32_t channel, u64 first, u64 count,
                                    const mdf4::ElementWindow& window) {
        arrays.push_back({group, channel, first, count, window.first, window.count});
        mdf4::ReadArrayResult result;
        if (!admit(group, channel, first, count, result)) {
            return result;
        }
        const mdf4::ChannelArray& array =
            document.groups(static_cast<int>(group)).channels(static_cast<int>(channel)).array();
        const double weight = array.ca_type() == 1 || array.ca_type() == 3 ? 10.0 : 1000.0;
        const u64 elementFirst = std::min<u64>(window.first, array.element_count());
        const u64 elements = std::min<u64>(window.count, array.element_count() - elementFirst);
        result.series.firstElement = elementFirst;
        result.series.elements = elements;
        result.series.time.reserve(static_cast<std::size_t>(result.samplesRead));
        result.series.value.reserve(static_cast<std::size_t>(result.samplesRead * elements));
        for (u64 s = result.firstSample; s < result.firstSample + result.samplesRead; ++s) {
            result.series.time.push_back(time(group, s));
            for (u64 k = elementFirst; k < elementFirst + elements; ++k) {
                result.series.value.push_back(weight * static_cast<double>(s) + static_cast<double>(k));
            }
        }
        return result;
    }

    // Delivers in chunks of two samples.
    mdf4::ScanResult scan(std::uint32_t group, std::uint32_t channel, u64 first, u64 count,
                          const mdf4::Visitor& visitor) {
        scans.push_back({group, channel, first, count});
        mdf4::ScanResult result;
        if (!admit(group, channel, first, count, result)) {
            return result;
        }
        std::vector<double> times;
        for (u64 s = result.firstSample; s < result.firstSample + result.samplesRead; ++s) {
            times.push_back(time(group, s));
        }
        for (std::size_t at = 0; at < times.size(); at += 2) {
            visitor(mdf4::SampleChunk{result.firstSample + at, times.data() + at, times.data() + at,
                                      std::min<std::size_t>(2, times.size() - at)});
        }
        return result;
    }
};

// The examples directory MDF4_EXAMPLES names; empty when it names none.
QString examplesDirectory() {
    const QString directory = qEnvironmentVariable("MDF4_EXAMPLES");
    return !directory.isEmpty() && QFileInfo(directory).isDir() ? directory : QString();
}

// One corpus file opened by the real reader, behind the unit's function
// shapes, recording every request.
struct CorpusFile {
    CorpusFile(const QString& directory, const char* path)
        : reader(QDir(directory).filePath(QString::fromUtf8(path)).toStdString()) {}

    mdf4array::ArrayPlan plan(std::uint32_t group, std::uint32_t channel, u64 sample,
                              const std::vector<u64>& slice = {}) const {
        return mdf4array::arrayPlan(
            reader.metadata(), [this](std::uint32_t axisGroup) { return reader.axis(axisGroup); }, group,
            channel, sample, slice);
    }

    mdf4array::ArraySample read(const mdf4array::ArrayPlan& plan) {
        return mdf4array::readArraySample(
            plan,
            [this](std::uint32_t group, std::uint32_t channel, u64 first, u64 count,
                   const mdf4::ElementWindow& window, const mdf4::Control& control) {
                arrays.push_back({group, channel, first, count, window.first, window.count});
                return reader.readArray(group, channel, first, count, window, control);
            },
            [this](std::uint32_t group, std::uint32_t channel, u64 first, u64 count, const mdf4::Control& control,
                   const mdf4::Visitor& visitor) {
                scans.push_back({group, channel, first, count});
                return reader.scan(group, channel, first, count, control, visitor);
            },
            mdf4::Control{});
    }

    const mdf4::Channel& channel(std::uint32_t group, std::uint32_t channel) const {
        return reader.metadata().groups(static_cast<int>(group)).channels(static_cast<int>(channel));
    }

    mdf4::Reader reader;
    QList<ArrayRequest> arrays;
    QList<ScanRequest> scans;
};

} // namespace

class TestMdf4ArrayRead : public QObject {
    Q_OBJECT

private slots:
    void sliceSelectsTheWindow();
    void reservationCountsValuesAndLabels();
    void axisPairing();
    void axisReadFailures();
    void classificationResult();
    void channelAxisBeforeItsFirstSample();
    void commonMasterPairing();
    void slicesOfARealArray();
};

// Case 10: a 2 x 3 x 4 array shows its last two dimensions as 3 rows x 4
// columns; slice 1 of the outer dimension is the element window {12, 12}.
void TestMdf4ArrayRead::sliceSelectsTheWindow() {
    FakeSource source;
    const mdf4array::ArrayPlan plan = source.plan(kCube, 2, {1});
    QCOMPARE(plan.group, kArrays);
    QCOMPARE(plan.channel, kCube);
    QCOMPARE(plan.sample, u64{2});
    QCOMPARE(plan.rows, u64{3});
    QCOMPARE(plan.columns, u64{4});
    QCOMPARE(plan.slice, std::vector<u64>{1});
    QCOMPARE(plan.window.first, u64{12});
    QCOMPARE(plan.window.count, u64{12});
    QCOMPARE(plan.rowAxis.kind, AxisSource::Kind::Index);
    QCOMPARE(plan.columnAxis.kind, AxisSource::Kind::Index);

    // An index past its dimension is clamped, a missing one is 0.
    const mdf4array::ArrayPlan clamped = source.plan(kCube, 0, {7});
    QCOMPARE(clamped.slice, std::vector<u64>{1});
    QCOMPARE(clamped.window.first, u64{12});
    const mdf4array::ArrayPlan first = source.plan(kCube, 0);
    QCOMPARE(first.slice, std::vector<u64>{0});
    QCOMPARE(first.window.first, u64{0});
    QCOMPARE(first.window.count, u64{12});

    // The outer indices are row-major, outer first: slice (1, 0) of
    // 2 x 3 x 3 x 4 starts at element 36.
    const mdf4array::ArrayPlan hyper = source.plan(kHyper, 0, {1, 0});
    QCOMPARE(hyper.slice, (std::vector<u64>{1, 0}));
    QCOMPARE(hyper.window.first, u64{36});
    QCOMPARE(hyper.window.count, u64{12});

    // One dimension: N rows, one column, the whole array.
    const mdf4array::ArrayPlan local = source.plan(kLocal, 0);
    QCOMPARE(local.rows, u64{2});
    QCOMPARE(local.columns, u64{1});
    QVERIFY(local.slice.empty());
    QCOMPARE(local.window.first, u64{0});
    QCOMPARE(local.window.count, u64{2});

    // The worker reads that window of the sample: rows x columns, row-major.
    const mdf4array::ArraySample sample = source.read(plan);
    QCOMPARE(source.arrays, (QList<ArrayRequest>{{kArrays, kCube, 2, 1, 12, 12}}));
    QVERIFY(source.scans.isEmpty());
    QCOMPARE(sample.outcome.status, mdf4::Status::Ok);
    QCOMPARE(sample.samplesRead, u64{1});
    QCOMPARE(sample.time, 2.0);
    QCOMPARE(sample.rows, u64{3});
    QCOMPARE(sample.columns, u64{4});
    QCOMPARE(sample.values.size(), std::size_t{12});
    QCOMPARE(sample.values[0], 2012.0);          // (0, 0): k 12
    QCOMPARE(sample.values[1 * 4 + 2], 2018.0);  // (1, 2): k 18
    QCOMPARE(sample.values[2 * 4 + 3], 2023.0);  // (2, 3): k 23
    QCOMPARE(sample.rowLabels.kind, AxisLabels::Kind::Index);
    QCOMPARE(sample.columnLabels.kind, AxisLabels::Kind::Index);
}

// The reservation: the structure, the values, a time and both axes' labels at
// 8 Bytes each, saturating; the sample read stays within it.
void TestMdf4ArrayRead::reservationCountsValuesAndLabels() {
    FakeSource source;
    const mdf4array::ArrayPlan cube = source.plan(kCube, 0, {1});
    QCOMPARE(mdf4array::reservation(cube), sizeof(mdf4array::ArraySample) + 8 * (12 + 1));
    const mdf4array::ArrayPlan map = source.plan(kMap, 1);
    QCOMPARE(map.rowAxis.labels, u64{4});     // 3 intervals: 4 borders
    QCOMPARE(map.columnAxis.labels, u64{2});  // 2 points
    QCOMPARE(mdf4array::reservation(map), sizeof(mdf4array::ArraySample) + 8 * (6 + 1 + 4 + 2));
    QVERIFY(source.read(cube).bytes() <= mdf4array::reservation(cube));
    const mdf4array::ArraySample labelled = source.read(map);
    QCOMPARE(labelled.rowLabels.values.size(), std::size_t{4});
    QCOMPARE(labelled.bytes(), sizeof(mdf4array::ArraySample) + 8 * (6 + 4 + 2));
    QVERIFY(labelled.bytes() <= mdf4array::reservation(map));

    // 2^32 x (2^32 - 1) elements fit a count, not 8 Bytes each.
    mdf4::File huge;
    mdf4::ChannelGroup* group = huge.add_groups();
    group->set_cycle_count(1);
    addArray(group, "Huge", 0, {u64{1} << 32, (u64{1} << 32) - 1});
    const auto sampleIndex = [](std::uint32_t g) { return mdf4::Axis{mdf4::AxisKind::SampleIndex, g, 0}; };
    QCOMPARE(mdf4array::reservation(mdf4array::arrayPlan(huge, sampleIndex, 0, 0, 0, {})), kAll);
}

// Case 11: the axis rule. Map's rows are labelled by Classes, one cycle
// without a master, read at sample 0 for every array sample; its columns by
// Local, in the array's own group, read at the array's sample. Curve's rows by
// Speed, whose group has a time master as Arrays has: the latest Speed sample
// at or before the array sample's time (0.5, 1.5, 2.5 against 0, 1, 2, 3),
// none before the first; its columns by Count, which has no master.
void TestMdf4ArrayRead::axisPairing() {
    FakeSource source;
    const mdf4array::ArrayPlan map = source.plan(kMap, 0);
    QCOMPARE(map.rows, u64{3});
    QCOMPARE(map.columns, u64{2});
    QCOMPARE(map.rowAxis.kind, AxisSource::Kind::Channel);
    QCOMPARE(map.rowAxis.group, kClasses);
    QCOMPARE(map.rowAxis.channel, std::uint32_t{0});
    QCOMPARE(map.rowAxis.pairing, AxisPairing::Constant);
    QVERIFY(map.rowAxis.intervals);
    QVERIFY(map.rowAxis.leftOpen);
    QCOMPARE(map.columnAxis.kind, AxisSource::Kind::Channel);
    QCOMPARE(map.columnAxis.group, kArrays);
    QCOMPARE(map.columnAxis.channel, kLocal);
    QCOMPARE(map.columnAxis.pairing, AxisPairing::Same);
    QVERIFY(!map.columnAxis.intervals);

    for (const u64 s : {u64{0}, u64{3}}) {
        source.arrays.clear();
        const mdf4array::ArraySample sample = source.read(source.plan(kMap, s));
        QCOMPARE(sample.outcome.status, mdf4::Status::Ok);
        QCOMPARE(source.arrays, (QList<ArrayRequest>{{kArrays, kMap, s, 1, 0, 6},
                                                     {kClasses, 0, 0, 1, 0, 4},
                                                     {kArrays, kLocal, s, 1, 0, 2}}));
        QVERIFY(source.scans.isEmpty());
        QCOMPARE(sample.values.size(), std::size_t{6});
        // The first interval is ]0, 1].
        QCOMPARE(sample.rowLabels.kind, AxisLabels::Kind::Intervals);
        QVERIFY(sample.rowLabels.leftOpen);
        QCOMPARE(sample.rowLabels.values, (std::vector<double>{0, 1, 2, 3}));
        QCOMPARE(sample.columnLabels.kind, AxisLabels::Kind::Points);
        QCOMPARE(sample.columnLabels.values,
                 (std::vector<double>{10.0 * static_cast<double>(s), 10.0 * static_cast<double>(s) + 1}));
        QVERIFY(sample.rowLabels.note.empty());
        QVERIFY(sample.columnLabels.note.empty());
    }

    const mdf4array::ArrayPlan curve = source.plan(kCurve, 0);
    QCOMPARE(curve.rowAxis.pairing, AxisPairing::CommonMaster);
    QCOMPARE(curve.rowAxis.master.kind, mdf4::AxisKind::Master);
    QCOMPARE(curve.rowAxis.master.group, kSpeeds);
    QCOMPARE(curve.rowAxis.master.channel, std::uint32_t{0});
    QCOMPARE(curve.rowAxis.cycleCount, u64{3});
    QCOMPARE(curve.columnAxis.pairing, AxisPairing::None);

    // At time 0 no Speed sample is at or before; the values still show.
    source.arrays.clear();
    const mdf4array::ArraySample early = source.read(curve);
    QCOMPARE(early.outcome.status, mdf4::Status::Ok);
    QCOMPARE(source.scans, (QList<ScanRequest>{{kSpeeds, 0, 0, 3}}));
    QCOMPARE(source.arrays, (QList<ArrayRequest>{{kArrays, kCurve, 0, 1, 0, 12}}));
    QCOMPARE(early.values.size(), std::size_t{12});
    QCOMPARE(early.rowLabels.kind, AxisLabels::Kind::Index);
    QVERIFY(early.rowLabels.values.empty());
    QCOMPARE(QString::fromStdString(early.rowLabels.note), QStringLiteral("no Speed sample at or before this one"));
    QCOMPARE(early.columnLabels.kind, AxisLabels::Kind::Index);
    QCOMPARE(QString::fromStdString(early.columnLabels.note), QStringLiteral("no common master with Count"));

    // An unnamed axis channel is named as the tree titles it.
    source.document.mutable_groups(static_cast<int>(kCounts))->mutable_channels(0)->clear_name();
    const mdf4array::ArraySample unnamed = source.read(source.plan(kCurve, 0));
    QCOMPARE(QString::fromStdString(unnamed.columnLabels.note), QStringLiteral("no common master with Channel 1"));

    // At time 1 the Speed sample at 0.5, at time 3 the one at 2.5.
    for (const auto& [s, j] : {std::pair<u64, u64>{1, 0}, std::pair<u64, u64>{3, 2}}) {
        source.arrays.clear();
        source.scans.clear();
        const mdf4array::ArraySample sample = source.read(source.plan(kCurve, s));
        QCOMPARE(sample.outcome.status, mdf4::Status::Ok);
        QCOMPARE(source.scans, (QList<ScanRequest>{{kSpeeds, 0, 0, 3}}));
        QCOMPARE(source.arrays, (QList<ArrayRequest>{{kArrays, kCurve, s, 1, 0, 12}, {kSpeeds, 1, j, 1, 0, 3}}));
        const double label = 10.0 * static_cast<double>(j);
        QCOMPARE(sample.rowLabels.kind, AxisLabels::Kind::Points);
        QCOMPARE(sample.rowLabels.values, (std::vector<double>{label, label + 1, label + 2}));
        QVERIFY(sample.rowLabels.note.empty());
    }

    // Equal times: the latest sample.
    source.speedTimes = {0.5, 1.5, 1.5};
    source.arrays.clear();
    source.read(source.plan(kCurve, 3));
    QCOMPARE(source.arrays.size(), 2);
    QCOMPARE(source.arrays.at(1), (ArrayRequest{kSpeeds, 1, 2, 1, 0, 3}));

    // A sample past the recording's end reads no axis.
    source.available = 2;
    source.arrays.clear();
    source.scans.clear();
    const mdf4array::ArraySample past = source.read(source.plan(kMap, 3));
    QCOMPARE(past.outcome.status, mdf4::Status::Ok);
    QCOMPARE(past.samplesRead, u64{0});
    QVERIFY(past.values.empty());
    QCOMPARE(source.arrays, (QList<ArrayRequest>{{kArrays, kMap, 3, 1, 0, 6}}));
    QVERIFY(source.scans.isEmpty());
}

// An axis whose read or master scan fails shows index headers with the
// reader's message beside the values; a Cancelled or SourceChanged outcome of
// any read is the sample's, and so is the array read's own failure.
void TestMdf4ArrayRead::axisReadFailures() {
    FakeSource source;
    source.failure = FakeSource::Failure{kClasses, 0, mdf4::Status::Error, "data block unreadable"};
    const mdf4array::ArraySample unreadable = source.read(source.plan(kMap, 1));
    QCOMPARE(unreadable.outcome.status, mdf4::Status::Ok);
    QCOMPARE(unreadable.values.size(), std::size_t{6});
    QCOMPARE(unreadable.rowLabels.kind, AxisLabels::Kind::Index);
    QVERIFY(unreadable.rowLabels.values.empty());
    QCOMPARE(QString::fromStdString(unreadable.rowLabels.note), QStringLiteral("data block unreadable"));
    QCOMPARE(unreadable.columnLabels.kind, AxisLabels::Kind::Points);
    QCOMPARE(unreadable.columnLabels.values, (std::vector<double>{10, 11}));

    source.failure = FakeSource::Failure{kSpeeds, 0, mdf4::Status::ResourceLimit, "scratch allowance exceeded"};
    source.arrays.clear();
    const mdf4array::ArraySample refused = source.read(source.plan(kCurve, 3));
    QCOMPARE(refused.outcome.status, mdf4::Status::Ok);
    QCOMPARE(refused.values.size(), std::size_t{12});
    QCOMPARE(refused.rowLabels.kind, AxisLabels::Kind::Index);
    QCOMPARE(QString::fromStdString(refused.rowLabels.note), QStringLiteral("scratch allowance exceeded"));
    QCOMPARE(source.arrays, (QList<ArrayRequest>{{kArrays, kCurve, 3, 1, 0, 12}}));

    for (const mdf4::Status status : {mdf4::Status::Cancelled, mdf4::Status::SourceChanged}) {
        source.failure = FakeSource::Failure{kClasses, 0, status, "stopped"};
        source.arrays.clear();
        const mdf4array::ArraySample axisEnded = source.read(source.plan(kMap, 1));
        QCOMPARE(axisEnded.outcome.status, status);
        QCOMPARE(axisEnded.samplesRead, u64{0});
        QVERIFY(axisEnded.values.empty());
        QCOMPARE(source.arrays.size(), 2);  // the column axis is not read

        source.failure = FakeSource::Failure{kSpeeds, 0, status, "stopped"};
        const mdf4array::ArraySample scanEnded = source.read(source.plan(kCurve, 3));
        QCOMPARE(scanEnded.outcome.status, status);
        QVERIFY(scanEnded.values.empty());
    }

    source.failure = FakeSource::Failure{kArrays, kMap, mdf4::Status::Error, "data block unreadable"};
    source.arrays.clear();
    const mdf4array::ArraySample failed = source.read(source.plan(kMap, 1));
    QCOMPARE(failed.outcome.status, mdf4::Status::Error);
    QCOMPARE(QString::fromUtf8(failed.outcome.message), QStringLiteral("data block unreadable"));
    QVERIFY(failed.values.empty());
    QCOMPARE(source.arrays.size(), 1);
}

// Case 20: POR's 2D_classification, 52 x 142 on the interval axes throttle
// (rows) and nmot (columns) in a one-cycle group without a master: both read
// at sample 0, from -inf to +inf, left-closed. [1][0] = 3698.800048828125 and
// the maximum 10697.0595703125 at [26][0] are corpus_array.cpp's spot checks.
void TestMdf4ArrayRead::classificationResult() {
    const QString directory = examplesDirectory();
    if (directory.isEmpty()) {
        QSKIP("MDF4_EXAMPLES names no corpus directory");
    }
    CorpusFile file(directory, "Arrays/Classification/Porsche_2D_classification_result.mf4");
    QVERIFY(file.reader.ready());
    QCOMPARE(QString::fromStdString(file.channel(0, 0).name()), QStringLiteral("2D_classification"));

    const mdf4array::ArrayPlan plan = file.plan(0, 0, 0);
    QCOMPARE(plan.rows, u64{52});
    QCOMPARE(plan.columns, u64{142});
    QCOMPARE(plan.window.first, u64{0});
    QCOMPARE(plan.window.count, u64{52 * 142});
    QCOMPARE(QString::fromStdString(plan.rowAxis.name), QStringLiteral("throttle"));
    QCOMPARE(plan.rowAxis.pairing, AxisPairing::Constant);
    QCOMPARE(QString::fromStdString(plan.columnAxis.name), QStringLiteral("nmot"));
    QCOMPARE(plan.columnAxis.pairing, AxisPairing::Constant);

    const mdf4array::ArraySample sample = file.read(plan);
    QCOMPARE(sample.outcome.status, mdf4::Status::Ok);
    QCOMPARE(sample.samplesRead, u64{1});
    QCOMPARE(sample.time, 0.0);
    QCOMPARE(file.arrays, (QList<ArrayRequest>{{0, 0, 0, 1, 0, 52 * 142}, {1, 1, 0, 1, 0, 53}, {1, 0, 0, 1, 0, 143}}));
    QVERIFY(file.scans.isEmpty());
    QCOMPARE(sample.values.size(), std::size_t{52 * 142});
    QCOMPARE(sample.values[1 * 142 + 0], 3698.800048828125);
    QCOMPARE(sample.values[26 * 142 + 0], 10697.0595703125);
    for (const auto& [labels, borders] :
         {std::pair<const AxisLabels*, std::size_t>{&sample.rowLabels, 53}, {&sample.columnLabels, 143}}) {
        QCOMPARE(labels->kind, AxisLabels::Kind::Intervals);
        QVERIFY(!labels->leftOpen);
        QCOMPARE(labels->values.size(), borders);
        QCOMPARE(labels->values.front(), -kInf);
        QCOMPARE(labels->values.back(), kInf);
        QVERIFY(labels->note.empty());
    }
    QVERIFY(sample.bytes() <= mdf4array::reservation(plan));
}

// Case 21, KF4's part: VMA's KF4 6 x 8 on the fixed points 0...5 and Curve1.
// Curve1's first sample (0.771390207 s) comes after KF4's sample 0
// (0.660880207 s), so its columns show indices with the note, and no Curve1
// array is read. [2][4] is element 20, 24 in sample 0.
void TestMdf4ArrayRead::channelAxisBeforeItsFirstSample() {
    const QString directory = examplesDirectory();
    if (directory.isEmpty()) {
        QSKIP("MDF4_EXAMPLES names no corpus directory");
    }
    CorpusFile file(directory, "Arrays/Simple/Vector_MeasurementArrays.mf4");
    QVERIFY(file.reader.ready());
    QCOMPARE(QString::fromStdString(file.channel(8, 1).name()), QStringLiteral("KF4"));

    const mdf4array::ArrayPlan plan = file.plan(8, 1, 0);
    QCOMPARE(plan.rows, u64{6});
    QCOMPARE(plan.columns, u64{8});
    QCOMPARE(plan.rowAxis.kind, AxisSource::Kind::Fixed);
    QCOMPARE(plan.columnAxis.pairing, AxisPairing::CommonMaster);
    QCOMPARE(QString::fromStdString(plan.columnAxis.name), QStringLiteral("Curve1"));

    const mdf4array::ArraySample sample = file.read(plan);
    QCOMPARE(sample.outcome.status, mdf4::Status::Ok);
    QCOMPARE(sample.time, 0.660880207);
    const mdf4::Axis curve1 = file.reader.axis(9);
    QCOMPARE(file.scans, (QList<ScanRequest>{{curve1.group, curve1.channel, 0, 4}}));
    QCOMPARE(file.arrays, (QList<ArrayRequest>{{8, 1, 0, 1, 0, 48}}));
    QCOMPARE(sample.rowLabels.kind, AxisLabels::Kind::Points);
    QCOMPARE(sample.rowLabels.values, (std::vector<double>{0, 1, 2, 3, 4, 5}));
    QCOMPARE(sample.columnLabels.kind, AxisLabels::Kind::Index);
    QVERIFY(sample.columnLabels.values.empty());
    QCOMPARE(QString::fromStdString(sample.columnLabels.note),
             QStringLiteral("no Curve1 sample at or before this one"));
    QCOMPARE(sample.values.size(), std::size_t{48});
    QCOMPARE(sample.values[2 * 8 + 4], 24.0);
}

// Case 22: VMA's KF2 8 x 8 on Curve2 (rows) and Curve1 (columns), each in a
// group with its own time master. KF2's sample 3 (15.020245207 s) pairs with
// Curve2's sample 2: Curve2's sample 3 (15.160142207 s) comes after it, and
// the spec's rule gives KF2 sample s the Curve2 sample s - 1. Curve2's samples
// hold the same points 11 ... 22, so the request tells them apart.
void TestMdf4ArrayRead::commonMasterPairing() {
    const QString directory = examplesDirectory();
    if (directory.isEmpty()) {
        QSKIP("MDF4_EXAMPLES names no corpus directory");
    }
    CorpusFile file(directory, "Arrays/Simple/Vector_MeasurementArrays.mf4");
    QVERIFY(file.reader.ready());
    QCOMPARE(QString::fromStdString(file.channel(0, 1).name()), QStringLiteral("KF2"));

    const mdf4array::ArrayPlan plan = file.plan(0, 1, 3);
    QCOMPARE(QString::fromStdString(plan.rowAxis.name), QStringLiteral("Curve2"));
    QCOMPARE(plan.rowAxis.pairing, AxisPairing::CommonMaster);
    QCOMPARE(QString::fromStdString(plan.columnAxis.name), QStringLiteral("Curve1"));
    QCOMPARE(plan.columnAxis.pairing, AxisPairing::CommonMaster);

    const mdf4array::ArraySample sample = file.read(plan);
    QCOMPARE(sample.outcome.status, mdf4::Status::Ok);
    QCOMPARE(sample.time, 15.020245207);
    const mdf4::Axis curve2 = file.reader.axis(1);
    const mdf4::Axis curve1 = file.reader.axis(9);
    QCOMPARE(file.scans, (QList<ScanRequest>{{curve2.group, curve2.channel, 0, 4}, {curve1.group, curve1.channel, 0, 4}}));
    QCOMPARE(file.arrays.size(), 3);
    QCOMPARE(file.arrays.at(0), (ArrayRequest{0, 1, 3, 1, 0, 64}));
    QCOMPARE(file.arrays.at(1), (ArrayRequest{1, 1, 2, 1, 0, 8}));
    QCOMPARE(sample.rowLabels.kind, AxisLabels::Kind::Points);
    QCOMPARE(sample.rowLabels.values.size(), std::size_t{8});
    QCOMPARE(sample.rowLabels.values.front(), 11.0);
    QCOMPARE(sample.rowLabels.values.back(), 22.0);
    // Curve1's sample 3 (15.669402207 s) comes after KF2's sample 3 as well.
    QCOMPARE(std::get<0>(file.arrays.at(2)), std::uint32_t{9});
    QVERIFY(std::get<2>(file.arrays.at(2)) < 3);
    QCOMPARE(sample.columnLabels.kind, AxisLabels::Kind::Points);
    QCOMPARE(sample.columnLabels.values.size(), std::size_t{8});
}

// dSPACE's Signal_MatrixDim, 2 x 3 x 4 on index axes, whose element
// [i1][i2][i3] of sample s holds i1 + 0.1 i2 + 0.01 i3 + 0.001 s: slice 0 of
// sample 0 at (0, 1) is 0.01, slice 1 at (0, 0) is 1, and sample 4's slice 1
// at (2, 3) is 1.234.
void TestMdf4ArrayRead::slicesOfARealArray() {
    const QString directory = examplesDirectory();
    if (directory.isEmpty()) {
        QSKIP("MDF4_EXAMPLES names no corpus directory");
    }
    CorpusFile file(directory, "Arrays/Simple/dSPACE_MeasurementArrays.mf4");
    QVERIFY(file.reader.ready());
    QCOMPARE(QString::fromStdString(file.channel(0, 2).name()), QStringLiteral("Signal_MatrixDim"));

    const mdf4array::ArrayPlan outer0 = file.plan(0, 2, 0, {0});
    QCOMPARE(outer0.rows, u64{3});
    QCOMPARE(outer0.columns, u64{4});
    const mdf4array::ArraySample slice0 = file.read(outer0);
    QCOMPARE(slice0.outcome.status, mdf4::Status::Ok);
    QCOMPARE(slice0.values.size(), std::size_t{12});
    QCOMPARE(slice0.values[0 * 4 + 1], 0.01);
    QCOMPARE(slice0.rowLabels.kind, AxisLabels::Kind::Index);
    QCOMPARE(slice0.columnLabels.kind, AxisLabels::Kind::Index);

    const mdf4array::ArraySample slice1 = file.read(file.plan(0, 2, 0, {1}));
    QCOMPARE(slice1.values.size(), std::size_t{12});
    QCOMPARE(slice1.values[0 * 4 + 0], 1.0);

    const mdf4array::ArraySample last = file.read(file.plan(0, 2, 4, {1}));
    QCOMPARE(last.outcome.status, mdf4::Status::Ok);
    QCOMPARE(last.time, 0.004);
    QCOMPARE(last.values.size(), std::size_t{12});
    QCOMPARE(last.values[2 * 4 + 3], 1.234);

    QCOMPARE(file.arrays, (QList<ArrayRequest>{{0, 2, 0, 1, 0, 12}, {0, 2, 0, 1, 12, 12}, {0, 2, 4, 1, 12, 12}}));
    QVERIFY(file.scans.isEmpty());
}

int main(int argc, char* argv[]) {
    TestMdf4ArrayRead testCase;
    const int failures = QTest::qExec(&testCase, argc, argv);
    if (failures == 0 && examplesDirectory().isEmpty()) {
        qInfo("MDF4_EXAMPLES names no corpus directory - the corpus cases skipped");
        return 77;
    }
    return failures;
}

#include "tst_mdf4arrayread.moc"
