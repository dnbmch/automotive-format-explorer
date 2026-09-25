#include "models/signalplotmodel.h"
#include "models/treemodel.h"
#include "sessions/mdf4documentsession.h"

#include <QCoreApplication>
#include <QTest>
#include <QThread>
#include <QThreadPool>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace {

using namespace std::chrono_literals;

// Channels of group 0: three signals, the group's time master, then a channel
// outside the read set.
enum Channel : int { Engine, Vehicle, Coolant, Master, Unsupported };

mdf4::File makeDocument(std::uint64_t sampleCount = 4) {
    mdf4::File document;
    document.set_version("4.20");
    mdf4::ChannelGroup* group = document.add_groups();
    group->set_name("Powertrain");
    group->set_cycle_count(sampleCount);
    group->set_storage(mdf4::ROW_DT);

    for (const char* name : {"EngineSpeed", "VehicleSpeed", "CoolantTemp"}) {
        mdf4::Channel* channel = group->add_channels();
        channel->set_name(name);
        channel->set_unit("rpm");
        channel->set_sample_count(sampleCount);
        channel->set_decodable(true);
        channel->set_data_type(mdf4::FLOAT_LE);
        channel->set_bit_count(64);
    }

    mdf4::Channel* master = group->add_channels();
    master->set_name("Acquisition time");
    master->set_unit("ms");
    master->set_sample_count(sampleCount);
    master->set_is_master(true);
    master->set_sync_type(1);
    master->set_cn_type(2);
    master->set_decodable(true);

    mdf4::Channel* unsupported = group->add_channels();
    unsupported->set_name("Label");
    unsupported->set_sample_count(sampleCount);
    unsupported->set_decodable(false);
    unsupported->set_not_decodable_reason("string data type");
    return document;
}

mdf4::ReadResult fourSamples(double base) {
    mdf4::ReadResult result;
    result.ok = true;
    result.series.time = {0.0, 1.0, 2.0, 3.0};
    result.series.value = {base, base + 1.0, base + 2.0, base + 3.0};
    return result;
}

// Channel c reads as four samples starting at 100 * (c + 1).
mdf4::ReadResult byChannel(std::uint32_t channel) {
    return fourSamples(100.0 * static_cast<double>(channel + 1));
}

// The test's view of the session's reads, shared with the read function so no
// completion timing can leave either side dangling. A gated read blocks until
// the test releases it. Waits are bounded only as deadlock guards and record a
// timeout; every one normally ends on a state change.
class Reads {
    // Defined first: the accessors below deduce their types from locked().
    template <typename F>
    bool waitUntil(F condition) {
        std::unique_lock<std::mutex> lock(_mutex);
        if (_changed.wait_for(lock, 10s, condition)) {
            return true;
        }
        _timedOut = true;
        return false;
    }
    template <typename F>
    void update(F change) {
        std::lock_guard<std::mutex> lock(_mutex);
        change();
        _changed.notify_all();
    }
    template <typename F>
    auto locked(F value) {
        std::lock_guard<std::mutex> lock(_mutex);
        return value();
    }

public:
    struct Request {
        std::uint32_t group = 0;
        std::uint32_t channel = 0;
        std::uint64_t first = 0;
        std::uint64_t count = 0;
    };

    explicit Reads(bool gated = true) : _open(!gated) {}

    std::function<mdf4::ReadResult(std::uint32_t)> result = byChannel;

    // Worker side: one read, blocked until released.
    mdf4::ReadResult read(Request request) {
        std::unique_lock<std::mutex> lock(_mutex);
        _requests.push_back(request);
        _inside++;
        _maxInside = std::max(_maxInside, _inside);
        _changed.notify_all();
        if (!_changed.wait_for(lock, 10s, [this] { return _open || _releases > 0; })) {
            _timedOut = true;
        }
        if (!_open) {
            _releases--;
        }
        _inside--;
        _events.push_back(QStringLiteral("read %1 returned").arg(request.channel));
        _changed.notify_all();
        return result(request.channel);
    }

    void note(const QString& event) {
        update([&] { _events.push_back(event); });
    }

    // Test side.
    bool waitForReads(int count) { return waitUntil([&] { return int(_requests.size()) >= count; }); }
    bool waitForEvent(const QString& event) {
        return waitUntil([&] { return _events.contains(event); });
    }
    void release() { update([this] { _releases++; }); }
    void openGate() { update([this] { _open = true; }); }
    void startTeardown() { update([this] { _teardown = true; }); }
    bool waitUntilTeardown() { return waitUntil([this] { return _teardown; }); }
    void noteTimeout() { update([this] { _timedOut = true; }); }

    std::vector<std::uint32_t> channels() {
        return locked([this] {
            std::vector<std::uint32_t> out;
            for (const Request& r : _requests) out.push_back(r.channel);
            return out;
        });
    }
    std::vector<Request> requests() { return locked([this] { return _requests; }); }
    int maxConcurrent() { return locked([this] { return _maxInside; }); }
    bool timedOut() { return locked([this] { return _timedOut; }); }
    QStringList events() { return locked([this] { return _events; }); }

private:
    std::mutex _mutex;
    std::condition_variable _changed;
    std::vector<Request> _requests;
    QStringList _events;
    int _inside = 0;
    int _maxInside = 0;
    int _releases = 0;
    bool _open = false;
    bool _teardown = false;
    bool _timedOut = false;
};

// Stands in for the reader: owned only by the session's read function and the
// copies its tasks capture, so its release marks the end of the source's life.
class Source {
public:
    explicit Source(std::shared_ptr<Reads> reads) : _reads(std::move(reads)) {}
    ~Source() { _reads->note(QStringLiteral("source released")); }

    mdf4::ReadResult read(Reads::Request request) { return _reads->read(request); }

private:
    std::shared_ptr<Reads> _reads;
};

std::unique_ptr<Mdf4DocumentSession> openSession(const std::shared_ptr<Reads>& reads,
                                                 mdf4::File document = makeDocument()) {
    auto source = std::make_shared<Source>(reads);
    return std::make_unique<Mdf4DocumentSession>(
        QStringLiteral("test.mf4"),
        QStringLiteral("test.mf4"),
        std::make_shared<const mdf4::File>(std::move(document)),
        [source](std::uint32_t group, std::uint32_t channel, std::uint64_t first,
                 std::uint64_t count) { return source->read({group, channel, first, count}); });
}

SignalPlotModel* plotOf(Mdf4DocumentSession& session) {
    return static_cast<SignalPlotModel*>(session.centerPanelModel());
}

quint64 channelKey(Mdf4DocumentSession& session, int channelIndex) {
    TreeModel* tree = session.treeModel();
    const QModelIndex group = tree->index(0, 0, tree->index(0, 0));
    return tree->data(tree->index(channelIndex, 0, group), TreeModel::NodeKeyRole).toULongLong();
}

quint64 groupKey(Mdf4DocumentSession& session) {
    TreeModel* tree = session.treeModel();
    return tree->data(tree->index(0, 0, tree->index(0, 0)), TreeModel::NodeKeyRole).toULongLong();
}

// Waits until every started read task has finished, then delivers the
// completions they posted. The global-pool wait is confined to this test
// executable; only call it when no read is blocked.
bool settle() {
    const bool done = QThreadPool::globalInstance()->waitForDone(10000);
    QCoreApplication::processEvents();
    return done;
}

bool showsChannel(SignalPlotModel* model, const char* name, double first) {
    return model->series().name == QLatin1String(name) && model->hasSeries() &&
           model->series().value.front() == first && !model->busy();
}

// Releases every read from another thread once teardown has been requested,
// so a read is still blocked when destruction starts on the test thread. The
// read may return just before the session's wait or during it; the checked
// order must hold either way. Destruction releases and joins on every path,
// including a failed check; declare it after the session.
class Releaser {
public:
    explicit Releaser(std::shared_ptr<Reads> reads)
        : _reads(std::move(reads)),
          _thread([this] {
              if (!_reads->waitUntilTeardown()) {
                  _reads->noteTimeout();
              }
              _reads->openGate();
          }) {}
    ~Releaser() {
        _reads->startTeardown();
        _reads->openGate();
        _thread.join();
    }

private:
    std::shared_ptr<Reads> _reads;
    std::thread _thread;
};

// Runs action once, from inside the first emission of signal for which when()
// holds: an ordinary same-thread observer calling back into the session.
template <typename Signal, typename When, typename Action>
void callOnce(SignalPlotModel* model, Signal signal, When when, Action action) {
    auto done = std::make_shared<bool>(false);
    QObject::connect(model, signal, model, [done, when, action]() {
        if (*done || !when()) {
            return;
        }
        *done = true;
        action();
    });
}

} // namespace

class TestMdf4DocumentSession : public QObject {
    Q_OBJECT

private slots:
    void readUsesMetadataRangeAndCachesResult();
    void timeMasterLabelsPlotDomain();
    void masterChannelIsAnAxisNotASignal();
    void nonMonotonicDomainFallsBackToRecordIndex();
    void readsNeverOverlapAndSkipIntermediateSelections();
    void reselectingTheActiveChannelReadsOnce();
    void staleCompletionIsCachedButNotShown();
    void settledSelectionDropsPendingRead();
    void failureAndEmptySuccessAreDistinct();
    void leastRecentlyUsedChannelIsEvictedAtBudget();
    void sessionFromWorkerThreadDeliversOnOwnerThread();
    void closeWaitsForActiveReadAndDropsPending();
    void closeDiscardsFinishedUndeliveredRead();
    void selectionFromCompletionNotification();
    void closeFromCompletionNotification();
};

void TestMdf4DocumentSession::readUsesMetadataRangeAndCachesResult() {
    constexpr std::uint64_t million = 1'000'000;
    auto reads = std::make_shared<Reads>(false);
    auto session = openSession(reads, makeDocument(million));
    SignalPlotModel* model = plotOf(*session);

    session->selectNode(channelKey(*session, Engine));
    QTRY_VERIFY_WITH_TIMEOUT(!model->busy(), 2000);
    const std::vector<Reads::Request> requests = reads->requests();
    QCOMPARE(requests.size(), std::size_t(1));
    QCOMPARE(requests[0].group, std::uint32_t(0));
    QCOMPARE(requests[0].channel, std::uint32_t(Engine));
    QCOMPARE(requests[0].first, std::uint64_t(0));
    QCOMPARE(requests[0].count, million);
    QCOMPARE(model->series().name, QStringLiteral("EngineSpeed"));
    QCOMPARE(model->series().domainName, QStringLiteral("Acquisition time"));
    QCOMPARE(model->series().value.front(), 100.0);

    session->selectNode(channelKey(*session, Engine));
    QVERIFY(!model->busy());
    QCOMPARE(reads->channels().size(), std::size_t(1));
    QCOMPARE(model->series().value.front(), 100.0);
    QVERIFY(!reads->timedOut());
}

void TestMdf4DocumentSession::timeMasterLabelsPlotDomain() {
    auto reads = std::make_shared<Reads>(false);
    auto session = openSession(reads);
    SignalPlotModel* model = plotOf(*session);
    session->selectNode(channelKey(*session, Vehicle));
    QTRY_VERIFY_WITH_TIMEOUT(!model->busy(), 2000);
    QCOMPARE(model->series().domainName, QStringLiteral("Acquisition time"));
    QCOMPARE(model->series().domainUnit, QStringLiteral("ms"));
}

void TestMdf4DocumentSession::masterChannelIsAnAxisNotASignal() {
    auto reads = std::make_shared<Reads>(false);
    auto session = openSession(reads);
    TreeModel* tree = session->treeModel();
    const QModelIndex group = tree->index(0, 0, tree->index(0, 0));
    const QModelIndex master = tree->index(Master, 0, group);
    QCOMPARE(tree->data(master, TreeModel::TitleRole).toString(),
             QStringLiteral("Acquisition time"));
    QCOMPARE(tree->data(master, TreeModel::SemanticKindRole).toInt(),
             static_cast<int>(SemanticKind::Attribute));
    QVERIFY(tree->data(master, TreeModel::SubtitleRole).toString()
                .contains(QStringLiteral("Master channel")));

    SignalPlotModel* model = plotOf(*session);
    session->selectNode(channelKey(*session, Master));
    QVERIFY(!model->busy());
    QVERIFY(!model->hasSeries());
    QCOMPARE(model->name(), QStringLiteral("Acquisition time"));
    QCOMPARE(model->placeholderText(), QStringLiteral("Master channel — this group's time axis"));
    QVERIFY(settle());
    QVERIFY(reads->channels().empty());
}

void TestMdf4DocumentSession::nonMonotonicDomainFallsBackToRecordIndex() {
    auto reads = std::make_shared<Reads>(false);
    reads->result = [](std::uint32_t) {
        mdf4::ReadResult result;
        result.ok = true;
        result.series.time = {0.0, 2.0, 1.0, 3.0};
        result.series.value = {10.0, 11.0, 12.0, 13.0};
        return result;
    };
    auto session = openSession(reads);
    SignalPlotModel* model = plotOf(*session);
    session->selectNode(channelKey(*session, Engine));
    QTRY_VERIFY_WITH_TIMEOUT(!model->busy(), 2000);

    QCOMPARE(model->series().domainName, QStringLiteral("Record index"));
    QVERIFY(model->series().domainUnit.isEmpty());
    QCOMPARE(model->series().time[1], 1.0);
    QCOMPARE(model->series().time[2], 2.0);
    QCOMPARE(model->series().value[1], 11.0);
}

// A -> B -> C while A reads: C replaces B as the pending selection, so the
// reader sees A then C, one at a time, and B never.
void TestMdf4DocumentSession::readsNeverOverlapAndSkipIntermediateSelections() {
    auto reads = std::make_shared<Reads>();
    auto session = openSession(reads);
    SignalPlotModel* model = plotOf(*session);

    session->selectNode(channelKey(*session, Engine));
    QVERIFY(reads->waitForReads(1));
    session->selectNode(channelKey(*session, Vehicle));
    session->selectNode(channelKey(*session, Coolant));
    QVERIFY(model->busy());
    QCOMPARE(model->series().name, QStringLiteral("CoolantTemp"));
    QCOMPARE(reads->channels(), std::vector<std::uint32_t>{Engine});

    reads->release();
    // The next read starts from A's completion, delivered by the event loop.
    QTRY_COMPARE_WITH_TIMEOUT(reads->channels().size(), std::size_t(2), 5000);
    QCOMPARE(reads->channels(), (std::vector<std::uint32_t>{Engine, Coolant}));
    QVERIFY(model->busy());
    reads->release();
    QTRY_VERIFY_WITH_TIMEOUT(showsChannel(model, "CoolantTemp", 300.0), 5000);
    QVERIFY(settle());
    QCOMPARE(reads->channels(), (std::vector<std::uint32_t>{Engine, Coolant}));
    QCOMPARE(reads->maxConcurrent(), 1);
    QVERIFY(!reads->timedOut());
}

// A -> B -> A: returning to the active channel drops B and reuses A's read.
void TestMdf4DocumentSession::reselectingTheActiveChannelReadsOnce() {
    auto reads = std::make_shared<Reads>();
    auto session = openSession(reads);
    SignalPlotModel* model = plotOf(*session);

    session->selectNode(channelKey(*session, Engine));
    QVERIFY(reads->waitForReads(1));
    session->selectNode(channelKey(*session, Vehicle));
    session->selectNode(channelKey(*session, Engine));
    QVERIFY(model->busy());

    reads->release();
    QTRY_VERIFY_WITH_TIMEOUT(showsChannel(model, "EngineSpeed", 100.0), 5000);
    QVERIFY(settle());
    QCOMPARE(reads->channels(), std::vector<std::uint32_t>{Engine});
    QVERIFY(!reads->timedOut());
}

// A completion for a channel that is no longer selected is kept, not shown.
void TestMdf4DocumentSession::staleCompletionIsCachedButNotShown() {
    auto reads = std::make_shared<Reads>();
    auto session = openSession(reads);
    SignalPlotModel* model = plotOf(*session);

    session->selectNode(channelKey(*session, Engine));
    QVERIFY(reads->waitForReads(1));
    session->selectNode(channelKey(*session, Master));
    reads->release();
    QVERIFY(settle());
    QCOMPARE(model->name(), QStringLiteral("Acquisition time"));
    QVERIFY(!model->hasSeries());
    QVERIFY(!model->busy());

    session->selectNode(channelKey(*session, Engine));
    QVERIFY(showsChannel(model, "EngineSpeed", 100.0));
    QVERIFY(settle());
    QCOMPARE(reads->channels(), std::vector<std::uint32_t>{Engine});
}

// A cached channel, a non-channel row, the master and an unsupported channel
// each settle the view without a read, so the waiting selection is dropped.
void TestMdf4DocumentSession::settledSelectionDropsPendingRead() {
    enum class Settle { Cached, GroupRow, Master, Unsupported };
    for (Settle kind : {Settle::Cached, Settle::GroupRow, Settle::Master, Settle::Unsupported}) {
        auto reads = std::make_shared<Reads>();
        auto session = openSession(reads);
        SignalPlotModel* model = plotOf(*session);
        std::vector<std::uint32_t> expected;
        if (kind == Settle::Cached) {
            session->selectNode(channelKey(*session, Vehicle));
            reads->release();
            QTRY_VERIFY_WITH_TIMEOUT(showsChannel(model, "VehicleSpeed", 200.0), 5000);
            QVERIFY(settle());
            expected.push_back(Vehicle);
        }

        session->selectNode(channelKey(*session, Engine));
        QVERIFY(reads->waitForReads(int(expected.size()) + 1));
        session->selectNode(channelKey(*session, Coolant));
        switch (kind) {
        case Settle::Cached:
            session->selectNode(channelKey(*session, Vehicle));
            break;
        case Settle::GroupRow:
            session->selectNode(groupKey(*session));
            break;
        case Settle::Master:
            session->selectNode(channelKey(*session, Master));
            break;
        case Settle::Unsupported:
            session->selectNode(channelKey(*session, Unsupported));
            break;
        }
        QVERIFY(!model->busy());
        reads->release();
        QVERIFY(settle());
        expected.push_back(Engine);
        QCOMPARE(reads->channels(), expected);
        QVERIFY(!model->busy());
        if (kind == Settle::Cached) {
            QVERIFY(showsChannel(model, "VehicleSpeed", 200.0));
        } else {
            QVERIFY(!model->hasSeries());
        }
        QVERIFY(!reads->timedOut());
    }
}

// A failed read explains itself and is read again next time; an empty success
// says there are no samples and is cached like any other result.
void TestMdf4DocumentSession::failureAndEmptySuccessAreDistinct() {
    auto reads = std::make_shared<Reads>(false);
    reads->result = [](std::uint32_t channel) {
        mdf4::ReadResult result;
        if (channel == Engine) {
            result.location = "DG[0]/CG[0]/CN[0]";
            result.message = "source file changed since it was opened; reload it";
        } else {
            result.ok = true;
        }
        return result;
    };
    auto session = openSession(reads);
    SignalPlotModel* model = plotOf(*session);

    session->selectNode(channelKey(*session, Engine));
    QTRY_VERIFY_WITH_TIMEOUT(!model->busy(), 5000);
    QVERIFY(!model->hasSeries());
    QCOMPARE(model->name(), QStringLiteral("EngineSpeed"));
    QCOMPARE(model->placeholderText(),
             QStringLiteral("Samples could not be read: source file changed since it was "
                            "opened; reload it (DG[0]/CG[0]/CN[0])"));

    session->selectNode(channelKey(*session, Vehicle));
    QTRY_VERIFY_WITH_TIMEOUT(!model->busy() && model->name() == QStringLiteral("VehicleSpeed"), 5000);
    QVERIFY(!model->hasSeries());
    QCOMPARE(model->placeholderText(), QStringLiteral("No samples recorded"));

    session->selectNode(channelKey(*session, Engine));
    QTRY_VERIFY_WITH_TIMEOUT(!model->busy() && model->name() == QStringLiteral("EngineSpeed"), 5000);
    QVERIFY(model->placeholderText().startsWith(QStringLiteral("Samples could not be read")));
    session->selectNode(channelKey(*session, Vehicle));
    QVERIFY(!model->busy());
    QCOMPARE(model->placeholderText(), QStringLiteral("No samples recorded"));
    QVERIFY(settle());
    QCOMPARE(reads->channels(), (std::vector<std::uint32_t>{Engine, Vehicle, Engine}));
}

// The cache budget is 256 MiB, so proving eviction needs series that really
// cross it: three channels of 6M samples are 96 MiB each at 16 bytes/sample.
void TestMdf4DocumentSession::leastRecentlyUsedChannelIsEvictedAtBudget() {
    constexpr std::uint64_t samples = 6'000'000;
    auto reads = std::make_shared<Reads>(false);
    reads->result = [](std::uint32_t channel) {
        mdf4::ReadResult result;
        result.ok = true;
        result.series.time.resize(static_cast<std::size_t>(samples));
        result.series.value.resize(static_cast<std::size_t>(samples));
        for (std::size_t i = 0; i < result.series.time.size(); ++i) {
            result.series.time[i] = static_cast<double>(i);
        }
        result.series.value.front() = static_cast<double>(channel);
        return result;
    };
    auto session = openSession(reads, makeDocument(samples));
    SignalPlotModel* model = plotOf(*session);

    for (int channel : {Engine, Vehicle, Coolant}) {
        session->selectNode(channelKey(*session, channel));
        QTRY_VERIFY_WITH_TIMEOUT(!model->busy(), 30000);
    }
    QCOMPARE(reads->channels().size(), std::size_t(3));

    // Vehicle is still resident; Engine was the least recently used when the
    // third series pushed the cache over budget.
    session->selectNode(channelKey(*session, Vehicle));
    QVERIFY(!model->busy());
    QCOMPARE(reads->channels().size(), std::size_t(3));
    QCOMPARE(model->series().value.front(), 1.0);

    session->selectNode(channelKey(*session, Engine));
    QTRY_VERIFY_WITH_TIMEOUT(!model->busy(), 30000);
    QCOMPARE(reads->channels().size(), std::size_t(4));
    QCOMPARE(model->series().value.front(), 0.0);
}

// The open worker builds the session and hands it to the controller thread;
// the watcher receiving completions moves with the models, or no completion
// would ever be delivered.
void TestMdf4DocumentSession::sessionFromWorkerThreadDeliversOnOwnerThread() {
    auto reads = std::make_shared<Reads>(false);
    std::unique_ptr<Mdf4DocumentSession> session;
    QThread* owner = QThread::currentThread();
    std::thread worker([&] {
        session = openSession(reads);
        session->moveModelsToThread(owner);
    });
    worker.join();
    SignalPlotModel* model = plotOf(*session);
    QCOMPARE(model->thread(), owner);

    session->selectNode(channelKey(*session, Coolant));
    QTRY_VERIFY_WITH_TIMEOUT(showsChannel(model, "CoolantTemp", 300.0), 5000);
}

// Closing with a read running and another pending: the pending one never
// starts, and destruction returns only after the running read did. The source
// stays alive until that read's task has let go of it.
void TestMdf4DocumentSession::closeWaitsForActiveReadAndDropsPending() {
    auto reads = std::make_shared<Reads>();
    auto session = openSession(reads);
    Releaser releaser(reads);

    session->selectNode(channelKey(*session, Engine));
    QVERIFY(reads->waitForReads(1));
    session->selectNode(channelKey(*session, Coolant));

    reads->startTeardown();
    session.reset();
    QVERIFY(reads->events().contains(QStringLiteral("read 0 returned")));
    QVERIFY(reads->waitForEvent(QStringLiteral("source released")));
    QCOMPARE(reads->events(),
             (QStringList{QStringLiteral("read 0 returned"), QStringLiteral("source released")}));
    QVERIFY(settle());
    QCOMPARE(reads->channels(), std::vector<std::uint32_t>{Engine});
    QVERIFY(!reads->timedOut());
}

// A read that finished while no event loop ran is discarded by destruction;
// its queued completion is never delivered to the closed session.
void TestMdf4DocumentSession::closeDiscardsFinishedUndeliveredRead() {
    auto reads = std::make_shared<Reads>();
    auto session = openSession(reads);

    session->selectNode(channelKey(*session, Engine));
    QVERIFY(reads->waitForReads(1));
    session->selectNode(channelKey(*session, Coolant));
    reads->release();
    QVERIFY(QThreadPool::globalInstance()->waitForDone(10000));

    session.reset();
    QVERIFY(reads->waitForEvent(QStringLiteral("source released")));
    QCoreApplication::processEvents();
    QCOMPARE(reads->channels(), std::vector<std::uint32_t>{Engine});
    QCOMPARE(reads->events(),
             (QStringList{QStringLiteral("read 0 returned"), QStringLiteral("source released")}));
    QVERIFY(!reads->timedOut());
}

// An observer of the completed series selects another uncached channel from
// inside the notification. The completion already owns its own result, so the
// new read gets its own, and the plot stays busy for it.
void TestMdf4DocumentSession::selectionFromCompletionNotification() {
    auto reads = std::make_shared<Reads>();
    auto session = openSession(reads);
    SignalPlotModel* model = plotOf(*session);
    Mdf4DocumentSession* raw = session.get();
    const quint64 coolant = channelKey(*session, Coolant);
    callOnce(model, &SignalPlotModel::seriesChanged,
             [model] { return model->hasSeries() && model->series().name == QStringLiteral("EngineSpeed"); },
             [raw, coolant] { raw->selectNode(coolant); });

    session->selectNode(channelKey(*session, Engine));
    reads->release();
    QTRY_COMPARE_WITH_TIMEOUT(reads->channels().size(), std::size_t(2), 5000);
    QCOMPARE(model->series().name, QStringLiteral("CoolantTemp"));
    QVERIFY(model->busy());
    QVERIFY(!model->hasSeries());

    reads->release();
    QTRY_VERIFY_WITH_TIMEOUT(showsChannel(model, "CoolantTemp", 300.0), 5000);
    session->selectNode(channelKey(*session, Engine));
    QVERIFY(showsChannel(model, "EngineSpeed", 100.0));
    QVERIFY(settle());
    QCOMPARE(reads->channels(), (std::vector<std::uint32_t>{Engine, Coolant}));
    QCOMPARE(reads->maxConcurrent(), 1);
    QVERIFY(!reads->timedOut());
}

// The completion's final notification is the idle state. Its observer selects
// another channel, which starts a read, then closes the session: destruction
// from inside the notification waits for that read, and nothing touches the
// session afterwards.
void TestMdf4DocumentSession::closeFromCompletionNotification() {
    auto reads = std::make_shared<Reads>();
    auto session = openSession(reads);
    SignalPlotModel* model = plotOf(*session);
    Releaser releaser(reads);
    const quint64 coolant = channelKey(*session, Coolant);
    bool closed = false;
    callOnce(model, &SignalPlotModel::busyChanged,
             [model] { return !model->busy() && model->series().name == QStringLiteral("EngineSpeed"); },
             [&] {
                 session->selectNode(coolant);
                 reads->startTeardown();
                 session.reset();
                 closed = true;
             });

    session->selectNode(channelKey(*session, Engine));
    QVERIFY(reads->waitForReads(1));
    reads->release();
    QTRY_VERIFY_WITH_TIMEOUT(closed, 5000);
    QVERIFY(reads->waitForEvent(QStringLiteral("source released")));
    QVERIFY(settle());
    QCOMPARE(reads->channels(), (std::vector<std::uint32_t>{Engine, Coolant}));
    QCOMPARE(reads->events(),
             (QStringList{QStringLiteral("read 0 returned"), QStringLiteral("read 2 returned"),
                          QStringLiteral("source released")}));
    QCOMPARE(reads->maxConcurrent(), 1);
    QVERIFY(!reads->timedOut());
}

QTEST_MAIN(TestMdf4DocumentSession)
#include "tst_mdf4documentsession.moc"
