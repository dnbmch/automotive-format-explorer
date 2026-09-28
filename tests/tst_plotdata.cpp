// The plot data builders, fed as a scan feeds them: ordered chunks of absolute
// sample indices with their coordinates and values.

#include "plotscan.h"

#include <QTest>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <random>
#include <utility>
#include <vector>

using namespace plotscan;

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr std::uint64_t kTop = std::numeric_limits<std::uint64_t>::max();

// Bins are consecutive runs within aligned runs of 2^binShift indices.
bool partitioned(const PlotOverview& overview, std::uint64_t first, std::uint64_t count) {
    std::uint64_t next = first;
    for (std::size_t b = 0; b < overview.bins.size(); ++b) {
        const PlotBin& bin = overview.bins[b];
        const std::uint64_t last = bin.firstSample + (bin.sampleCount - 1);
        if (bin.firstSample != next || bin.sampleCount == 0 ||
            (bin.firstSample >> overview.binShift) != (last >> overview.binShift)) {
            return false;
        }
        next = bin.firstSample + bin.sampleCount;
    }
    return next == first + count;
}

} // namespace

class TestPlotData : public QObject {
    Q_OBJECT

private slots:
    void partitionsIntoAlignedBins();
    void smallSignalKeepsEverySampleInItsOwnBin();
    void spikeBeyondExactPrefixAppearsInOverview();
    void lateRegressionForcesIndexDomain();
    void nonfiniteCoordinateForcesIndexDomain();
    void nonfiniteValuesStayOutOfExtrema();
    void shortScanIsIncompleteAtFullResolution();
    void emptyScanHasNoBins();
    void farIrregularWindowHoldsEveryEligibleSample();
    void equalTimestampsAcrossBinsStayTogether();
    void rangeInGapHoldsBothNeighbors();
    void binAcrossGapRecordsItsWidth();
    void windowRefusedBeyondItsLimit();
    void windowFailsWhenSamplesContradictOverview();
    void partitionMathNearTwoToThe64();
    void storageIsReservedUpFront();
};

// Ten million regular samples: at most 4,096 bins and more than half of them,
// each a consecutive run inside one aligned run of indices.
void TestPlotData::partitionsIntoAlignedBins() {
    constexpr std::uint64_t count = 10'000'000;
    const PlotOverviewPtr overview = overviewOf(count, regular(1e-3));
    QVERIFY(overview->bins.size() <= kPlotOverviewBins);
    QVERIFY(overview->bins.size() > kPlotOverviewBins / 2);
    QVERIFY(partitioned(*overview, 0, count));
    for (std::size_t b = 1; b < overview->bins.size(); ++b) {
        QVERIFY((overview->bins[b].firstSample >> overview->binShift) !=
                (overview->bins[b - 1].firstSample >> overview->binShift));
    }
    QVERIFY(overview->domain == PlotDomain::Time);
    QCOMPARE(overview->sampleCount, count);
    QVERIFY(!overview->incomplete());
    const PlotBin& middle = overview->bins[1000];
    QCOMPARE(middle.firstDomain, static_cast<double>(middle.firstSample) * 1e-3);
    QCOMPARE(middle.lastDomain,
             static_cast<double>(middle.firstSample + middle.sampleCount - 1) * 1e-3);
    QCOMPARE(overview->domainEnd(), static_cast<double>(count - 1) * 1e-3);
    QVERIFY(overview->minimumSpacing > 0.9e-3 && overview->minimumSpacing < 1.1e-3);
    QVERIFY(overview->maximum <= 1.0 && overview->maximum > 0.999);
}

void TestPlotData::smallSignalKeepsEverySampleInItsOwnBin() {
    const PlotOverviewPtr overview = overviewOf(10, regular(0.5));
    QCOMPARE(overview->bins.size(), std::size_t(10));
    QCOMPARE(overview->binShift, 0u);
    for (std::size_t b = 0; b < 10; ++b) {
        QCOMPARE(overview->bins[b].firstSample, std::uint64_t(b));
        QCOMPARE(overview->bins[b].sampleCount, std::uint64_t(1));
        QCOMPARE(overview->bins[b].minimum, overview->bins[b].maximum);
    }
    QCOMPARE(overview->minimumSpacing, 0.5);
}

// A spike past the first 4 Mi samples, the most one exact window holds.
void TestPlotData::spikeBeyondExactPrefixAppearsInOverview() {
    constexpr std::uint64_t count = kPlotWindowSamples + (std::uint64_t{1} << 20);
    constexpr std::uint64_t spike = kPlotWindowSamples + 300'001;
    const PlotOverviewPtr overview = overviewOf(count, [](std::uint64_t index) {
        const double value = index == spike ? 1000.0 : index == spike + 7 ? -500.0 : 0.0;
        return std::make_pair(static_cast<double>(index), value);
    });
    QCOMPARE(overview->maximum, 1000.0);
    QCOMPARE(overview->minimum, -500.0);
    const auto holds = std::find_if(overview->bins.begin(), overview->bins.end(),
                                    [](const PlotBin& bin) {
                                        return bin.firstSample <= spike &&
                                               spike < bin.firstSample + bin.sampleCount;
                                    });
    QVERIFY(holds != overview->bins.end());
    QCOMPARE(holds->maximum, 1000.0);
}

// One coordinate going back at the very end makes the whole result set plot
// against sample indices: the bins and every window of the overview.
void TestPlotData::lateRegressionForcesIndexDomain() {
    constexpr std::uint64_t count = 200'000;
    const Samples samples = [](std::uint64_t index) {
        const double time = index == count - 10 ? 1.0 : static_cast<double>(index) * 0.01;
        return std::make_pair(time, static_cast<double>(index % 17));
    };
    const PlotOverviewPtr overview = overviewOf(count, samples);
    QVERIFY(overview->domain == PlotDomain::Index);
    QCOMPARE(overview->minimumSpacing, 1.0);
    QCOMPARE(overview->domainStart(), 0.0);
    QCOMPARE(overview->domainEnd(), static_cast<double>(count - 1));
    for (const PlotBin& bin : overview->bins) {
        QCOMPARE(bin.firstDomain, static_cast<double>(bin.firstSample));
        QCOMPARE(bin.lastDomain, static_cast<double>(bin.firstSample + bin.sampleCount - 1));
    }

    const PlotWindowRequest request = overview->windowRequest(1000.5, 2000.5);
    QVERIFY(request.domain == PlotDomain::Index);
    const PlotWindowPtr window = windowOf(request, samples);
    QVERIFY(window);
    QVERIFY(window->domain == PlotDomain::Index);
    QCOMPARE(window->firstSample, std::uint64_t(1000));
    QCOMPARE(window->time.size(), std::size_t(1002));
    for (std::size_t i = 0; i < window->time.size(); ++i) {
        QCOMPARE(window->time[i], static_cast<double>(1000 + i));
        QCOMPARE(window->value[i], static_cast<double>((1000 + i) % 17));
    }
    QVERIFY(window->covers(1000.5, 2000.5));
}

void TestPlotData::nonfiniteCoordinateForcesIndexDomain() {
    const PlotOverviewPtr overview = overviewOf(100, [](std::uint64_t index) {
        return std::make_pair(index == 5 ? kNaN : static_cast<double>(index), 1.0);
    });
    QVERIFY(overview->domain == PlotDomain::Index);
    QCOMPARE(overview->bins[5].firstDomain, 5.0);
}

// Nonfinite values keep their slots and counts but never reach the extrema; a
// bin of nothing else has no extrema at all.
void TestPlotData::nonfiniteValuesStayOutOfExtrema() {
    const std::vector<double> values{1.0, kNaN, kInf, -kInf, 5.0, kNaN, kNaN, 2.0};
    const PlotOverviewPtr single = overviewOf(values.size(), [&](std::uint64_t index) {
        return std::make_pair(static_cast<double>(index), values[index]);
    });
    QCOMPARE(single->bins.size(), values.size());
    QCOMPARE(single->bins[1].sampleCount, std::uint64_t(1));
    QCOMPARE(single->bins[1].finiteCount, std::uint64_t(0));
    QVERIFY(std::isnan(single->bins[1].minimum) && std::isnan(single->bins[1].maximum));
    QVERIFY(std::isnan(single->bins[2].maximum));
    QCOMPARE(single->finiteCount, std::uint64_t(3));
    QCOMPARE(single->minimum, 1.0);
    QCOMPARE(single->maximum, 5.0);

    constexpr std::uint64_t count = 100'000;
    const PlotOverviewPtr wide = overviewOf(count, [](std::uint64_t index) {
        return std::make_pair(static_cast<double>(index),
                              index % 3 == 0 ? kInf : static_cast<double>(index % 1000));
    });
    QVERIFY(wide->bins.front().sampleCount > 1);
    QVERIFY(wide->bins.front().finiteCount < wide->bins.front().sampleCount);
    QCOMPARE(wide->maximum, 999.0);
    QCOMPARE(wide->finiteCount, count - (count + 2) / 3);
}

// A source that ends early: the overview keeps what arrived, bin per sample as
// if the count had been right, and says it is incomplete.
void TestPlotData::shortScanIsIncompleteAtFullResolution() {
    const PlotOverviewPtr overview = overviewOf(1000, regular(0.1), 1'000'000);
    QVERIFY(overview->incomplete());
    QCOMPARE(overview->sampleCount, std::uint64_t(1000));
    QCOMPARE(overview->requestedCount, std::uint64_t(1'000'000));
    QCOMPARE(overview->bins.size(), std::size_t(1000));
    QCOMPARE(overview->binShift, 0u);
    QCOMPARE(overview->domainEnd(), 999 * 0.1);
}

void TestPlotData::emptyScanHasNoBins() {
    const PlotOverviewPtr overview = overviewOf(0, regular(1.0), 100);
    QVERIFY(overview->bins.empty());
    QCOMPARE(overview->sampleCount, std::uint64_t(0));
    QVERIFY(overview->incomplete());
    QVERIFY(std::isnan(overview->minimum));
    QCOMPARE(overview->windowRequest(0.0, 1.0).sampleCount, std::uint64_t(0));
}

// Irregular spacing with repeats and gaps, three million samples: a range far
// into the recording holds exactly the samples within it plus one neighbor
// each side, each at its original absolute index.
void TestPlotData::farIrregularWindowHoldsEveryEligibleSample() {
    constexpr std::size_t count = 3'000'000;
    std::vector<double> time(count);
    std::mt19937_64 random(20260927);
    std::uniform_real_distribution<double> step(0.0, 2e-3);
    double coordinate = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        const std::uint64_t kind = random() % 100;
        if (kind == 99) {
            coordinate += 5.0;  // a gap
        } else if (kind >= 10) {
            coordinate += step(random);
        }  // else a repeat of the previous coordinate
        time[i] = coordinate;
    }
    const Samples samples = [&](std::uint64_t index) {
        return std::make_pair(time[index], static_cast<double>(index) * 0.5);
    };
    const PlotOverviewPtr overview = overviewOf(count, samples);
    QVERIFY(overview->domain == PlotDomain::Time);

    const double start = time[2'500'000];
    const double end = time[2'520'000];
    std::size_t first = 0;
    while (time[first + 1] < start) {
        ++first;
    }
    std::size_t last = first;
    while (time[last] <= end) {
        ++last;
    }

    const PlotWindowRequest request = overview->windowRequest(start, end);
    QVERIFY(request.sampleCount < kPlotWindowSamples);
    const PlotWindowPtr window = windowOf(request, samples);
    QVERIFY(window);
    QCOMPARE(window->firstSample, std::uint64_t(first));
    QCOMPARE(window->time.size(), last - first + 1);
    for (std::size_t i = 0; i < window->time.size(); ++i) {
        QCOMPARE(window->time[i], time[first + i]);
        QCOMPARE(window->value[i], static_cast<double>(first + i) * 0.5);
    }
    QVERIFY(window->covers(start, end));
}

// Runs of 1,000 equal coordinates span many bins; a range on one of them holds
// the whole run.
void TestPlotData::equalTimestampsAcrossBinsStayTogether() {
    const Samples samples = [](std::uint64_t index) {
        return std::make_pair(static_cast<double>(index / 1000), static_cast<double>(index));
    };
    const PlotOverviewPtr overview = overviewOf(100'000, samples);
    QVERIFY(overview->binShift > 0 && (std::uint64_t{1} << overview->binShift) < 1000);
    QCOMPARE(overview->minimumSpacing, 1.0);

    const PlotWindowPtr window = windowOf(overview->windowRequest(7.0, 7.0), samples);
    QVERIFY(window);
    QCOMPARE(window->firstSample, std::uint64_t(6999));
    QCOMPARE(window->time.size(), std::size_t(1002));
    QCOMPARE(window->time.front(), 6.0);
    QCOMPARE(window->time[1], 7.0);
    QCOMPARE(window->time[1000], 7.0);
    QCOMPARE(window->time.back(), 8.0);
    QVERIFY(window->covers(7.0, 7.0));
    QVERIFY(!window->covers(5.5, 7.0));
}

// A range inside a gap holds no sample; the window is the two neighbors, whose
// line crosses the range.
void TestPlotData::rangeInGapHoldsBothNeighbors() {
    const Samples samples = [](std::uint64_t index) {
        const double offset = index < 500 ? 0.0 : 1e6;
        return std::make_pair(offset + static_cast<double>(index), 1.0);
    };
    const PlotOverviewPtr overview = overviewOf(1000, samples);
    const PlotWindowPtr window = windowOf(overview->windowRequest(1000.0, 2000.0), samples);
    QVERIFY(window);
    QCOMPARE(window->firstSample, std::uint64_t(499));
    QCOMPARE(window->time.size(), std::size_t(2));
    QVERIFY(window->covers(1000.0, 2000.0));
}

// Bins partition indices, so one can hold the samples on both sides of a pause;
// it records the pause as its widest gap, for the painter to leave empty.
void TestPlotData::binAcrossGapRecordsItsWidth() {
    const PlotOverviewPtr overview = overviewOf(100'000, [](std::uint64_t index) {
        return std::make_pair(static_cast<double>(index) + (index >= 50'000 ? 1e6 : 0.0), 1.0);
    });
    std::size_t across = 0;
    for (const PlotBin& bin : overview->bins) {
        if (bin.firstSample < 50'000 && bin.firstSample + bin.sampleCount > 50'000) {
            QCOMPARE(bin.widestGap, 1e6 + 1.0);
            ++across;
        } else {
            QCOMPARE(bin.widestGap, 1.0);
        }
    }
    QCOMPARE(across, std::size_t(1));
}

// The limit counts the neighbors: 200 samples in range need 202.
void TestPlotData::windowRefusedBeyondItsLimit() {
    const Samples samples = regular(1.0);
    const PlotOverviewPtr overview = overviewOf(10'000, samples);
    const PlotWindowRequest request = overview->windowRequest(100.0, 299.5);
    PlotBuild state = PlotBuild::Continue;
    QVERIFY(!windowOf(request, samples, 201, &state));
    QVERIFY(state == PlotBuild::Refuse);
    const PlotWindowPtr window = windowOf(request, samples, 202, &state);
    QVERIFY(window);
    QCOMPARE(window->time.size(), std::size_t(202));
    QCOMPARE(PlotWindowBuilder::reservation(request, 201),
             sizeof(PlotWindowBuilder) + sizeof(PlotWindow) + 2 * (201 + 1) * sizeof(double));
}

// Samples that no longer match the overview never become a window: a
// coordinate going back, or coordinates that moved.
void TestPlotData::windowFailsWhenSamplesContradictOverview() {
    const PlotOverviewPtr overview = overviewOf(10'000, regular(1.0));
    const PlotWindowRequest request = overview->windowRequest(5000.0, 5010.0);
    PlotBuild state = PlotBuild::Continue;
    QVERIFY(!windowOf(request, [](std::uint64_t index) {
        return std::make_pair(index == 5005 ? 0.0 : static_cast<double>(index), 1.0);
    }, kPlotWindowSamples, &state));
    QVERIFY(state == PlotBuild::Fail);
    QVERIFY(!windowOf(request, [](std::uint64_t index) {
        return std::make_pair(static_cast<double>(index) + 100.0, 1.0);
    }, kPlotWindowSamples, &state));
    QVERIFY(state == PlotBuild::Fail);
}

// Indices up to 2^64 - 2, the last a u64 count can reach, through the bin
// partition, the cover and the window.
void TestPlotData::partitionMathNearTwoToThe64() {
    const auto single = [](PlotOverviewBuilder& builder, std::uint64_t index) {
        const double time = static_cast<double>(index);
        const double value = 1.0;
        builder.add(index, &time, &value, 1);
    };

    // Two bins at most: the runs widen to 2^63, where every index has one of two.
    PlotOverviewBuilder two(2);
    for (std::uint64_t index : {std::uint64_t{0}, std::uint64_t{1}, std::uint64_t{1} << 32,
                                std::uint64_t{1} << 63, kTop - 1}) {
        single(two, index);
    }
    const PlotOverviewPtr widest = two.finish(kTop);
    QCOMPARE(widest->binShift, 63u);
    QCOMPARE(widest->bins.size(), std::size_t(2));
    QCOMPARE(widest->bins[0].firstSample, std::uint64_t(0));
    QCOMPARE(widest->bins[0].sampleCount, std::uint64_t(3));
    QCOMPARE(widest->bins[1].firstSample, std::uint64_t{1} << 63);
    QCOMPARE(widest->bins[1].sampleCount, std::uint64_t(2));

    // 4,096 bins across the whole index space, then the last index joins the last.
    PlotOverviewBuilder full(kTop);
    for (std::uint64_t k = 0; k < kPlotOverviewBins; ++k) {
        single(full, k << 52);
    }
    single(full, kTop - 1);
    const PlotOverviewPtr spread = full.finish(kTop);
    QCOMPARE(spread->binShift, 52u);
    QCOMPARE(spread->bins.size(), kPlotOverviewBins);
    QCOMPARE(spread->bins.back().firstSample, std::uint64_t{4095} << 52);
    QCOMPARE(spread->bins.back().sampleCount, std::uint64_t(2));

    // Consecutive samples ending at the top index: covers and windows reach its end.
    constexpr std::uint64_t count = 5000;
    constexpr std::uint64_t base = kTop - count;
    const Samples samples = [](std::uint64_t index) {
        return std::make_pair(static_cast<double>(index - base), static_cast<double>(index % 7));
    };
    PlotOverviewBuilder top(count);
    scan(base, count, samples,
         [&](std::uint64_t first, const double* time, const double* value, std::size_t size) {
             top.add(first, time, value, size);
             return true;
         }, 777);
    const PlotOverviewPtr overview = top.finish(count);
    QVERIFY(partitioned(*overview, base, count));
    QCOMPARE(overview->binShift, 1u);

    const PlotWindowRequest tail = overview->windowRequest(4990.5, 4999.0);
    QCOMPARE(tail.firstSample + tail.sampleCount, kTop);
    QVERIFY(tail.samplesBefore && !tail.samplesAfter);
    const PlotWindowPtr end = windowOf(tail, samples);
    QVERIFY(end && end->atEnd);
    QCOMPARE(end->firstSample, base + 4990);
    QCOMPARE(end->firstSample + end->time.size(), kTop);

    const PlotWindowRequest inside = overview->windowRequest(1000.5, 1001.5);
    QVERIFY(inside.samplesBefore && inside.samplesAfter);
    const PlotWindowPtr middle = windowOf(inside, samples);
    QVERIFY(middle);
    QCOMPARE(middle->firstSample, base + 1000);
    QCOMPARE(middle->time.size(), std::size_t(3));
    QCOMPARE(middle->value[1], static_cast<double>((base + 1001) % 7));
}

// What a builder may hold is known before it exists: an overview retains
// exactly its reservation, a window at most its own.
void TestPlotData::storageIsReservedUpFront() {
    for (std::uint64_t stated : {std::uint64_t{0}, std::uint64_t{1}, std::uint64_t{2},
                                 std::uint64_t{100}, std::uint64_t{4096}, std::uint64_t{4097},
                                 std::uint64_t{1'000'000'000}, kTop}) {
        PlotOverviewBuilder builder(stated);
        scan(0, std::min<std::uint64_t>(stated, 10'000), regular(1.0),
             [&](std::uint64_t first, const double* time, const double* value, std::size_t size) {
                 builder.add(first, time, value, size);
                 return true;
             });
        const PlotOverviewPtr overview = builder.finish(stated);
        QCOMPARE(overview->bytes() + sizeof(PlotOverviewBuilder),
                 PlotOverviewBuilder::reservation(stated));
    }

    const PlotOverviewPtr overview = overviewOf(100'000, regular(1.0));
    const PlotWindowRequest request = overview->windowRequest(50'000.5, 50'100.5);
    const PlotWindowPtr window = windowOf(request, regular(1.0));
    QVERIFY(window);
    QCOMPARE(window->time.size(), std::size_t(102));
    // Allocated when the left neighbor came, for the rest of the cover.
    QCOMPARE(window->time.capacity(),
             static_cast<std::size_t>(request.firstSample + request.sampleCount -
                                      window->firstSample));
    QVERIFY(window->bytes() + sizeof(PlotWindowBuilder) <=
            PlotWindowBuilder::reservation(request));
}

QTEST_MAIN(TestPlotData)
#include "tst_plotdata.moc"
