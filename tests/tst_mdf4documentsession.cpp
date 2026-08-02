#include "adapters/mdf4adapter.h"
#include "models/signalplotmodel.h"
#include "models/treemodel.h"
#include "sessions/mdf4documentsession.h"

#include <QSemaphore>
#include <QTest>
#include <QThread>

#include <atomic>
#include <cstdint>
#include <limits>

namespace {

mdf4::File makeDocument(std::uint64_t sampleCount = 4) {
    mdf4::File document;
    document.set_version("4.20");
    mdf4::ChannelGroup* group = document.add_groups();
    group->set_name("Powertrain");
    group->set_cycle_count(sampleCount);
    group->set_storage(mdf4::ROW_DT);

    for (const char* name : {"EngineSpeed", "VehicleSpeed"}) {
        mdf4::Channel* channel = group->add_channels();
        channel->set_name(name);
        channel->set_unit("rpm");
        channel->set_sample_count(sampleCount);
        channel->set_decodable(true);
        channel->set_data_type(mdf4::FLOAT_LE);
        channel->set_bit_count(64);
    }
    return document;
}

quint64 channelKey(Mdf4DocumentSession& session, int channelIndex) {
    TreeModel* tree = session.treeModel();
    const QModelIndex file = tree->index(0, 0);
    const QModelIndex group = tree->index(0, 0, file);
    const QModelIndex channel = tree->index(channelIndex, 0, group);
    return tree->data(channel, TreeModel::NodeKeyRole).toULongLong();
}

PlotSeries fourSamples(double base) {
    PlotSeries series;
    series.time = {0.0, 1.0, 2.0, 3.0};
    series.value = {base, base + 1.0, base + 2.0, base + 3.0};
    return series;
}

} // namespace

class TestMdf4DocumentSession : public QObject {
    Q_OBJECT

private slots:
    void decodeUsesMetadataRangeAndCachesResult();
    void timeMasterLabelsPlotDomain();
    void staleDecodeCannotReplaceNewSelection();
    void siblingWriterFileOpensAndPlots();
};

void TestMdf4DocumentSession::decodeUsesMetadataRangeAndCachesResult() {
    constexpr std::uint64_t million = 1'000'000;
    std::atomic<int> calls{0};
    std::atomic<std::uint32_t> observedGroup{std::numeric_limits<std::uint32_t>::max()};
    std::atomic<std::uint32_t> observedChannel{std::numeric_limits<std::uint32_t>::max()};
    std::atomic<std::uint64_t> observedFirst{std::numeric_limits<std::uint64_t>::max()};
    std::atomic<std::uint64_t> observedCount{0};

    Mdf4DocumentSession session(
        QStringLiteral("large.mf4"),
        QStringLiteral("large.mf4"),
        makeDocument(million),
        {},
        [&](const QString&, std::uint32_t group, std::uint32_t channel,
            std::uint64_t first, std::uint64_t count) {
            observedGroup = group;
            observedChannel = channel;
            observedFirst = first;
            observedCount = count;
            ++calls;
            return fourSamples(100.0);
        });

    auto* model = static_cast<SignalPlotModel*>(session.centerPanelModel());
    const quint64 key = channelKey(session, 0);
    QVERIFY(key != 0);

    session.selectNode(key);
    QTRY_VERIFY_WITH_TIMEOUT(!model->busy(), 2000);
    QCOMPARE(calls.load(), 1);
    QCOMPARE(observedGroup.load(), std::uint32_t(0));
    QCOMPARE(observedChannel.load(), std::uint32_t(0));
    QCOMPARE(observedFirst.load(), std::uint64_t(0));
    QCOMPARE(observedCount.load(), million);
    QCOMPARE(model->series().name, QStringLiteral("EngineSpeed"));
    QCOMPARE(model->series().domainName, QStringLiteral("Record index"));
    QVERIFY(model->series().domainUnit.isEmpty());
    QCOMPARE(model->series().value.front(), 100.0);

    session.selectNode(key);
    QVERIFY(!model->busy());
    QCOMPARE(calls.load(), 1);
    QCOMPARE(model->series().value.front(), 100.0);
}

void TestMdf4DocumentSession::timeMasterLabelsPlotDomain() {
    mdf4::File document = makeDocument();
    mdf4::Channel* master = document.mutable_groups(0)->add_channels();
    master->set_name("Acquisition time");
    master->set_unit("ms");
    master->set_sample_count(4);
    master->set_is_master(true);
    master->set_sync_type(1);
    master->set_cn_type(2);
    master->set_decodable(true);

    Mdf4DocumentSession session(
        QStringLiteral("timed.mf4"),
        QStringLiteral("timed.mf4"),
        std::move(document),
        {},
        [](const QString&, std::uint32_t, std::uint32_t,
           std::uint64_t, std::uint64_t) { return fourSamples(10.0); });

    auto* model = static_cast<SignalPlotModel*>(session.centerPanelModel());
    session.selectNode(channelKey(session, 0));
    QTRY_VERIFY_WITH_TIMEOUT(!model->busy(), 2000);
    QCOMPARE(model->series().domainName, QStringLiteral("Acquisition time"));
    QCOMPARE(model->series().domainUnit, QStringLiteral("ms"));
}

void TestMdf4DocumentSession::staleDecodeCannotReplaceNewSelection() {
    QSemaphore firstStarted;
    QSemaphore secondStarted;

    Mdf4DocumentSession session(
        QStringLiteral("race.mf4"),
        QStringLiteral("race.mf4"),
        makeDocument(),
        {},
        [&](const QString&, std::uint32_t, std::uint32_t channel,
            std::uint64_t, std::uint64_t) {
            if (channel == 0) {
                firstStarted.release();
                QThread::msleep(150);
                return fourSamples(100.0);
            }
            secondStarted.release();
            QThread::msleep(10);
            return fourSamples(200.0);
        });

    auto* model = static_cast<SignalPlotModel*>(session.centerPanelModel());
    session.selectNode(channelKey(session, 0));
    QVERIFY(firstStarted.tryAcquire(1, 2000));

    session.selectNode(channelKey(session, 1));
    QVERIFY(secondStarted.tryAcquire(1, 2000));
    QTRY_VERIFY_WITH_TIMEOUT(!model->busy(), 2000);
    QCOMPARE(model->series().name, QStringLiteral("VehicleSpeed"));
    QCOMPARE(model->series().value.front(), 200.0);

    QTest::qWait(200);
    QCOMPARE(model->series().name, QStringLiteral("VehicleSpeed"));
    QCOMPARE(model->series().value.front(), 200.0);
}

void TestMdf4DocumentSession::siblingWriterFileOpensAndPlots() {
    const QString path = qEnvironmentVariable("MDF4_WRITER_SAMPLE");
    if (path.isEmpty()) {
        QSKIP("Set MDF4_WRITER_SAMPLE to run the sibling writer-file smoke");
    }

    Mdf4Adapter adapter;
    LoadResult result = adapter.load(path);
    QVERIFY2(result.session != nullptr, qPrintable(
        result.diagnostics.isEmpty() ? QStringLiteral("MDF4 session was not created")
                                     : result.diagnostics.front().detail));

    TreeModel* tree = result.session->treeModel();
    const QModelIndex file = tree->index(0, 0);
    QVERIFY(file.isValid());

    quint64 plottableKey = 0;
    for (int groupRow = 0; groupRow < tree->rowCount(file) && plottableKey == 0; ++groupRow) {
        const QModelIndex group = tree->index(groupRow, 0, file);
        for (int channelRow = 0; channelRow < tree->rowCount(group); ++channelRow) {
            const QModelIndex channel = tree->index(channelRow, 0, group);
            if (tree->data(channel, TreeModel::SemanticKindRole).toInt() ==
                static_cast<int>(SemanticKind::Entity)) {
                plottableKey = tree->data(channel, TreeModel::NodeKeyRole).toULongLong();
                break;
            }
        }
    }
    QVERIFY2(plottableKey != 0, "writer file contains no plottable channel");

    auto* model = static_cast<SignalPlotModel*>(result.session->centerPanelModel());
    result.session->selectNode(plottableKey);
    QTRY_VERIFY_WITH_TIMEOUT(!model->busy(), 5000);
    QVERIFY(model->hasSeries());
    QCOMPARE(model->series().time.size(), model->series().value.size());
}

QTEST_MAIN(TestMdf4DocumentSession)
#include "tst_mdf4documentsession.moc"
