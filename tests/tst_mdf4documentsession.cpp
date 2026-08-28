#include "models/signalplotmodel.h"
#include "models/treemodel.h"
#include "sessions/mdf4documentsession.h"

#include <QSemaphore>
#include <QTest>
#include <QThread>

#include <atomic>
#include <cstddef>
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

// The group's own time axis, appended after the two signals.
mdf4::File documentWithMaster(std::uint64_t sampleCount = 4) {
    mdf4::File document = makeDocument(sampleCount);
    mdf4::Channel* master = document.mutable_groups(0)->add_channels();
    master->set_name("Acquisition time");
    master->set_unit("ms");
    master->set_sample_count(sampleCount);
    master->set_is_master(true);
    master->set_sync_type(1);
    master->set_cn_type(2);
    master->set_decodable(true);
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
    void masterChannelIsAnAxisNotASignal();
    void nonMonotonicDomainFallsBackToRecordIndex();
    void staleDecodeCannotReplaceNewSelection();
    void supersededDecodeIsCachedNotDiscarded();
    void reselectingAnInFlightChannelDecodesOnce();
    void leastRecentlyUsedChannelIsEvictedAtBudget();
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
    Mdf4DocumentSession session(
        QStringLiteral("timed.mf4"),
        QStringLiteral("timed.mf4"),
        documentWithMaster(),
        {},
        [](const QString&, std::uint32_t, std::uint32_t,
           std::uint64_t, std::uint64_t) { return fourSamples(10.0); });

    auto* model = static_cast<SignalPlotModel*>(session.centerPanelModel());
    session.selectNode(channelKey(session, 0));
    QTRY_VERIFY_WITH_TIMEOUT(!model->busy(), 2000);
    QCOMPARE(model->series().domainName, QStringLiteral("Acquisition time"));
    QCOMPARE(model->series().domainUnit, QStringLiteral("ms"));
}

void TestMdf4DocumentSession::masterChannelIsAnAxisNotASignal() {
    std::atomic<int> calls{0};
    Mdf4DocumentSession session(
        QStringLiteral("timed.mf4"),
        QStringLiteral("timed.mf4"),
        documentWithMaster(),
        {},
        [&](const QString&, std::uint32_t, std::uint32_t,
            std::uint64_t, std::uint64_t) {
            ++calls;
            return fourSamples(10.0);
        });

    TreeModel* tree = session.treeModel();
    const QModelIndex group = tree->index(0, 0, tree->index(0, 0));
    const QModelIndex master = tree->index(2, 0, group);
    QCOMPARE(tree->data(master, TreeModel::TitleRole).toString(),
             QStringLiteral("Acquisition time"));
    QCOMPARE(tree->data(master, TreeModel::SemanticKindRole).toInt(),
             static_cast<int>(SemanticKind::Attribute));
    QVERIFY(tree->data(master, TreeModel::SubtitleRole).toString()
                .contains(QStringLiteral("Master channel")));

    auto* model = static_cast<SignalPlotModel*>(session.centerPanelModel());
    session.selectNode(channelKey(session, 2));
    QTest::qWait(50);
    QVERIFY(!model->busy());
    QVERIFY(!model->hasSeries());
    QCOMPARE(model->name(), QStringLiteral("Acquisition time"));
    QCOMPARE(calls.load(), 0);
}

void TestMdf4DocumentSession::nonMonotonicDomainFallsBackToRecordIndex() {
    Mdf4DocumentSession session(
        QStringLiteral("unsorted.mf4"),
        QStringLiteral("unsorted.mf4"),
        documentWithMaster(),
        {},
        [](const QString&, std::uint32_t, std::uint32_t,
           std::uint64_t, std::uint64_t) {
            PlotSeries series;
            series.time = {0.0, 2.0, 1.0, 3.0};
            series.value = {10.0, 11.0, 12.0, 13.0};
            return series;
        });

    auto* model = static_cast<SignalPlotModel*>(session.centerPanelModel());
    session.selectNode(channelKey(session, 0));
    QTRY_VERIFY_WITH_TIMEOUT(!model->busy(), 2000);

    QCOMPARE(model->series().domainName, QStringLiteral("Record index"));
    QVERIFY(model->series().domainUnit.isEmpty());
    QCOMPARE(model->series().time[1], 1.0);
    QCOMPARE(model->series().time[2], 2.0);
    QCOMPARE(model->series().value[1], 11.0);
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

void TestMdf4DocumentSession::supersededDecodeIsCachedNotDiscarded() {
    QSemaphore firstStarted;
    std::atomic<int> calls{0};

    Mdf4DocumentSession session(
        QStringLiteral("superseded.mf4"),
        QStringLiteral("superseded.mf4"),
        makeDocument(),
        {},
        [&](const QString&, std::uint32_t, std::uint32_t channel,
            std::uint64_t, std::uint64_t) {
            ++calls;
            if (channel == 0) {
                firstStarted.release();
                QThread::msleep(150);
                return fourSamples(100.0);
            }
            return fourSamples(200.0);
        });

    auto* model = static_cast<SignalPlotModel*>(session.centerPanelModel());
    session.selectNode(channelKey(session, 0));
    QVERIFY(firstStarted.tryAcquire(1, 2000));
    session.selectNode(channelKey(session, 1));
    QTRY_VERIFY_WITH_TIMEOUT(!model->busy(), 2000);
    QCOMPARE(model->series().name, QStringLiteral("VehicleSpeed"));

    // Let the superseded decode of channel 0 land in the cache.
    QTest::qWait(300);
    QCOMPARE(calls.load(), 2);

    session.selectNode(channelKey(session, 0));
    QVERIFY(!model->busy());
    QCOMPARE(calls.load(), 2);
    QCOMPARE(model->series().name, QStringLiteral("EngineSpeed"));
    QCOMPARE(model->series().value.front(), 100.0);
}

void TestMdf4DocumentSession::reselectingAnInFlightChannelDecodesOnce() {
    QSemaphore firstStarted;
    std::atomic<int> calls{0};

    Mdf4DocumentSession session(
        QStringLiteral("inflight.mf4"),
        QStringLiteral("inflight.mf4"),
        makeDocument(),
        {},
        [&](const QString&, std::uint32_t, std::uint32_t channel,
            std::uint64_t, std::uint64_t) {
            ++calls;
            if (channel == 0) {
                firstStarted.release();
                QThread::msleep(150);
                return fourSamples(100.0);
            }
            return fourSamples(200.0);
        });

    auto* model = static_cast<SignalPlotModel*>(session.centerPanelModel());
    session.selectNode(channelKey(session, 0));
    QVERIFY(firstStarted.tryAcquire(1, 2000));
    session.selectNode(channelKey(session, 1));
    session.selectNode(channelKey(session, 0));

    QTRY_VERIFY_WITH_TIMEOUT(!model->busy(), 3000);
    QCOMPARE(model->series().name, QStringLiteral("EngineSpeed"));
    QCOMPARE(model->series().value.front(), 100.0);
    QTest::qWait(100);
    QCOMPARE(calls.load(), 2);
}

// The cache budget is 256 MiB, so proving eviction needs series that really
// cross it: three channels of 6M samples are 96 MiB each at 16 bytes/sample.
void TestMdf4DocumentSession::leastRecentlyUsedChannelIsEvictedAtBudget() {
    constexpr std::uint64_t samples = 6'000'000;
    std::atomic<int> calls{0};

    mdf4::File document = makeDocument(samples);
    mdf4::Channel* third = document.mutable_groups(0)->add_channels();
    third->set_name("CoolantTemp");
    third->set_unit("degC");
    third->set_sample_count(samples);
    third->set_decodable(true);
    third->set_data_type(mdf4::FLOAT_LE);
    third->set_bit_count(64);

    Mdf4DocumentSession session(
        QStringLiteral("huge.mf4"),
        QStringLiteral("huge.mf4"),
        std::move(document),
        {},
        [&](const QString&, std::uint32_t, std::uint32_t channel,
            std::uint64_t, std::uint64_t count) {
            ++calls;
            PlotSeries series;
            series.time.resize(static_cast<std::size_t>(count));
            series.value.resize(static_cast<std::size_t>(count));
            for (std::size_t i = 0; i < series.time.size(); ++i) {
                series.time[i] = static_cast<double>(i);
            }
            series.value.front() = static_cast<double>(channel);
            return series;
        });

    auto* model = static_cast<SignalPlotModel*>(session.centerPanelModel());
    for (int channel = 0; channel < 3; ++channel) {
        session.selectNode(channelKey(session, channel));
        QTRY_VERIFY_WITH_TIMEOUT(!model->busy(), 30000);
    }
    QCOMPARE(calls.load(), 3);

    // Channel 1 is still resident; channel 0 was the least recently used when
    // the third series pushed the cache over budget.
    session.selectNode(channelKey(session, 1));
    QVERIFY(!model->busy());
    QCOMPARE(calls.load(), 3);
    QCOMPARE(model->series().value.front(), 1.0);

    session.selectNode(channelKey(session, 0));
    QTRY_VERIFY_WITH_TIMEOUT(!model->busy(), 30000);
    QCOMPARE(calls.load(), 4);
    QCOMPARE(model->series().value.front(), 0.0);
}

QTEST_MAIN(TestMdf4DocumentSession)
#include "tst_mdf4documentsession.moc"
