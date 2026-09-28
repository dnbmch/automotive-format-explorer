// Generated recordings through the production adapter, the real reader and
// the session: the overview covers every sample, and a far zoom reads exactly
// the samples of its view at their original indices.

#include "adapters/mdf4adapter.h"
#include "models/signalplotmodel.h"
#include "models/treemodel.h"
#include "sessions/documentsession.h"
#include "sessions/mdf4documentsession.h"
#include "mf4recording.h"

#include <QCoreApplication>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>

namespace {

// More samples than one exact window holds.
constexpr std::uint64_t kCount = 4'500'000;
// Neither index repeats the time of the sample before it.
constexpr std::uint64_t kSpike = 4'400'001;
constexpr std::uint64_t kMissing = 4'400'010;

// Irregular time: one kHz with jitter, every seventh sample repeating the one
// before it, and a ten-second gap after every 500,000 samples.
double timeAt(std::uint64_t index) {
    const std::uint64_t step = index % 7 == 3 ? index - 1 : index;
    const double jitter = static_cast<double>((step * 2654435761u) % 1000) * 1e-7;
    return static_cast<double>(step) * 1e-3 + jitter + 10.0 * static_cast<double>(step / 500'000);
}

double valueAt(std::uint64_t index) {
    if (index == kSpike) {
        return 1000.0;
    }
    if (index == kMissing) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return 10.0 * std::sin(static_cast<double>(index) * 1e-4);
}

} // namespace

class TestMdf4Recording : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void spikeBeyondExactPrefixAppearsInOverview();
    void farIrregularZoomHoldsEverySample();
    void cleanupTestCase();

private:
    QTemporaryDir _dir;
    LoadResult _loaded;
    SignalPlotModel* _model = nullptr;
};

void TestMdf4Recording::initTestCase() {
    QVERIFY(_dir.isValid());
    const QString path = _dir.filePath(QStringLiteral("irregular.mf4"));
    QVERIFY(mf4recording::write(path.toStdString(), kCount, {{"Pressure", "bar"}}, timeAt,
                                [](std::size_t, std::uint64_t index) { return valueAt(index); }));
    _loaded = Mdf4Adapter().load(path);
    QVERIFY(_loaded.session);
    QVERIFY2(_loaded.diagnostics.isEmpty(),
             qPrintable(_loaded.diagnostics.isEmpty() ? QString()
                                                      : _loaded.diagnostics.front().title));
    _model = static_cast<SignalPlotModel*>(_loaded.session->centerPanelModel());

    TreeModel* tree = _loaded.session->treeModel();
    const QModelIndex group = tree->index(0, 0, tree->index(0, 0));
    QCOMPARE(tree->rowCount(group), 2);
    QCOMPARE(tree->data(tree->index(1, 0, group), TreeModel::TitleRole).toString(),
             QStringLiteral("Pressure"));
    _loaded.session->selectNode(
        tree->data(tree->index(1, 0, group), TreeModel::NodeKeyRole).toULongLong());
    QTRY_VERIFY_WITH_TIMEOUT(_model->hasSamples() && !_model->busy(), 120000);
}

// The spike lies past the first 4,194,304 samples, which is all one exact
// window could hold; the overview of the whole recording shows it.
void TestMdf4Recording::spikeBeyondExactPrefixAppearsInOverview() {
    QCOMPARE(_model->plotState(), SignalPlotModel::Overview);
    QCOMPARE(_model->name(), QStringLiteral("Pressure"));
    QCOMPARE(_model->unit(), QStringLiteral("bar"));
    QCOMPARE(_model->domainName(), QStringLiteral("time"));
    QCOMPARE(_model->domainUnit(), QStringLiteral("s"));
    const PlotOverview& overview = *_model->overview();
    QVERIFY(overview.domain == PlotDomain::Time);
    QCOMPARE(overview.sampleCount, kCount);
    QVERIFY(!overview.incomplete());
    QCOMPARE(overview.maximum, 1000.0);
    QCOMPARE(overview.finiteCount, kCount - 1);
    QCOMPARE(_model->fullStart(), timeAt(0));
    QCOMPARE(_model->fullEnd(), timeAt(kCount - 1));

    const std::vector<PlotColumn>& columns = _model->columns(1000);
    double top = -1.0;
    for (const PlotColumn& column : columns) {
        top = std::max(top, column.maximum);
    }
    QCOMPARE(top, 1000.0);
}

// A view of about a hundred samples near the end. The window, which reaches a
// view's width further each side, holds consecutive samples at their original
// indices, repeats and the missing value included, and the samples in view are
// exactly those whose time lies in it.
void TestMdf4Recording::farIrregularZoomHoldsEverySample() {
    const double start = timeAt(kSpike - 50);
    const double end = timeAt(kSpike + 50);
    _model->setVisibleRange(start, end);
    QTRY_VERIFY_WITH_TIMEOUT(_model->plotState() == SignalPlotModel::Detail && !_model->busy(),
                             60000);
    QCOMPARE(_model->viewStart(), start);
    QCOMPARE(_model->viewEnd(), end);

    const PlotWindow& window = *_model->window();
    QVERIFY(window.firstSample > 4'000'000);
    QVERIFY(window.time.size() < 1000);
    for (std::size_t i = 0; i < window.time.size(); ++i) {
        const std::uint64_t index = window.firstSample + i;
        QCOMPARE(window.time[i], timeAt(index));
        if (index == kMissing) {
            QVERIFY(std::isnan(window.value[i]));
        } else {
            QCOMPARE(window.value[i], valueAt(index));
        }
    }

    std::uint64_t first = kSpike - 50;
    while (timeAt(first - 1) >= start) {
        --first;
    }
    std::uint64_t last = kSpike + 50;
    while (timeAt(last + 1) <= end) {
        ++last;
    }
    const auto [visibleFirst, visibleLast] = _model->visibleSampleRange();
    QCOMPARE(window.firstSample + visibleFirst, first);
    QCOMPARE(window.firstSample + visibleLast, last + 1);
    QVERIFY(window.time.front() < start && window.time.back() > end);

    _model->setCursor(timeAt(kSpike), 0.0);
    QVERIFY(_model->cursor().exact);
    QCOMPARE(_model->cursor().firstSample, kSpike);
    QCOMPARE(_model->cursor().value, 1000.0);
}

void TestMdf4Recording::cleanupTestCase() {
    _loaded.session.reset();
}

QTEST_MAIN(TestMdf4Recording)
#include "tst_mdf4recording.moc"
