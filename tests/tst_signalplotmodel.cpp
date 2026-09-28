// The format-neutral plot model: what a producer installs, the states a view
// shows, bounds and zoom, the detail the view asks for, truthful hover and
// viewport-sized paint columns.

#include "models/signalplotmodel.h"
#include "plotscan.h"

#include <QElapsedTimer>
#include <QLocale>
#include <QSignalSpy>
#include <QTest>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>

using namespace plotscan;

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

PlotHeader header() {
    return {QStringLiteral("EngineSpeed"), QStringLiteral("rpm"), QStringLiteral("Time"),
            QStringLiteral("s")};
}

// Coordinates are the indices, values index modulo 1,000.
Samples ramp() {
    return [](std::uint64_t index) {
        return std::make_pair(static_cast<double>(index), static_cast<double>(index % 1000));
    };
}

Samples listed(std::vector<double> time, std::vector<double> value) {
    return [time = std::move(time), value = std::move(value)](std::uint64_t index) {
        return std::make_pair(time[index], value[index]);
    };
}

// Installs the window the model asks for, built from the same samples.
void installWanted(SignalPlotModel& model, const Samples& samples) {
    const std::optional<PlotWindowRequest> request = model.detailRequest();
    QVERIFY(request);
    model.setWindow(windowOf(*request, samples));
}

} // namespace

class TestSignalPlotModel : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void exposesSignalAndOverview();
    void statesFollowWhatIsInstalled();
    void reportsBusyAndProgress();
    void zoomsPansAndResetsView();
    void irregularAxisUsesLocalZoomFloor();
    void windowInstallKeepsBoundsAndZoom();
    void resetReturnsToOverviewAtOnce();
    void viewLeavingWindowWantsDetail();
    void denseViewAsksToZoomIn();
    void overviewHoverReportsBinsNotASample();
    void exactHoverReportsSampleAndIndex();
    void incompleteIsNeverComplete();
    void spikeBeyondExactPrefixAppearsInColumns();
    void detailColumnsPreserveDenseExtrema();
    void indexDomainShowsSampleIndices();
    void observerMayDestroyModel();
    void columnsStayViewportBounded();
};

void TestSignalPlotModel::initTestCase() {
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
}

void TestSignalPlotModel::exposesSignalAndOverview() {
    SignalPlotModel model;
    model.setSignal(header(), overviewOf(3, listed({0.0, 1.0, 2.0}, {4.0, -2.0, 8.0})));

    QCOMPARE(model.name(), QStringLiteral("EngineSpeed"));
    QCOMPARE(model.unit(), QStringLiteral("rpm"));
    QCOMPARE(model.domainName(), QStringLiteral("Time"));
    QCOMPARE(model.domainUnit(), QStringLiteral("s"));
    QVERIFY(model.hasSamples());
    QCOMPARE(model.countText(), QStringLiteral("3 samples"));
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(model.fullStart(), 0.0);
    QCOMPARE(model.fullEnd(), 2.0);
    QCOMPARE(model.viewStart(), 0.0);
    QCOMPARE(model.viewEnd(), 2.0);
    QVERIFY(model.viewMinimum() < -2.0);
    QVERIFY(model.viewMaximum() > 8.0);
    QCOMPARE(model.plotState(), SignalPlotModel::Overview);
}

// Each state and its message follow from what the producer installed, and
// stateChanged announces every move once.
void TestSignalPlotModel::statesFollowWhatIsInstalled() {
    SignalPlotModel model;
    QSignalSpy moved(&model, &SignalPlotModel::stateChanged);
    QCOMPARE(model.plotState(), SignalPlotModel::NoSignal);
    QVERIFY(model.message().isEmpty());

    model.setSignal(header());
    QCOMPARE(model.plotState(), SignalPlotModel::Pending);
    QCOMPARE(model.message(), QStringLiteral("Reading samples…"));
    QVERIFY(model.countText().isEmpty());

    model.setSignal(header(), {}, PlotNote::Failed, QStringLiteral("Samples could not be read: x"));
    QCOMPARE(model.plotState(), SignalPlotModel::Failed);
    QCOMPARE(model.message(), QStringLiteral("Samples could not be read: x"));

    model.setSignal(header(), {}, PlotNote::Refused, QStringLiteral("Over the limit"));
    QCOMPARE(model.plotState(), SignalPlotModel::Refused);
    QCOMPARE(model.message(), QStringLiteral("Over the limit"));

    model.setSignal(header(), {}, PlotNote::Empty, QStringLiteral("Master channel"));
    QCOMPARE(model.plotState(), SignalPlotModel::Empty);
    QCOMPARE(model.message(), QStringLiteral("Master channel"));
    QVERIFY(!model.hasSamples());

    model.setSignal(header(), overviewOf(0, ramp()));
    QCOMPARE(model.plotState(), SignalPlotModel::Empty);
    QCOMPARE(model.message(), QStringLiteral("No samples recorded"));
    QCOMPARE(model.countText(), QStringLiteral("0 samples"));

    model.setSignal(header(), overviewOf(0, ramp(), 10));
    QCOMPARE(model.plotState(), SignalPlotModel::Empty);
    QVERIFY(model.incomplete());
    QCOMPARE(model.message(), QStringLiteral("No samples: the recording ends before its first one"));
    QCOMPARE(model.countText(), QStringLiteral("0 of 10 samples"));

    model.clear();
    QCOMPARE(model.plotState(), SignalPlotModel::NoSignal);
    QVERIFY(model.name().isEmpty());
    QCOMPARE(moved.count(), 7);
}

void TestSignalPlotModel::reportsBusyAndProgress() {
    SignalPlotModel model;
    QSignalSpy changed(&model, &SignalPlotModel::busyChanged);
    QCOMPARE(model.progress(), -1.0);

    model.setBusy(true);
    QVERIFY(model.busy());
    model.setBusy(true);
    QCOMPARE(changed.count(), 1);
    model.setProgress(0.5);
    QCOMPARE(model.progress(), 0.5);
    model.setProgress(0.5);
    QCOMPARE(changed.count(), 2);
    model.setBusy(false);
    QVERIFY(!model.busy());
    QCOMPARE(changed.count(), 3);
}

void TestSignalPlotModel::zoomsPansAndResetsView() {
    SignalPlotModel model;
    model.setSignal(header(), overviewOf(100, ramp()));

    model.setVisibleRange(20.0, 40.0);
    QCOMPARE(model.viewStart(), 20.0);
    QCOMPARE(model.viewEnd(), 40.0);

    model.panBy(100.0);
    QCOMPARE(model.viewStart(), 79.0);
    QCOMPARE(model.viewEnd(), 99.0);

    const double oldSpan = model.viewEnd() - model.viewStart();
    model.zoomAt(89.0, 0.5);
    QVERIFY(qFuzzyCompare(model.viewEnd() - model.viewStart(), oldSpan * 0.5));

    model.resetView();
    QCOMPARE(model.viewStart(), 0.0);
    QCOMPARE(model.viewEnd(), 99.0);
}

// The zoom floor is the smallest spacing the worker found, so a dense burst
// between wide gaps still opens up.
void TestSignalPlotModel::irregularAxisUsesLocalZoomFloor() {
    SignalPlotModel model;
    model.setSignal(header(), overviewOf(3, listed({0.0, 0.001, 1000.0}, {1.0, 2.0, 3.0})));

    model.setVisibleRange(0.0, 0.002);
    QVERIFY(model.viewEnd() - model.viewStart() < 0.01);

    model.zoomAt(0.001, 0.01);
    QVERIFY(model.viewEnd() - model.viewStart() >= 0.001);
    QVERIFY(model.viewEnd() - model.viewStart() < 0.002);
}

// A window arriving for the zoomed view keeps the overview's bounds and the
// zoom; zooming within it needs no other window.
void TestSignalPlotModel::windowInstallKeepsBoundsAndZoom() {
    const Samples samples = ramp();
    SignalPlotModel model;
    QSignalSpy wanted(&model, &SignalPlotModel::detailWanted);
    model.setSignal(header(), overviewOf(100'000, samples));
    QCOMPARE(wanted.count(), 1);

    model.setVisibleRange(20'000.0, 20'500.0);
    QCOMPARE(wanted.count(), 2);
    const std::optional<PlotWindowRequest> request = model.detailRequest();
    QVERIFY(request);
    QVERIFY(request->start <= 20'000.0 && request->end >= 20'500.0);
    QVERIFY(request->sampleCount <= kPlotWindowSamples);

    model.setWindow(windowOf(*request, samples));
    QCOMPARE(model.plotState(), SignalPlotModel::Detail);
    QCOMPARE(model.viewStart(), 20'000.0);
    QCOMPARE(model.viewEnd(), 20'500.0);
    QCOMPARE(model.fullStart(), 0.0);
    QCOMPARE(model.fullEnd(), 99'999.0);
    QVERIFY(!model.detailRequest());
    QCOMPARE(wanted.count(), 2);

    model.zoomAt(20'250.0, 0.5);
    QCOMPARE(model.plotState(), SignalPlotModel::Detail);
    QCOMPARE(wanted.count(), 2);
}

// Reset shows the whole overview at once and lets go of the window, which no
// longer covers the view; a producer installs it again for that view.
void TestSignalPlotModel::resetReturnsToOverviewAtOnce() {
    const Samples samples = ramp();
    SignalPlotModel model;
    model.setSignal(header(), overviewOf(100'000, samples));
    model.setVisibleRange(20'000.0, 20'500.0);
    installWanted(model, samples);
    QCOMPARE(model.plotState(), SignalPlotModel::Detail);
    const PlotWindowPtr window = model.window();

    QSignalSpy wanted(&model, &SignalPlotModel::detailWanted);
    model.resetView();
    QCOMPARE(model.plotState(), SignalPlotModel::Overview);
    QCOMPARE(model.viewStart(), 0.0);
    QCOMPARE(model.viewEnd(), 99'999.0);
    QVERIFY(!model.window());
    QCOMPARE(window.use_count(), 1L);
    QCOMPARE(wanted.count(), 1);

    model.setVisibleRange(20'000.0, 20'500.0);
    QCOMPARE(model.plotState(), SignalPlotModel::Overview);
    model.setWindow(window);
    QCOMPARE(model.plotState(), SignalPlotModel::Detail);
}

// Panning past the window's coverage falls back to the overview and asks for
// the new view; a note on the old view goes with it.
void TestSignalPlotModel::viewLeavingWindowWantsDetail() {
    const Samples samples = ramp();
    SignalPlotModel model;
    model.setSignal(header(), overviewOf(100'000, samples));
    model.setVisibleRange(20'000.0, 20'500.0);
    installWanted(model, samples);

    QSignalSpy wanted(&model, &SignalPlotModel::detailWanted);
    model.panBy(5'000.0);
    QCOMPARE(model.plotState(), SignalPlotModel::Overview);
    QVERIFY(!model.window());
    QCOMPARE(wanted.count(), 1);
    const std::optional<PlotWindowRequest> request = model.detailRequest();
    QVERIFY(request);
    QVERIFY(request->start <= 25'000.0 && request->end >= 25'500.0);

    // A note answers the request; it asks for nothing again.
    model.setWindow(nullptr, PlotNote::Failed, QStringLiteral("Exact samples could not be read"));
    QCOMPARE(model.message(), QStringLiteral("Exact samples could not be read"));
    QCOMPARE(wanted.count(), 1);
    model.panBy(10.0);
    QVERIFY(model.message() != QStringLiteral("Exact samples could not be read"));
}

// Five million samples in view: no window may hold them, so none is wanted and
// the overview says to zoom in; a narrower view gets its window.
void TestSignalPlotModel::denseViewAsksToZoomIn() {
    SignalPlotModel model;
    model.setSignal(header(), overviewOf(kPlotWindowSamples + (1u << 20), ramp()));
    QCOMPARE(model.plotState(), SignalPlotModel::Overview);
    QVERIFY(!model.detailRequest());
    QCOMPARE(model.message(),
             QStringLiteral("More than 4,194,304 samples in view; zoom in for exact samples"));

    model.setVisibleRange(1'000.0, 2'000.0);
    const std::optional<PlotWindowRequest> request = model.detailRequest();
    QVERIFY(request);
    QVERIFY(request->sampleCount <= kPlotWindowSamples);
    QVERIFY(model.message().isEmpty());
    model.setBusy(true);
    QCOMPARE(model.message(), QStringLiteral("Reading exact samples…"));
}

// Over the overview the cursor names the bins under the pointer's column: their
// index interval, count and the true extrema of those samples. It never
// presents one of them as the sample at the pointer.
void TestSignalPlotModel::overviewHoverReportsBinsNotASample() {
    const Samples samples = ramp();
    SignalPlotModel model;
    model.setSignal(header(), overviewOf(100'000, samples));
    QSignalSpy changed(&model, &SignalPlotModel::cursorChanged);

    model.setCursor(50'000.2, 40.0);
    QCOMPARE(changed.count(), 1);
    const PlotCursor cursor = model.cursor();
    QVERIFY(cursor.visible);
    QVERIFY(!cursor.exact);
    QVERIFY(cursor.firstSample <= 49'961 && cursor.firstSample + cursor.sampleCount > 50'040);
    double minimum = 1e300;
    double maximum = -1e300;
    for (std::uint64_t index = cursor.firstSample; index < cursor.firstSample + cursor.sampleCount;
         ++index) {
        minimum = std::min(minimum, samples(index).second);
        maximum = std::max(maximum, samples(index).second);
    }
    QCOMPARE(cursor.minimum, minimum);
    QCOMPARE(cursor.maximum, maximum);
    QCOMPARE(cursor.finiteCount, cursor.sampleCount);
    QCOMPARE(cursor.firstDomain, static_cast<double>(cursor.firstSample));
    QCOMPARE(cursor.lastDomain,
             static_cast<double>(cursor.firstSample + cursor.sampleCount - 1));
    const QString text = model.cursorText();
    QVERIFY2(text.contains(QStringLiteral("%1 samples, index").arg(cursor.sampleCount)), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("min 0, max 999 rpm")), qPrintable(text));

    model.setCursor(50'000.4, 40.0);
    QCOMPARE(changed.count(), 1);
    model.clearCursor();
    QVERIFY(!model.cursorVisible());
    QCOMPARE(changed.count(), 2);
}

// Over exact samples the cursor is the nearest sample: its coordinate, its
// actual value and its absolute index. A window arriving under a resting
// pointer turns the report exact.
void TestSignalPlotModel::exactHoverReportsSampleAndIndex() {
    const Samples samples = regular(1.0);
    SignalPlotModel model;
    model.setSignal(header(), overviewOf(100'000, samples));
    model.setVisibleRange(20'000.0, 20'500.0);
    model.setCursor(20'100.4, 0.5);
    QVERIFY(!model.cursor().exact);

    installWanted(model, samples);
    const PlotCursor cursor = model.cursor();
    QVERIFY(cursor.exact);
    QCOMPARE(cursor.firstSample, std::uint64_t(20'100));
    QCOMPARE(cursor.firstDomain, 20'100.0);
    QCOMPARE(cursor.value, std::sin(20'100.0));
    QCOMPARE(model.cursorLines(),
             (QStringList{QStringLiteral("Time = 20100 s"),
                          SignalPlotModel::numberText(std::sin(20'100.0)) + QStringLiteral(" rpm"),
                          QStringLiteral("index 20,100")}));
}

// A short source keeps its observed bounds and count and says it is
// incomplete; nothing extends it to the stated end.
void TestSignalPlotModel::incompleteIsNeverComplete() {
    SignalPlotModel model;
    model.setSignal(header(), overviewOf(1000, regular(0.1), 1'000'000));
    QVERIFY(model.incomplete());
    QCOMPARE(model.countText(), QStringLiteral("1,000 of 1,000,000 samples"));
    QCOMPARE(model.fullEnd(), 999 * 0.1);
    QCOMPARE(model.viewEnd(), 999 * 0.1);
    QCOMPARE(model.plotState(), SignalPlotModel::Overview);
}

// A spike past the first 4 Mi samples reaches the whole-recording view.
void TestSignalPlotModel::spikeBeyondExactPrefixAppearsInColumns() {
    constexpr std::uint64_t count = kPlotWindowSamples + (std::uint64_t{1} << 20);
    constexpr std::uint64_t spike = kPlotWindowSamples + 700'003;
    SignalPlotModel model;
    model.setSignal(header(), overviewOf(count, [](std::uint64_t index) {
        return std::make_pair(static_cast<double>(index), index == spike ? 1000.0 : 0.0);
    }));
    const std::vector<PlotColumn>& columns = model.columns(1200);
    QVERIFY(!columns.empty() && columns.size() <= 1200);
    const double halfColumn = (model.viewEnd() - model.viewStart()) / 2400.0;
    const auto holds = std::find_if(columns.begin(), columns.end(), [&](const PlotColumn& column) {
        return std::abs(column.domain - static_cast<double>(spike)) <= halfColumn;
    });
    QVERIFY(holds != columns.end());
    QCOMPARE(holds->maximum, 1000.0);
    QVERIFY(model.viewMaximum() > 1000.0);
}

// Exact samples denser than the pixels: each column spans the extrema of its
// samples, nonfinite ones left out.
void TestSignalPlotModel::detailColumnsPreserveDenseExtrema() {
    std::vector<double> values(10'000, 0.0);
    values[1234] = 50.0;
    values[1235] = -40.0;
    values[8765] = kNaN;
    const Samples samples = [&](std::uint64_t index) {
        return std::make_pair(static_cast<double>(index), values[index]);
    };
    SignalPlotModel model;
    model.setSignal(header(), overviewOf(values.size(), samples));
    installWanted(model, samples);
    QCOMPARE(model.plotState(), SignalPlotModel::Detail);

    const std::vector<PlotColumn>& columns = model.columns(100);
    QVERIFY(columns.size() <= 100);
    const auto spike = std::find_if(columns.begin(), columns.end(), [](const PlotColumn& column) {
        return column.minimum == -40.0 && column.maximum == 50.0;
    });
    QVERIFY(spike != columns.end());
    QVERIFY(std::all_of(columns.begin(), columns.end(), [](const PlotColumn& column) {
        return std::isfinite(column.minimum) && std::isfinite(column.maximum);
    }));
}

// A result set whose coordinates went back plots, and reports, sample indices.
void TestSignalPlotModel::indexDomainShowsSampleIndices() {
    const Samples samples = [](std::uint64_t index) {
        return std::make_pair(index == 900 ? 0.0 : static_cast<double>(index) * 0.5, 1.0);
    };
    SignalPlotModel model;
    model.setSignal(header(), overviewOf(1000, samples));
    QCOMPARE(model.domainName(), QStringLiteral("Sample index"));
    QVERIFY(model.domainUnit().isEmpty());
    QCOMPARE(model.fullEnd(), 999.0);

    model.setVisibleRange(100.0, 200.0);
    installWanted(model, samples);
    QCOMPARE(model.plotState(), SignalPlotModel::Detail);
    model.setCursor(150.2, 0.5);
    QCOMPARE(model.cursorLines().front(), QStringLiteral("Sample index = 150"));
}

// Observers run synchronously and may destroy the model, as closing a tab
// does; nothing is announced after that.
void TestSignalPlotModel::observerMayDestroyModel() {
    auto* model = new SignalPlotModel;
    bool announcedAfter = false;
    QObject::connect(model, &SignalPlotModel::contentChanged, [&model] {
        delete model;
        model = nullptr;
    });
    QObject::connect(model, &SignalPlotModel::viewChanged, [&] { announcedAfter = true; });
    model->setSignal(header(), overviewOf(10, ramp()));
    QVERIFY(!model);
    QVERIFY(!announcedAfter);
}

void TestSignalPlotModel::columnsStayViewportBounded() {
    constexpr std::uint64_t count = 1'000'000;
    const Samples samples = [](std::uint64_t index) {
        const double value = index == 111'111 ? -1234.0
                           : index == 777'777 ? 4321.0
                                              : std::sin(static_cast<double>(index) * 0.002);
        return std::make_pair(static_cast<double>(index) * 0.001, value);
    };
    SignalPlotModel model;
    model.setSignal(header(), overviewOf(count, samples));
    installWanted(model, samples);
    QCOMPARE(model.plotState(), SignalPlotModel::Detail);

    QElapsedTimer timer;
    timer.start();
    const std::vector<PlotColumn>& columns = model.columns(1200);
    const qint64 elapsed = timer.elapsed();
    QVERIFY(!columns.empty() && columns.size() <= 1200);
    QCOMPARE(std::min_element(columns.begin(), columns.end(),
                              [](const PlotColumn& a, const PlotColumn& b) {
                                  return a.minimum < b.minimum;
                              })->minimum,
             -1234.0);
    QCOMPARE(std::max_element(columns.begin(), columns.end(),
                              [](const PlotColumn& a, const PlotColumn& b) {
                                  return a.maximum < b.maximum;
                              })->maximum,
             4321.0);
    QVERIFY2(elapsed < 2000, qPrintable(QStringLiteral("columns took %1 ms").arg(elapsed)));
    QCOMPARE(model.columns(1200).data(), columns.data());
}

QTEST_MAIN(TestSignalPlotModel)
#include "tst_signalplotmodel.moc"
