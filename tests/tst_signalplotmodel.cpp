// Format-neutral signal plot model tests.
//
// Pins the provider seam, viewport interaction state, nearest-sample cursor,
// and the bounded min/max representation used to paint dense recordings.

#include "models/signalplotmodel.h"

#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTest>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

PlotSeries makeSeries(std::size_t count) {
    PlotSeries series;
    series.name = QStringLiteral("EngineSpeed");
    series.unit = QStringLiteral("rpm");
    series.domainName = QStringLiteral("Time");
    series.domainUnit = QStringLiteral("s");
    series.time.reserve(count);
    series.value.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        series.time.push_back(static_cast<double>(i));
        series.value.push_back(static_cast<double>(i) * 2.0);
    }
    return series;
}

PlotSeriesPtr shared(PlotSeries series) {
    return std::make_shared<const PlotSeries>(std::move(series));
}

} // namespace

class TestSignalPlotModel : public QObject {
    Q_OBJECT

private slots:
    void exposesSeriesAndListRoles();
    void reportsBusyState();
    void zoomsPansAndResetsView();
    void irregularAxisUsesLocalZoomFloor();
    void cursorChoosesNearestSample();
    void bucketsPreserveDenseExtrema();
    void millionSamplesStayViewportBounded();
};

void TestSignalPlotModel::exposesSeriesAndListRoles() {
    SignalPlotModel model;
    PlotSeries series = makeSeries(3);
    series.value = {4.0, -2.0, 8.0};
    model.setSeries(shared(std::move(series)));

    QCOMPARE(model.name(), QStringLiteral("EngineSpeed"));
    QCOMPARE(model.unit(), QStringLiteral("rpm"));
    QCOMPARE(model.domainName(), QStringLiteral("Time"));
    QCOMPARE(model.domainUnit(), QStringLiteral("s"));
    QCOMPARE(model.sampleCount(), quint64(3));
    QVERIFY(model.hasSeries());
    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(model.data(model.index(1, 0), SignalPlotModel::TimeRole).toDouble(), 1.0);
    QCOMPARE(model.data(model.index(1, 0), SignalPlotModel::ValueRole).toDouble(), -2.0);
    QCOMPARE(model.roleNames().value(SignalPlotModel::TimeRole), QByteArray("time"));
    QCOMPARE(model.roleNames().value(SignalPlotModel::ValueRole), QByteArray("value"));
    QCOMPARE(model.fullStart(), 0.0);
    QCOMPARE(model.fullEnd(), 2.0);
    QCOMPARE(model.viewStart(), 0.0);
    QCOMPARE(model.viewEnd(), 2.0);
    QVERIFY(model.viewMinimum() < -2.0);
    QVERIFY(model.viewMaximum() > 8.0);
}

void TestSignalPlotModel::reportsBusyState() {
    SignalPlotModel model;
    QSignalSpy changed(&model, &SignalPlotModel::busyChanged);

    model.setBusy(true);
    QVERIFY(model.busy());
    QCOMPARE(changed.count(), 1);

    model.setBusy(true);
    QCOMPARE(changed.count(), 1);

    model.setBusy(false);
    QVERIFY(!model.busy());
    QCOMPARE(changed.count(), 2);
}

void TestSignalPlotModel::zoomsPansAndResetsView() {
    SignalPlotModel model;
    model.setSeries(shared(makeSeries(100)));

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

void TestSignalPlotModel::irregularAxisUsesLocalZoomFloor() {
    SignalPlotModel model;
    PlotSeries series;
    series.time = {0.0, 0.001, 1000.0};
    series.value = {1.0, 2.0, 3.0};
    model.setSeries(shared(std::move(series)));

    model.setVisibleRange(0.0, 0.002);
    QVERIFY(model.viewEnd() - model.viewStart() < 0.01);

    model.zoomAt(0.001, 0.01);
    QVERIFY(model.viewEnd() - model.viewStart() >= 0.001);
    QVERIFY(model.viewEnd() - model.viewStart() < 0.002);
}

void TestSignalPlotModel::cursorChoosesNearestSample() {
    SignalPlotModel model;
    PlotSeries series;
    series.time = {0.0, 5.0, 10.0};
    series.value = {10.0, 20.0, 30.0};
    model.setSeries(shared(std::move(series)));

    model.setCursorTime(6.0);
    QVERIFY(model.cursorVisible());
    QCOMPARE(model.cursorIndex(), qint64(1));
    QCOMPARE(model.cursorTime(), 5.0);
    QCOMPARE(model.cursorValue(), 20.0);

    model.setCursorTime(8.0);
    QCOMPARE(model.cursorIndex(), qint64(2));

    model.clearCursor();
    QVERIFY(!model.cursorVisible());
    QCOMPARE(model.cursorIndex(), qint64(-1));
}

void TestSignalPlotModel::bucketsPreserveDenseExtrema() {
    SignalPlotModel model;
    PlotSeries series = makeSeries(10'000);
    std::fill(series.value.begin(), series.value.end(), 0.0);
    series.value[1234] = 50.0;
    series.value[1235] = -40.0;
    series.value[8765] = std::numeric_limits<double>::quiet_NaN();
    model.setSeries(shared(std::move(series)));

    const auto& buckets = model.buckets(100);
    QVERIFY(buckets.size() <= 100);

    const auto spike = std::find_if(buckets.begin(), buckets.end(), [](const PlotBucket& bucket) {
        return bucket.firstSample <= 1234 && bucket.lastSample > 1235;
    });
    QVERIFY(spike != buckets.end());
    QCOMPARE(spike->minimum, -40.0);
    QCOMPARE(spike->maximum, 50.0);
}

void TestSignalPlotModel::millionSamplesStayViewportBounded() {
    constexpr std::size_t sampleCount = 1'000'000;
    SignalPlotModel model;
    PlotSeries series;
    series.name = QStringLiteral("Dense");
    series.time.resize(sampleCount);
    series.value.resize(sampleCount);
    for (std::size_t i = 0; i < sampleCount; ++i) {
        series.time[i] = static_cast<double>(i) * 0.001;
        series.value[i] = std::sin(static_cast<double>(i) * 0.002);
    }
    series.value[111'111] = -1234.0;
    series.value[777'777] = 4321.0;
    QElapsedTimer timer;
    timer.start();
    model.setSeries(shared(std::move(series)));
    const auto& buckets = model.buckets(1200);
    const qint64 elapsed = timer.elapsed();

    QVERIFY2(!buckets.empty(), "dense series produced no paint buckets");
    QVERIFY(buckets.size() <= 1200);
    const auto minimum = std::min_element(buckets.begin(), buckets.end(),
        [](const PlotBucket& left, const PlotBucket& right) {
            return left.minimum < right.minimum;
        });
    const auto maximum = std::max_element(buckets.begin(), buckets.end(),
        [](const PlotBucket& left, const PlotBucket& right) {
            return left.maximum < right.maximum;
        });
    QCOMPARE(minimum->minimum, -1234.0);
    QCOMPARE(maximum->maximum, 4321.0);
    QVERIFY2(elapsed < 2000, qPrintable(
        QStringLiteral("dense bucketing took %1 ms").arg(elapsed)));

    const PlotBucket* cachedStorage = buckets.data();
    QCOMPARE(model.buckets(1200).data(), cachedStorage);
}

QTEST_MAIN(TestSignalPlotModel)
#include "tst_signalplotmodel.moc"
