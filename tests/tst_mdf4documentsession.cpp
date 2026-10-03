#include "core/appcontroller.h"
#include "models/detailmodel.h"
#include "models/signalplotmodel.h"
#include "models/treemodel.h"
#include "plotscan.h"
#include "sessions/mdf4documentsession.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QLocale>
#include <QSignalSpy>
#include <QTest>
#include <QThread>
#include <QThreadPool>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <thread>
#include <tuple>
#include <vector>

namespace {

// The heap blocks of a result the session never hands out. The allocation
// functions below replace the global ones and track, by address, the blocks
// of one watched size: the size of that result's largest arrays.
namespace heap {

std::atomic<std::size_t> watchedSize{0};
std::array<std::atomic<void*>, 4> watched{};

// Tracks the blocks of `size` allocated from now on; 0 tracks none.
void watch(std::size_t size) {
    watchedSize = 0;
    for (std::atomic<void*>& block : watched) {
        block = nullptr;
    }
    watchedSize = size;
}

int live() {
    int count = 0;
    for (const std::atomic<void*>& block : watched) {
        count += block.load() != nullptr ? 1 : 0;
    }
    return count;
}

} // namespace heap

} // namespace

void* operator new(std::size_t size) {
    void* block = std::malloc(size > 0 ? size : 1);
    if (!block) {
        throw std::bad_alloc();
    }
    if (size > 0 && size == heap::watchedSize.load()) {
        for (std::atomic<void*>& slot : heap::watched) {
            void* empty = nullptr;
            if (slot.compare_exchange_strong(empty, block)) {
                break;
            }
        }
    }
    return block;
}

void operator delete(void* block) noexcept {
    for (std::atomic<void*>& slot : heap::watched) {
        void* expected = block;
        if (block && slot.load() == block && slot.compare_exchange_strong(expected, nullptr)) {
            break;
        }
    }
    std::free(block);
}

void operator delete(void* block, std::size_t) noexcept {
    operator delete(block);
}

namespace {

using namespace std::chrono_literals;

// Channels of group 0: three signals, the group's time master, then a channel
// outside the read set. Group 1 has one signal and no master of its own.
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

    mdf4::ChannelGroup* body = document.add_groups();
    body->set_name("Body");
    body->set_cycle_count(sampleCount);
    mdf4::Channel* door = body->add_channels();
    door->set_name("DoorState");
    door->set_sample_count(sampleCount);
    door->set_decodable(true);
    return document;
}

// The source's axes: group 0 has its own time master, group 1 none.
mdf4::Axis timeMaster(std::uint32_t group) {
    return group == 0 ? mdf4::Axis{mdf4::AxisKind::Master, 0, Master}
                      : mdf4::Axis{mdf4::AxisKind::SampleIndex, group, 0};
}

mdf4::ScanResult finished(mdf4::Status status, const char* message, std::string location = {}) {
    mdf4::ScanResult result;
    result.outcome.status = status;
    result.outcome.message = message;
    result.outcome.location = std::move(location);
    return result;
}

// The test's view of the session's scans, shared with the scan function so no
// completion timing can leave either side dangling. Channel c holds `count`
// samples: coordinate i, value 100 * (c + 1) + i. A scan delivers its clamped
// request in chunks, checks the cancel flag before each chunk, and when gated
// blocks before its first chunk until the test releases it or the session
// cancels it. Waits are bounded only as deadlock guards and record a timeout.
class Scans {
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

    explicit Scans(bool gated = true, std::uint64_t count = 4) : _count(count), _open(!gated) {}

    // Set before the first scan.
    std::function<std::pair<double, double>(std::uint32_t, std::uint64_t)> sample =
        [](std::uint32_t channel, std::uint64_t index) {
            return std::make_pair(static_cast<double>(index),
                                  100.0 * static_cast<double>(channel + 1) + static_cast<double>(index));
        };
    // A finished outcome in place of the samples.
    std::function<std::optional<mdf4::ScanResult>(const Request&)> outcome;
    // The source ends after this many samples: Ok and Short.
    std::uint64_t available = std::numeric_limits<std::uint64_t>::max();
    std::size_t chunk = 4096;
    bool honorCancel = true;
    // Reports a quarter done before waiting at the gate.
    bool progressBeforeGate = false;
    // A misbehaving source that reports progress after it saw the cancellation.
    bool progressAfterCancel = false;
    // Progress calls per chunk.
    int progressBurst = 1;
    // Time each chunk takes.
    std::chrono::milliseconds chunkTime{0};

    // Worker side.
    mdf4::ScanResult scan(const Request& request, const mdf4::Control& control,
                          const mdf4::Visitor& visitor) {
        const auto cancelled = [&] {
            return honorCancel && control.cancel && control.cancel->load();
        };
        if (progressBeforeGate && control.progress) {
            control.progress(request.count * 4, request.count * 16);
        }
        {
            std::unique_lock<std::mutex> lock(_mutex);
            _requests.push_back(request);
            _inside++;
            _maxInside = std::max(_maxInside, _inside);
            _changed.notify_all();
            const auto deadline = std::chrono::steady_clock::now() + 10s;
            while (!_open && _releases == 0 && !cancelled()) {
                if (std::chrono::steady_clock::now() > deadline) {
                    _timedOut = true;
                    break;
                }
                _changed.wait_for(lock, 1ms);
            }
            if (!_open && _releases > 0 && !cancelled()) {
                _releases--;
            }
        }
        mdf4::ScanResult result = deliver(request, control, visitor, cancelled);
        update([&] {
            _inside--;
            _events.push_back(QStringLiteral("scan %1 returned").arg(request.channel));
        });
        return result;
    }

    void note(const QString& event) {
        update([&] { _events.push_back(event); });
    }

    // Test side.
    bool waitForScans(int count) {
        return waitUntil([&] { return int(_requests.size()) >= count; });
    }
    bool waitForEvent(const QString& event) {
        return waitUntil([&] { return _events.contains(event); });
    }
    void release() { update([this] { _releases++; }); }
    void openGate() { update([this] { _open = true; }); }
    void closeGate() { update([this] { _open = false; }); }
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
    template <typename Cancelled>
    mdf4::ScanResult deliver(const Request& request, const mdf4::Control& control,
                             const mdf4::Visitor& visitor, Cancelled cancelled) {
        mdf4::ScanResult result;
        if (outcome) {
            if (std::optional<mdf4::ScanResult> given = outcome(request)) {
                return *given;
            }
        }
        result.firstSample = std::min(request.first, _count);
        result.sampleCount = std::min(request.count, _count - result.firstSample);
        const std::uint64_t end = std::min(result.firstSample + result.sampleCount,
                                           std::max(available, result.firstSample));
        const std::uint64_t total = result.sampleCount * 16;
        std::vector<double> time;
        std::vector<double> value;
        for (std::uint64_t first = result.firstSample; first < end;) {
            if (cancelled()) {
                if (progressAfterCancel && control.progress) {
                    std::this_thread::sleep_for(150ms);
                    control.progress(total * 3 / 4, total);
                }
                result.outcome.status = mdf4::Status::Cancelled;
                result.outcome.message = "cancelled";
                return result;
            }
            for (int call = 1; control.progress && call <= progressBurst; ++call) {
                control.progress((first - result.firstSample) * 16 + std::uint64_t(call), total);
            }
            const auto size = static_cast<std::size_t>(std::min<std::uint64_t>(chunk, end - first));
            time.resize(size);
            value.resize(size);
            for (std::size_t i = 0; i < size; ++i) {
                std::tie(time[i], value[i]) = sample(request.channel, first + i);
            }
            switch (visitor(mdf4::SampleChunk{first, time.data(), value.data(), size})) {
            case mdf4::Visit::Continue:
                break;
            case mdf4::Visit::Cancel:
                result.outcome.status = mdf4::Status::Cancelled;
                result.outcome.message = "cancelled by the visitor";
                return result;
            case mdf4::Visit::ResourceLimit:
                result.outcome.status = mdf4::Status::ResourceLimit;
                result.outcome.resource = mdf4::Resource::Consumer;
                result.outcome.message = "refused by the visitor";
                return result;
            }
            first += size;
            result.samplesRead = first - result.firstSample;
            std::this_thread::sleep_for(chunkTime);
        }
        result.outcome.status = mdf4::Status::Ok;
        result.outcome.message = result.samplesRead < result.sampleCount ? "the source ends early" : "";
        result.coverage = result.samplesRead < result.sampleCount ? mdf4::Coverage::Short
                                                                  : mdf4::Coverage::Complete;
        return result;
    }

    const std::uint64_t _count;
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

// Stands in for the reader: owned only by the session's scan function and the
// copies its tasks capture, so its release marks the end of the source's life.
class Source {
public:
    explicit Source(std::shared_ptr<Scans> scans) : _scans(std::move(scans)) {}
    ~Source() { _scans->note(QStringLiteral("source released")); }

    mdf4::ScanResult scan(const Scans::Request& request, const mdf4::Control& control,
                          const mdf4::Visitor& visitor) {
        return _scans->scan(request, control, visitor);
    }

private:
    std::shared_ptr<Scans> _scans;
};

std::unique_ptr<Mdf4DocumentSession> openSession(
    const std::shared_ptr<Scans>& scans, mdf4::File document = makeDocument(),
    const Mdf4SessionLimits& limits = {},
    Mdf4DocumentSession::AxisFunction axis = timeMaster) {
    auto source = std::make_shared<Source>(scans);
    return std::make_unique<Mdf4DocumentSession>(
        QStringLiteral("test.mf4"),
        QStringLiteral("test.mf4"),
        std::make_shared<const mdf4::File>(std::move(document)),
        [source](std::uint32_t group, std::uint32_t channel, std::uint64_t first,
                 std::uint64_t count, const mdf4::Control& control,
                 const mdf4::Visitor& visitor) {
            return source->scan({group, channel, first, count}, control, visitor);
        },
        std::move(axis), QList<DiagnosticMessage>{}, limits);
}

// Opens through the controller: the adapter hands over a session whose scans
// the test gates.
class SessionAdapter final : public FormatAdapter {
public:
    explicit SessionAdapter(std::shared_ptr<Scans> scans) : _scans(std::move(scans)) {}

    LoadResult load(const QString&) const override {
        LoadResult result;
        result.session = openSession(_scans);
        return result;
    }

private:
    std::shared_ptr<Scans> _scans;
};

SignalPlotModel* plotOf(Mdf4DocumentSession& session) {
    return static_cast<SignalPlotModel*>(session.centerPanelModel());
}

quint64 channelKey(Mdf4DocumentSession& session, int channelIndex, int groupIndex = 0) {
    TreeModel* tree = session.treeModel();
    const QModelIndex group = tree->index(groupIndex, 0, tree->index(0, 0));
    return tree->data(tree->index(channelIndex, 0, group), TreeModel::NodeKeyRole).toULongLong();
}

quint64 groupKey(Mdf4DocumentSession& session) {
    TreeModel* tree = session.treeModel();
    return tree->data(tree->index(0, 0, tree->index(0, 0)), TreeModel::NodeKeyRole).toULongLong();
}

// A row of the first group: one of its channels, or kGroupRow for the group.
constexpr int kGroupRow = -1;

quint64 rowKey(Mdf4DocumentSession& session, int row) {
    return row == kGroupRow ? groupKey(session) : channelKey(session, row);
}

// The Name field of the detail panel's first card.
QString detailName(DetailModel* detail) {
    const QVariantList fields = detail->data(detail->index(0), DetailModel::FieldsRole).toList();
    for (const QVariant& field : fields) {
        const QVariantMap entry = field.toMap();
        if (entry.value(QStringLiteral("key")) == QStringLiteral("Name")) {
            return entry.value(QStringLiteral("value")).toString();
        }
    }
    return {};
}

// No scan in flight and none waiting for the event loop: runs the pool dry and
// delivers what it posted, again while completions start further scans. Only
// call it when no scan is blocked.
bool settle(Mdf4DocumentSession& session) {
    for (int round = 0; round < 50; ++round) {
        if (!QThreadPool::globalInstance()->waitForDone(10000)) {
            return false;
        }
        QCoreApplication::processEvents();
        if (session.resultBytes().reserved == 0 &&
            QThreadPool::globalInstance()->activeThreadCount() == 0) {
            return true;
        }
    }
    return false;
}

// The plot shows channel `name` settled: its samples exact, starting at `first`.
bool showsExact(SignalPlotModel* model, const char* name, double first) {
    return model->name() == QLatin1String(name) &&
           model->plotState() == SignalPlotModel::Detail && !model->busy() &&
           model->window()->value.front() == first;
}

// Releases every scan from another thread once teardown has been requested,
// so a scan is still blocked when destruction starts on the test thread.
// Destruction releases and joins on every path, including a failed check;
// declare it after the session.
class Releaser {
public:
    explicit Releaser(std::shared_ptr<Scans> scans)
        : _scans(std::move(scans)),
          _thread([this] {
              if (!_scans->waitUntilTeardown()) {
                  _scans->noteTimeout();
              }
              _scans->openGate();
          }) {}
    ~Releaser() {
        _scans->startTeardown();
        _scans->openGate();
        _thread.join();
    }

private:
    std::shared_ptr<Scans> _scans;
    std::thread _thread;
};

// Runs action once, from inside the first emission of signal for which when()
// holds: an ordinary same-thread observer calling back into the session.
template <typename Model, typename Signal, typename When, typename Action>
void callOnce(Model* model, Signal signal, When when, Action action) {
    auto done = std::make_shared<bool>(false);
    QObject::connect(model, signal, model, [done, when, action]() {
        if (*done || !when()) {
            return;
        }
        *done = true;
        action();
    });
}

// A selection's notifications, in the order it makes them: the detail panel's
// reset and raw form, then the plot's content.
enum Hook : int { DetailReset, DetailRawForm, PlotContent };

const char* hookName(int hook) {
    switch (hook) {
    case DetailReset: return "detail reset";
    case DetailRawForm: return "detail raw form";
    default: return "plot content";
    }
}

// Runs action once, from inside the first `hook` notification after this call.
template <typename Action>
void callOnSelection(Mdf4DocumentSession& session, int hook, Action action) {
    const auto always = [] { return true; };
    DetailModel* detail = session.detailModel();
    switch (hook) {
    case DetailReset:
        callOnce(detail, &DetailModel::modelReset, always, action);
        break;
    case DetailRawForm:
        callOnce(detail, &DetailModel::rawJsonChanged, always, action);
        break;
    default:
        callOnce(plotOf(session), &SignalPlotModel::contentChanged, always, action);
        break;
    }
}

// A channel too dense for one window at full view: 4.3 M samples.
constexpr std::uint64_t kDense = 4'300'000;

// The largest heap block of an overview of `stated` samples: its bins.
std::size_t binBytes(std::uint64_t stated) {
    return PlotOverviewBuilder(stated).finish(stated)->bins.capacity() * sizeof(PlotBin);
}

} // namespace

class TestMdf4DocumentSession : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void overviewThenExactWindowOfTheView();
    void domainComesFromTheSourceAxis();
    void masterChannelIsAnAxisNotASignal();
    void regressingTimePlotsSampleIndices();
    void supersededScansAreCancelled();
    void reselectingAfterCancelScansAgain();
    void reselectingTheRunningChannelKeepsItsScan();
    void cancelledWorkIsNeitherShownNorKept();
    void settledSelectionDropsPendingAndCancels();
    void outcomesStayDistinct();
    void zoomScansTheWindowTheViewNeeds();
    void rapidZoomCancelsObsoleteWindows();
    void staleProgressNeverReachesThePlot();
    void progressIsCoalesced();
    void reservationBecomesTheResultChargeWithoutGap();
    void obsoleteResultReleasedBeforeTheNextAdmission_data();
    void obsoleteResultReleasedBeforeTheNextAdmission();
    void unheldResultsEvictedLeastRecentlyUsed();
    void heldResultsRefuseAdmission();
    void sessionFromWorkerThreadDeliversOnOwnerThread();
    void closeCancelsScanInFlightAndDropsPending();
    void closeDiscardsFinishedUndeliveredScan();
    void selectionFromCompletionNotification();
    void selectionFromBusyNotificationStaysBusy();
    void closeFromCompletionNotification();
    void selectionFromSelectionNotification_data();
    void selectionFromSelectionNotification();
    void closeFromSelectionNotification_data();
    void closeFromSelectionNotification();
    void closingTabWithScanInFlight();
    void treeAdmittedUpToItsAllowance();
    void treeOverAllowanceShowsOnlyTheFileRow();
};

void TestMdf4DocumentSession::initTestCase() {
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
}

// The overview scans the whole metadata range; the view it opens with fits one
// window, so the exact samples follow. Selecting the channel again shows both
// from the cache.
void TestMdf4DocumentSession::overviewThenExactWindowOfTheView() {
    auto scans = std::make_shared<Scans>(false);
    auto session = openSession(scans);
    SignalPlotModel* model = plotOf(*session);

    session->selectNode(channelKey(*session, Engine));
    QVERIFY(settle(*session));
    const std::vector<Scans::Request> requests = scans->requests();
    QCOMPARE(requests.size(), std::size_t(2));
    QCOMPARE(requests[0].channel, std::uint32_t(Engine));
    QCOMPARE(requests[0].first, std::uint64_t(0));
    QCOMPARE(requests[0].count, std::uint64_t(4));
    QCOMPARE(requests[1].first, std::uint64_t(0));
    QCOMPARE(requests[1].count, std::uint64_t(4));
    QVERIFY(showsExact(model, "EngineSpeed", 100.0));
    QCOMPARE(model->countText(), QStringLiteral("4 samples"));
    QCOMPARE(model->window()->time.size(), std::size_t(4));

    session->selectNode(channelKey(*session, Coolant));
    QVERIFY(settle(*session));
    session->selectNode(channelKey(*session, Engine));
    QVERIFY(showsExact(model, "EngineSpeed", 100.0));
    QVERIFY(settle(*session));
    QCOMPARE(scans->channels(), (std::vector<std::uint32_t>{Engine, Engine, Coolant, Coolant}));
    QVERIFY(!scans->timedOut());
}

// The domain is the axis the source resolved: the group's own master, another
// group's master, or the sample index. The session looks for no master itself.
void TestMdf4DocumentSession::domainComesFromTheSourceAxis() {
    auto scans = std::make_shared<Scans>(false);
    auto session = openSession(scans);
    SignalPlotModel* model = plotOf(*session);
    session->selectNode(channelKey(*session, Vehicle));
    QCOMPARE(model->domainName(), QStringLiteral("Acquisition time"));
    QCOMPARE(model->domainUnit(), QStringLiteral("ms"));
    session->selectNode(channelKey(*session, 0, 1));
    QCOMPARE(model->name(), QStringLiteral("DoorState"));
    QCOMPARE(model->domainName(), QStringLiteral("Sample index"));
    QVERIFY(model->domainUnit().isEmpty());
    QVERIFY(settle(*session));

    auto remote = openSession(scans, makeDocument(), {}, [](std::uint32_t) {
        return mdf4::Axis{mdf4::AxisKind::Master, 0, Master};
    });
    SignalPlotModel* remoteModel = plotOf(*remote);
    remote->selectNode(channelKey(*remote, 0, 1));
    QCOMPARE(remoteModel->domainName(), QStringLiteral("Acquisition time"));
    QCOMPARE(remoteModel->domainUnit(), QStringLiteral("ms"));
    QVERIFY(settle(*remote));
}

void TestMdf4DocumentSession::masterChannelIsAnAxisNotASignal() {
    auto scans = std::make_shared<Scans>(false);
    auto session = openSession(scans);
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
    QVERIFY(!model->hasSamples());
    QCOMPARE(model->plotState(), SignalPlotModel::Empty);
    QCOMPARE(model->name(), QStringLiteral("Acquisition time"));
    QCOMPARE(model->message(), QStringLiteral("Master channel — this group's time axis"));
    QVERIFY(settle(*session));
    QVERIFY(scans->channels().empty());
}

// One coordinate going back makes the whole result set, overview and window,
// plot against sample indices.
void TestMdf4DocumentSession::regressingTimePlotsSampleIndices() {
    auto scans = std::make_shared<Scans>(false);
    scans->sample = [](std::uint32_t, std::uint64_t index) {
        return std::make_pair(index == 2 ? 0.5 : static_cast<double>(index),
                              10.0 + static_cast<double>(index));
    };
    auto session = openSession(scans);
    SignalPlotModel* model = plotOf(*session);
    session->selectNode(channelKey(*session, Engine));
    QVERIFY(settle(*session));

    QCOMPARE(model->domainName(), QStringLiteral("Sample index"));
    QVERIFY(model->domainUnit().isEmpty());
    QCOMPARE(model->plotState(), SignalPlotModel::Detail);
    QCOMPARE(model->window()->time, (std::vector<double>{0.0, 1.0, 2.0, 3.0}));
    QCOMPARE(model->window()->value[2], 12.0);
}

// A -> B -> C while A scans: A is cancelled, C replaces B as the pending scan,
// so the source sees A, then C, one at a time, and B never.
void TestMdf4DocumentSession::supersededScansAreCancelled() {
    auto scans = std::make_shared<Scans>();
    auto session = openSession(scans);
    SignalPlotModel* model = plotOf(*session);

    session->selectNode(channelKey(*session, Engine));
    QVERIFY(scans->waitForScans(1));
    session->selectNode(channelKey(*session, Vehicle));
    session->selectNode(channelKey(*session, Coolant));
    QVERIFY(model->busy());
    QCOMPARE(model->name(), QStringLiteral("CoolantTemp"));
    QCOMPARE(model->plotState(), SignalPlotModel::Pending);

    // A sees its cancellation; its completion starts C, then C's window.
    QVERIFY(scans->waitForEvent(QStringLiteral("scan 0 returned")));
    QTRY_COMPARE_WITH_TIMEOUT(scans->channels().size(), std::size_t(2), 5000);
    scans->openGate();
    QVERIFY(settle(*session));
    QVERIFY(showsExact(model, "CoolantTemp", 300.0));
    QCOMPARE(scans->channels(), (std::vector<std::uint32_t>{Engine, Coolant, Coolant}));
    QCOMPARE(scans->maxConcurrent(), 1);
    QVERIFY(!scans->timedOut());
}

// A -> B -> A: A's scan was cancelled when B came, so A waits as the pending
// scan and runs again once the cancelled one has returned.
void TestMdf4DocumentSession::reselectingAfterCancelScansAgain() {
    auto scans = std::make_shared<Scans>();
    auto session = openSession(scans);
    SignalPlotModel* model = plotOf(*session);

    session->selectNode(channelKey(*session, Engine));
    QVERIFY(scans->waitForScans(1));
    session->selectNode(channelKey(*session, Vehicle));
    session->selectNode(channelKey(*session, Engine));
    QVERIFY(model->busy());
    QVERIFY(scans->waitForEvent(QStringLiteral("scan 0 returned")));
    QTRY_COMPARE_WITH_TIMEOUT(scans->channels().size(), std::size_t(2), 5000);
    scans->openGate();
    QVERIFY(settle(*session));
    QVERIFY(showsExact(model, "EngineSpeed", 100.0));
    QCOMPARE(scans->channels(), (std::vector<std::uint32_t>{Engine, Engine, Engine}));
    QVERIFY(!scans->timedOut());
}

// Selecting the channel whose overview is being scanned keeps that scan.
void TestMdf4DocumentSession::reselectingTheRunningChannelKeepsItsScan() {
    auto scans = std::make_shared<Scans>();
    auto session = openSession(scans);
    SignalPlotModel* model = plotOf(*session);

    session->selectNode(channelKey(*session, Engine));
    QVERIFY(scans->waitForScans(1));
    session->selectNode(channelKey(*session, Engine));
    QVERIFY(model->busy());
    scans->openGate();
    QVERIFY(settle(*session));
    QVERIFY(showsExact(model, "EngineSpeed", 100.0));
    QCOMPARE(scans->channels(), (std::vector<std::uint32_t>{Engine, Engine}));
}

// A source that finishes the scan although the session cancelled it: the
// result neither reaches the plot nor the cache, so the channel is scanned
// again when it is selected again.
void TestMdf4DocumentSession::cancelledWorkIsNeitherShownNorKept() {
    auto scans = std::make_shared<Scans>();
    scans->honorCancel = false;
    auto session = openSession(scans);
    SignalPlotModel* model = plotOf(*session);

    session->selectNode(channelKey(*session, Engine));
    QVERIFY(scans->waitForScans(1));
    session->selectNode(channelKey(*session, Master));
    scans->release();
    QVERIFY(settle(*session));
    QCOMPARE(model->name(), QStringLiteral("Acquisition time"));
    QVERIFY(!model->hasSamples());
    QVERIFY(!model->busy());
    QCOMPARE(session->resultBytes().results, std::size_t(0));
    QCOMPARE(session->resultBytes().retained, std::uint64_t(0));

    scans->openGate();
    session->selectNode(channelKey(*session, Engine));
    QVERIFY(settle(*session));
    QVERIFY(showsExact(model, "EngineSpeed", 100.0));
    QCOMPARE(scans->channels(), (std::vector<std::uint32_t>{Engine, Engine, Engine}));
}

// A cached channel, a non-channel row, the master and an unsupported channel
// each settle the view without a scan: the pending scan is dropped and the one
// in flight cancelled.
void TestMdf4DocumentSession::settledSelectionDropsPendingAndCancels() {
    enum class Settle { Cached, GroupRow, Master, Unsupported };
    for (Settle kind : {Settle::Cached, Settle::GroupRow, Settle::Master, Settle::Unsupported}) {
        auto scans = std::make_shared<Scans>();
        auto session = openSession(scans);
        SignalPlotModel* model = plotOf(*session);
        std::vector<std::uint32_t> expected;
        if (kind == Settle::Cached) {
            scans->openGate();
            session->selectNode(channelKey(*session, Vehicle));
            QVERIFY(settle(*session));
            scans->closeGate();
            expected = {Vehicle, Vehicle};
        }

        session->selectNode(channelKey(*session, Engine));
        QVERIFY(scans->waitForScans(int(expected.size()) + 1));
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
        QVERIFY(scans->waitForEvent(QStringLiteral("scan 0 returned")));
        QVERIFY(settle(*session));
        expected.push_back(Engine);
        QCOMPARE(scans->channels(), expected);
        QVERIFY(!model->busy());
        if (kind == Settle::Cached) {
            QVERIFY(showsExact(model, "VehicleSpeed", 200.0));
        } else {
            QVERIFY(!model->hasSamples());
        }
        QVERIFY(!scans->timedOut());
    }
}

// Each way a scan can end reads differently: a failure explains itself and is
// scanned again next time, a changed file asks to be reopened, a refusal names
// the limit, a short source says how much of it arrived, and an empty success
// says there are no samples and is kept like any other result.
void TestMdf4DocumentSession::outcomesStayDistinct() {
    auto scans = std::make_shared<Scans>(false);
    scans->outcome = [](const Scans::Request& request) -> std::optional<mdf4::ScanResult> {
        switch (request.channel) {
        case Engine:
            return finished(mdf4::Status::Error, "block cannot be read", "DG[0]/CG[0]/CN[0]");
        case Vehicle:
            return finished(mdf4::Status::SourceChanged, "source file changed since it was opened");
        case Coolant: {
            mdf4::ScanResult refused = finished(mdf4::Status::ResourceLimit,
                                                "fragment exceeds the scratch allowance",
                                                "DG[0]/CG[0]/CN[2]");
            refused.outcome.resource = mdf4::Resource::Scratch;
            refused.outcome.required = std::uint64_t{80} << 20;
            refused.outcome.limit = std::uint64_t{64} << 20;
            return refused;
        }
        default:
            return std::nullopt;
        }
    };
    auto session = openSession(scans);
    SignalPlotModel* model = plotOf(*session);

    session->selectNode(channelKey(*session, Engine));
    QVERIFY(settle(*session));
    QCOMPARE(model->plotState(), SignalPlotModel::Failed);
    QCOMPARE(model->name(), QStringLiteral("EngineSpeed"));
    QCOMPARE(model->message(),
             QStringLiteral("Samples could not be read: block cannot be read (DG[0]/CG[0]/CN[0])"));

    session->selectNode(channelKey(*session, Vehicle));
    QVERIFY(settle(*session));
    QCOMPARE(model->plotState(), SignalPlotModel::Failed);
    QCOMPARE(model->message(), QStringLiteral("Samples could not be read: the file changed since "
                                              "it was opened; reopen it"));

    session->selectNode(channelKey(*session, Coolant));
    QVERIFY(settle(*session));
    QCOMPARE(model->plotState(), SignalPlotModel::Refused);
    QCOMPARE(model->message(),
             QStringLiteral("Samples need more working memory than the decoder allows: 80 MiB "
                            "for one step, 64 MiB allowed (DG[0]/CG[0]/CN[2])"));

    session->selectNode(channelKey(*session, Engine));
    QVERIFY(settle(*session));
    QCOMPARE(model->plotState(), SignalPlotModel::Failed);
    QCOMPARE(scans->channels(), (std::vector<std::uint32_t>{Engine, Vehicle, Coolant, Engine}));
    QCOMPARE(session->resultBytes().results, std::size_t(0));

    auto shortScans = std::make_shared<Scans>(false);
    shortScans->available = 2;
    auto shortSession = openSession(shortScans);
    SignalPlotModel* shortModel = plotOf(*shortSession);
    shortSession->selectNode(channelKey(*shortSession, Engine));
    QVERIFY(settle(*shortSession));
    QVERIFY(shortModel->incomplete());
    QCOMPARE(shortModel->countText(), QStringLiteral("2 of 4 samples"));
    QCOMPARE(shortModel->fullEnd(), 1.0);
    QCOMPARE(shortModel->plotState(), SignalPlotModel::Detail);

    auto emptyScans = std::make_shared<Scans>(false, 0);
    auto emptySession = openSession(emptyScans, makeDocument(0));
    SignalPlotModel* emptyModel = plotOf(*emptySession);
    emptySession->selectNode(channelKey(*emptySession, Engine));
    QVERIFY(settle(*emptySession));
    QCOMPARE(emptyModel->plotState(), SignalPlotModel::Empty);
    QCOMPARE(emptyModel->message(), QStringLiteral("No samples recorded"));
    emptySession->selectNode(channelKey(*emptySession, Vehicle));
    QVERIFY(settle(*emptySession));
    emptySession->selectNode(channelKey(*emptySession, Engine));
    QCOMPARE(emptyModel->message(), QStringLiteral("No samples recorded"));
    QVERIFY(settle(*emptySession));
    QCOMPARE(emptyScans->channels(), (std::vector<std::uint32_t>{Engine, Vehicle}));
}

// Four million samples do not fit one window, so the whole view stays an
// overview; zoomed in, the session scans exactly the cover the plot names and
// installs the window for the view.
void TestMdf4DocumentSession::zoomScansTheWindowTheViewNeeds() {
    auto scans = std::make_shared<Scans>(false, kDense);
    scans->chunk = 65536;
    auto session = openSession(scans, makeDocument(kDense));
    SignalPlotModel* model = plotOf(*session);

    session->selectNode(channelKey(*session, Engine));
    QVERIFY(settle(*session));
    QCOMPARE(model->plotState(), SignalPlotModel::Overview);
    QCOMPARE(scans->channels().size(), std::size_t(1));
    QVERIFY(model->message().startsWith(QStringLiteral("More than 4,194,304 samples in view")));

    model->setVisibleRange(1'000'000.0, 1'001'000.0);
    const std::optional<PlotWindowRequest> wanted = model->detailRequest();
    QVERIFY(wanted);
    QVERIFY(settle(*session));
    const std::vector<Scans::Request> requests = scans->requests();
    QCOMPARE(requests.size(), std::size_t(2));
    QCOMPARE(requests[1].first, wanted->firstSample);
    QCOMPARE(requests[1].count, wanted->sampleCount);
    QCOMPARE(model->plotState(), SignalPlotModel::Detail);
    QCOMPARE(model->viewStart(), 1'000'000.0);
    QCOMPARE(model->viewEnd(), 1'001'000.0);
    QVERIFY(model->window()->covers(1'000'000.0, 1'001'000.0));
    const std::size_t first = static_cast<std::size_t>(1'000'000 - model->window()->firstSample);
    QCOMPARE(model->window()->value[first], 100.0 + 1'000'000.0);

    // Within the window's reach the view needs no other scan.
    model->setVisibleRange(1'000'200.0, 1'000'400.0);
    QCOMPARE(model->plotState(), SignalPlotModel::Detail);
    QVERIFY(settle(*session));
    QCOMPARE(scans->channels().size(), std::size_t(2));
}

// Zooming three times while the first window scans: that scan is cancelled, the
// second view's window is never scanned, and only the last view's window is
// installed.
void TestMdf4DocumentSession::rapidZoomCancelsObsoleteWindows() {
    auto scans = std::make_shared<Scans>(true, kDense);
    scans->chunk = 65536;
    auto session = openSession(scans, makeDocument(kDense));
    SignalPlotModel* model = plotOf(*session);
    session->selectNode(channelKey(*session, Engine));
    scans->release();
    QVERIFY(settle(*session));
    QCOMPARE(model->plotState(), SignalPlotModel::Overview);

    model->setVisibleRange(1'000'000.0, 1'001'000.0);
    QVERIFY(scans->waitForScans(2));
    QVERIFY(model->busy());
    QCOMPARE(model->message(), QStringLiteral("Reading exact samples…"));
    model->setVisibleRange(2'000'000.0, 2'001'000.0);
    model->setVisibleRange(3'000'000.0, 3'001'000.0);
    const std::optional<PlotWindowRequest> last = model->detailRequest();
    QVERIFY(last);
    QVERIFY(model->busy());

    QVERIFY(scans->waitForEvent(QStringLiteral("scan 0 returned")));
    QTRY_COMPARE_WITH_TIMEOUT(scans->channels().size(), std::size_t(3), 5000);
    scans->release();
    QVERIFY(settle(*session));
    const std::vector<Scans::Request> requests = scans->requests();
    QCOMPARE(requests.size(), std::size_t(3));
    QVERIFY(requests[1].first < 1'000'000 && requests[1].first + requests[1].count > 1'001'000);
    QCOMPARE(requests[2].first, last->firstSample);
    QCOMPARE(requests[2].count, last->sampleCount);
    QCOMPARE(model->plotState(), SignalPlotModel::Detail);
    QVERIFY(model->window()->covers(3'000'000.0, 3'001'000.0));
    QVERIFY(!model->busy());
    QCOMPARE(scans->maxConcurrent(), 1);
    QVERIFY(!scans->timedOut());
}

// Progress belongs to the scan in flight for the selection: once that scan is
// cancelled, what it still reports never reaches the plot.
void TestMdf4DocumentSession::staleProgressNeverReachesThePlot() {
    auto scans = std::make_shared<Scans>();
    scans->progressBeforeGate = true;
    scans->progressAfterCancel = true;
    auto session = openSession(scans);
    SignalPlotModel* model = plotOf(*session);
    std::vector<double> shown;
    QObject::connect(model, &SignalPlotModel::busyChanged, model,
                     [&] { shown.push_back(model->progress()); });

    session->selectNode(channelKey(*session, Engine));
    QTRY_COMPARE_WITH_TIMEOUT(model->progress(), 0.25, 5000);
    session->selectNode(channelKey(*session, Vehicle));
    QCOMPARE(model->progress(), -1.0);
    QVERIFY(model->busy());

    // Engine reports three quarters after its cancellation, then returns;
    // Vehicle's scan starts and reports its own quarter.
    QVERIFY(scans->waitForEvent(QStringLiteral("scan 0 returned")));
    QTRY_COMPARE_WITH_TIMEOUT(scans->channels().size(), std::size_t(2), 5000);
    QTRY_COMPARE_WITH_TIMEOUT(model->progress(), 0.25, 5000);
    QVERIFY(std::find(shown.begin(), shown.end(), 0.75) == shown.end());
    scans->openGate();
    QVERIFY(settle(*session));
    QVERIFY(showsExact(model, "VehicleSpeed", 200.0));
    QCOMPARE(model->progress(), -1.0);
}

// A source reporting progress two thousand times a second, for over a second,
// reaches the plot at most once per 100 ms: the worker coalesces it. Without
// that, the progress callouts alone arrive about twice as often.
void TestMdf4DocumentSession::progressIsCoalesced() {
    constexpr std::uint64_t samples = 120 * 512;
    auto scans = std::make_shared<Scans>(false, samples);
    scans->chunk = 512;
    scans->progressBurst = 20;
    scans->chunkTime = 10ms;
    auto session = openSession(scans, makeDocument(samples));
    SignalPlotModel* model = plotOf(*session);
    // The overview scan's progress; the window scan's follows the overview.
    std::vector<double> shown;
    QObject::connect(model, &SignalPlotModel::busyChanged, model, [&] {
        if (!model->hasSamples() && model->progress() >= 0.0) {
            shown.push_back(model->progress());
        }
    });

    QElapsedTimer timer;
    timer.start();
    session->selectNode(channelKey(*session, Engine));
    QTRY_VERIFY_WITH_TIMEOUT(model->hasSamples(), 10000);
    const qint64 elapsed = timer.elapsed();
    const std::size_t updates = shown.size();
    QVERIFY2(updates >= 2 && updates <= std::size_t(elapsed / 100 + 2),
             qPrintable(QStringLiteral("%1 progress updates in %2 ms").arg(updates).arg(elapsed)));
    QVERIFY(std::is_sorted(shown.begin(), shown.end()));
    QVERIFY(settle(*session));
}

// Admission reserves a scan's bytes before it launches; its completion turns
// the reservation into the result's own charge in one step, never both, never
// neither, and the sum stays within the allowance throughout.
void TestMdf4DocumentSession::reservationBecomesTheResultChargeWithoutGap() {
    auto scans = std::make_shared<Scans>();
    auto session = openSession(scans);
    const std::uint64_t allowance = Mdf4SessionLimits{}.resultBytes;
    const auto within = [&] {
        const Mdf4DocumentSession::ResultBytes bytes = session->resultBytes();
        return bytes.retained + bytes.reserved <= allowance;
    };

    session->selectNode(channelKey(*session, Engine));
    QVERIFY(scans->waitForScans(1));
    Mdf4DocumentSession::ResultBytes bytes = session->resultBytes();
    QCOMPARE(bytes.reserved, PlotOverviewBuilder::reservation(4));
    QCOMPARE(bytes.retained, std::uint64_t(0));
    QVERIFY(within());

    scans->release();
    QTRY_VERIFY_WITH_TIMEOUT(plotOf(*session)->hasSamples(), 5000);
    QVERIFY(scans->waitForScans(2));
    const PlotOverviewPtr overview = plotOf(*session)->overview();
    const PlotWindowRequest wanted = overview->windowRequest(0.0, 3.0);
    bytes = session->resultBytes();
    QCOMPARE(bytes.retained, overview->bytes());
    QCOMPARE(bytes.reserved, PlotWindowBuilder::reservation(wanted));
    QCOMPARE(overview->bytes() + sizeof(PlotOverviewBuilder), PlotOverviewBuilder::reservation(4));
    QVERIFY(within());

    scans->release();
    QVERIFY(settle(*session));
    bytes = session->resultBytes();
    QCOMPARE(bytes.reserved, std::uint64_t(0));
    QCOMPARE(bytes.retained, overview->bytes() + plotOf(*session)->window()->bytes());
    QCOMPARE(bytes.results, std::size_t(2));
    QCOMPARE(bytes.held, std::size_t(2));
}

void TestMdf4DocumentSession::obsoleteResultReleasedBeforeTheNextAdmission_data() {
    QTest::addColumn<bool>("window");
    QTest::addColumn<bool>("refused");
    QTest::newRow("overview, next scan pending") << false << false;
    QTest::newRow("overview, next scan asked by an observer") << false << true;
    QTest::newRow("window, next scan pending") << true << false;
    QTest::newRow("window, next scan asked by an observer") << true << true;
}

// A scan that finished while no event loop ran is made obsolete by a new
// selection. Its result, an overview's bins or a window's arrays, goes before
// anything else is admitted or announced, and so does its request's hold on
// the overview of its result set: when the session starts the pending scan
// itself, and when it refuses the pending scan and an observer of that refusal
// at once asks for another channel. The test looks at the heap and the
// allowance from inside the completion's handling, once that next scan has
// started.
void TestMdf4DocumentSession::obsoleteResultReleasedBeforeTheNextAdmission() {
    QFETCH(bool, window);
    QFETCH(bool, refused);
    // Engine's result arrays have sizes no other block here shares. Refused,
    // Vehicle's overview needs more than the whole allowance. Otherwise the
    // allowance holds an overview with its window but never two overviews:
    // after Engine's window, Vehicle's overview of the same size gets in only
    // by evicting Engine's, which nothing may hold any more.
    constexpr std::uint64_t kEngine = 3000;
    constexpr std::uint64_t kWide = 100'000;
    const std::uint64_t vehicle = refused ? kWide : window ? kEngine : 4;
    auto scans = std::make_shared<Scans>(true, kEngine);
    scans->chunk = 1000;
    mdf4::File document = makeDocument();
    document.mutable_groups(0)->mutable_channels(Engine)->set_sample_count(kEngine);
    document.mutable_groups(0)->mutable_channels(Vehicle)->set_sample_count(vehicle);
    const Mdf4SessionLimits limits{Mdf4SessionLimits{}.treeBytes,
                                   refused ? PlotOverviewBuilder::reservation(kWide) - 1
                                           : PlotOverviewBuilder::reservation(kEngine) * 3 / 2};
    auto session = openSession(scans, std::move(document), limits);
    SignalPlotModel* model = plotOf(*session);

    // An overview's builder reserves its bins when it starts; a window's
    // allocates its arrays at its first sample, past the gate.
    if (!window) {
        heap::watch(binBytes(kEngine));
    }
    session->selectNode(channelKey(*session, Engine));
    QVERIFY(scans->waitForScans(1));
    if (window) {
        scans->release();
        QTRY_VERIFY_WITH_TIMEOUT(model->hasSamples(), 5000);
        QVERIFY(scans->waitForScans(2));
        const std::optional<PlotWindowRequest> request = model->detailRequest();
        QVERIFY(request);
        const plotscan::Samples engine = [&scans](std::uint64_t index) {
            return scans->sample(Engine, index);
        };
        heap::watch(plotscan::windowOf(*request, engine)->time.capacity() * sizeof(double));
    }
    scans->release();
    QVERIFY(QThreadPool::globalInstance()->waitForDone(10000));
    QCOMPARE(heap::live(), window ? 2 : 1);

    session->selectNode(channelKey(*session, Vehicle));
    // The next scan: Vehicle's, or Coolant's that the observer asks for.
    const int next = window ? 3 : 2;
    const std::uint64_t reservation = PlotOverviewBuilder::reservation(refused ? 4 : vehicle);
    int live = -1;
    bool started = false;
    Mdf4DocumentSession::ResultBytes bytes;
    const auto look = [&] {
        bytes = session->resultBytes();
        started = bytes.reserved > 0 && scans->waitForScans(next);
        live = heap::live();
    };
    if (refused) {
        const quint64 coolant = channelKey(*session, Coolant);
        callOnce(model, &SignalPlotModel::stateChanged,
                 [model] { return model->plotState() == SignalPlotModel::Refused; },
                 [&session, coolant, look] {
                     session->selectNode(coolant);
                     look();
                 });
    } else {
        // Progress on show makes the session announce its busy state after it
        // started the pending scan.
        model->setProgress(0.5);
        callOnce(model, &SignalPlotModel::busyChanged, [model] { return model->progress() < 0.0; },
                 look);
    }
    QTRY_VERIFY_WITH_TIMEOUT(live >= 0, 5000);
    heap::watch(0);
    QVERIFY2(live == 0 && started && bytes.reserved == reservation,
             qPrintable(QStringLiteral("%1 blocks of the discarded result live; next scan %2; "
                                       "%3 bytes reserved, %4 expected")
                            .arg(live)
                            .arg(started ? QStringLiteral("started") : QStringLiteral("not started"))
                            .arg(bytes.reserved)
                            .arg(reservation)));

    scans->openGate();
    QVERIFY(settle(*session));
    QVERIFY(refused ? showsExact(model, "CoolantTemp", 300.0)
                    : showsExact(model, "VehicleSpeed", 200.0));
    QVERIFY(!scans->timedOut());
}

// Past the allowance the least recently used results nobody else holds go
// first; a window goes before the overview of its result set.
void TestMdf4DocumentSession::unheldResultsEvictedLeastRecentlyUsed() {
    auto probeScans = std::make_shared<Scans>(false);
    auto probe = openSession(probeScans);
    probe->selectNode(channelKey(*probe, Engine));
    QVERIFY(settle(*probe));
    const std::uint64_t channel = probe->resultBytes().retained;

    // Room for two channels and a third's overview, not its window.
    const std::uint64_t allowance = 2 * channel + PlotOverviewBuilder::reservation(4) + 8;
    auto scans = std::make_shared<Scans>(false);
    auto session = openSession(scans, makeDocument(), Mdf4SessionLimits{Mdf4SessionLimits{}.treeBytes, allowance});
    SignalPlotModel* model = plotOf(*session);
    for (int channelIndex : {Engine, Vehicle, Coolant}) {
        session->selectNode(channelKey(*session, channelIndex));
        QVERIFY(settle(*session));
        const Mdf4DocumentSession::ResultBytes bytes = session->resultBytes();
        QVERIFY(bytes.retained + bytes.reserved <= allowance);
    }
    QVERIFY(showsExact(model, "CoolantTemp", 300.0));
    QCOMPARE(session->resultBytes().results, std::size_t(4));
    QCOMPARE(session->resultBytes().retained, 2 * channel);

    session->selectNode(channelKey(*session, Vehicle));
    QVERIFY(showsExact(model, "VehicleSpeed", 200.0));
    session->selectNode(channelKey(*session, Engine));
    QVERIFY(settle(*session));
    QCOMPARE(scans->channels(), (std::vector<std::uint32_t>{Engine, Engine, Vehicle, Vehicle,
                                                            Coolant, Coolant, Engine, Engine}));
}

// Results someone else still holds are never evicted: an old window kept alive
// by another owner, with its overview, plus the shown overview leave no room
// for the replacement window's reservation, which is refused with the numbers.
// Once the old owner lets go, the same request is admitted.
void TestMdf4DocumentSession::heldResultsRefuseAdmission() {
    auto probeScans = std::make_shared<Scans>(false);
    auto probe = openSession(probeScans);
    probe->selectNode(channelKey(*probe, Engine));
    QVERIFY(settle(*probe));
    const std::uint64_t channel = probe->resultBytes().retained;
    const std::uint64_t overviewBytes = probe->resultBytes().retained - plotOf(*probe)->window()->bytes();

    // Engine's results and Vehicle's overview fit; Vehicle's window does not.
    const std::uint64_t allowance = channel + overviewBytes + 64;
    auto scans = std::make_shared<Scans>(false);
    auto session = openSession(scans, makeDocument(), Mdf4SessionLimits{Mdf4SessionLimits{}.treeBytes, allowance});
    SignalPlotModel* model = plotOf(*session);
    session->selectNode(channelKey(*session, Engine));
    QVERIFY(settle(*session));
    PlotWindowPtr old = model->window();

    session->selectNode(channelKey(*session, Vehicle));
    QVERIFY(settle(*session));
    QCOMPARE(model->plotState(), SignalPlotModel::Overview);
    QVERIFY2(model->message().startsWith(QStringLiteral("Exact samples need ")) &&
                 model->message().contains(QStringLiteral("but results in use hold")),
             qPrintable(model->message()));
    Mdf4DocumentSession::ResultBytes bytes = session->resultBytes();
    QCOMPARE(bytes.results, std::size_t(3));
    QCOMPARE(bytes.held, std::size_t(3));
    QCOMPARE(bytes.reserved, std::uint64_t(0));
    QVERIFY(bytes.retained <= allowance);

    old.reset();
    session->selectNode(channelKey(*session, Vehicle));
    QVERIFY(settle(*session));
    QVERIFY(showsExact(model, "VehicleSpeed", 200.0));
    bytes = session->resultBytes();
    QCOMPARE(bytes.results, std::size_t(2));
    QVERIFY(bytes.retained <= allowance);
    QCOMPARE(scans->channels(), (std::vector<std::uint32_t>{Engine, Engine, Vehicle, Vehicle}));
}

// The open worker builds the session and hands it to the controller thread;
// the watcher receiving completions moves with the models, or no completion
// would ever be delivered.
void TestMdf4DocumentSession::sessionFromWorkerThreadDeliversOnOwnerThread() {
    auto scans = std::make_shared<Scans>(false);
    std::unique_ptr<Mdf4DocumentSession> session;
    QThread* owner = QThread::currentThread();
    std::thread worker([&] {
        session = openSession(scans);
        session->moveModelsToThread(owner);
    });
    worker.join();
    SignalPlotModel* model = plotOf(*session);
    QCOMPARE(model->thread(), owner);

    session->selectNode(channelKey(*session, Coolant));
    QTRY_VERIFY_WITH_TIMEOUT(showsExact(model, "CoolantTemp", 300.0), 5000);
}

// Closing with a scan in flight and another pending: the running scan is
// cancelled, the pending one never starts, and destruction returns only after
// the running scan did. The source stays alive until that scan's task has let
// go of it.
void TestMdf4DocumentSession::closeCancelsScanInFlightAndDropsPending() {
    auto scans = std::make_shared<Scans>();
    auto session = openSession(scans);

    session->selectNode(channelKey(*session, Engine));
    QVERIFY(scans->waitForScans(1));
    session->selectNode(channelKey(*session, Coolant));

    session.reset();
    QVERIFY(scans->events().contains(QStringLiteral("scan 0 returned")));
    QVERIFY(scans->waitForEvent(QStringLiteral("source released")));
    QCOMPARE(scans->events(),
             (QStringList{QStringLiteral("scan 0 returned"), QStringLiteral("source released")}));
    QVERIFY(QThreadPool::globalInstance()->waitForDone(10000));
    QCoreApplication::processEvents();
    QCOMPARE(scans->channels(), std::vector<std::uint32_t>{Engine});
    QVERIFY(!scans->timedOut());
}

// A scan that finished while no event loop ran is discarded by destruction;
// its queued completion is never delivered to the closed session.
void TestMdf4DocumentSession::closeDiscardsFinishedUndeliveredScan() {
    auto scans = std::make_shared<Scans>();
    auto session = openSession(scans);

    session->selectNode(channelKey(*session, Engine));
    QVERIFY(scans->waitForScans(1));
    scans->release();
    QVERIFY(QThreadPool::globalInstance()->waitForDone(10000));

    session.reset();
    QVERIFY(scans->waitForEvent(QStringLiteral("source released")));
    QCoreApplication::processEvents();
    QCOMPARE(scans->channels(), std::vector<std::uint32_t>{Engine});
    QCOMPARE(scans->events(),
             (QStringList{QStringLiteral("scan 0 returned"), QStringLiteral("source released")}));
    QVERIFY(!scans->timedOut());
}

// An observer of the installed overview selects another uncached channel from
// inside the notification. The completion already settled its own result, so
// the new scan gets its own, and the plot stays busy for it.
void TestMdf4DocumentSession::selectionFromCompletionNotification() {
    auto scans = std::make_shared<Scans>();
    auto session = openSession(scans);
    SignalPlotModel* model = plotOf(*session);
    Mdf4DocumentSession* raw = session.get();
    const quint64 coolant = channelKey(*session, Coolant);
    callOnce(model, &SignalPlotModel::contentChanged,
             [model] { return model->hasSamples() && model->name() == QStringLiteral("EngineSpeed"); },
             [raw, coolant] { raw->selectNode(coolant); });

    session->selectNode(channelKey(*session, Engine));
    scans->release();
    QTRY_COMPARE_WITH_TIMEOUT(scans->channels().size(), std::size_t(2), 5000);
    QCOMPARE(model->name(), QStringLiteral("CoolantTemp"));
    QVERIFY(model->busy());
    QVERIFY(!model->hasSamples());

    scans->openGate();
    QVERIFY(settle(*session));
    QVERIFY(showsExact(model, "CoolantTemp", 300.0));
    session->selectNode(channelKey(*session, Engine));
    QVERIFY(settle(*session));
    QVERIFY(showsExact(model, "EngineSpeed", 100.0));
    QCOMPARE(scans->channels(), (std::vector<std::uint32_t>{Engine, Coolant, Coolant, Engine}));
    QCOMPARE(scans->maxConcurrent(), 1);
    QVERIFY(!scans->timedOut());
}

// The exact samples settle the plot, and its progress resets. An observer of
// that busy notification selects another uncached channel from inside it: the
// new scan starts and waits at the gate, and the plot stays busy for it.
void TestMdf4DocumentSession::selectionFromBusyNotificationStaysBusy() {
    auto scans = std::make_shared<Scans>();
    scans->progressBeforeGate = true;
    auto session = openSession(scans);
    SignalPlotModel* model = plotOf(*session);
    Mdf4DocumentSession* raw = session.get();
    const quint64 vehicle = channelKey(*session, Vehicle);
    callOnce(model, &SignalPlotModel::busyChanged,
             [model] {
                 return model->plotState() == SignalPlotModel::Detail && model->progress() < 0.0;
             },
             [raw, vehicle] { raw->selectNode(vehicle); });

    session->selectNode(channelKey(*session, Engine));
    QTRY_COMPARE_WITH_TIMEOUT(model->progress(), 0.25, 5000);
    scans->release();
    QTRY_COMPARE_WITH_TIMEOUT(scans->channels().size(), std::size_t(2), 5000);
    QTRY_COMPARE_WITH_TIMEOUT(model->progress(), 0.25, 5000);
    scans->release();
    QTRY_COMPARE_WITH_TIMEOUT(scans->channels().size(), std::size_t(3), 5000);
    QTRY_COMPARE_WITH_TIMEOUT(model->name(), QStringLiteral("VehicleSpeed"), 5000);
    QCOMPARE(model->plotState(), SignalPlotModel::Pending);
    QVERIFY(model->busy());

    scans->openGate();
    QVERIFY(settle(*session));
    QVERIFY(showsExact(model, "VehicleSpeed", 200.0));
    QCOMPARE(scans->channels(), (std::vector<std::uint32_t>{Engine, Engine, Vehicle, Vehicle}));
    QVERIFY(!scans->timedOut());
}

// The completion's final notification is the busy state. Its observer selects
// another channel, which starts a scan, then closes the session: destruction
// from inside the notification cancels and waits for that scan, and nothing
// touches the session afterwards.
void TestMdf4DocumentSession::closeFromCompletionNotification() {
    auto scans = std::make_shared<Scans>();
    auto session = openSession(scans);
    SignalPlotModel* model = plotOf(*session);
    Releaser releaser(scans);
    const quint64 coolant = channelKey(*session, Coolant);
    bool closed = false;
    callOnce(model, &SignalPlotModel::busyChanged,
             [model] { return !model->busy() && model->plotState() == SignalPlotModel::Detail; },
             [&] {
                 session->selectNode(coolant);
                 scans->startTeardown();
                 session.reset();
                 closed = true;
             });

    session->selectNode(channelKey(*session, Engine));
    QVERIFY(scans->waitForScans(1));
    scans->release();
    QTRY_VERIFY_WITH_TIMEOUT(scans->channels().size() == 2, 5000);
    scans->release();
    QTRY_VERIFY_WITH_TIMEOUT(closed, 5000);
    QVERIFY(scans->waitForEvent(QStringLiteral("source released")));
    QVERIFY(QThreadPool::globalInstance()->waitForDone(10000));
    QCoreApplication::processEvents();
    QCOMPARE(scans->channels(), (std::vector<std::uint32_t>{Engine, Engine, Coolant}));
    QCOMPARE(scans->events(),
             (QStringList{QStringLiteral("scan 0 returned"), QStringLiteral("scan 0 returned"),
                          QStringLiteral("scan 2 returned"), QStringLiteral("source released")}));
    QCOMPARE(scans->maxConcurrent(), 1);
    QVERIFY(!scans->timedOut());
}

void TestMdf4DocumentSession::selectionFromSelectionNotification_data() {
    QTest::addColumn<int>("first");
    QTest::addColumn<int>("then");
    QTest::addColumn<int>("hook");
    for (const int hook : {DetailReset, DetailRawForm, PlotContent}) {
        const char* name = hookName(hook);
        QTest::addRow("channel then channel, %s", name) << int(Engine) << int(Coolant) << hook;
        QTest::addRow("channel then group, %s", name) << int(Engine) << kGroupRow << hook;
        QTest::addRow("group then channel, %s", name) << kGroupRow << int(Coolant) << hook;
        QTest::addRow("master then channel, %s", name) << int(Master) << int(Coolant) << hook;
    }
}

// An observer selects another row from inside one of a selection's
// notifications: the detail panel and the plot both end on that newer row, and
// the superseded selection scans nothing.
void TestMdf4DocumentSession::selectionFromSelectionNotification() {
    QFETCH(int, first);
    QFETCH(int, then);
    QFETCH(int, hook);
    auto scans = std::make_shared<Scans>(false);
    auto session = openSession(scans);
    SignalPlotModel* model = plotOf(*session);
    DetailModel* detail = session->detailModel();
    Mdf4DocumentSession* raw = session.get();
    const quint64 newer = rowKey(*session, then);
    callOnSelection(*session, hook, [raw, newer] { raw->selectNode(newer); });

    session->selectNode(rowKey(*session, first));
    QVERIFY(settle(*session));
    if (then == kGroupRow) {
        QCOMPARE(detailName(detail), QStringLiteral("Powertrain"));
        QVERIFY(model->name().isEmpty());
        QVERIFY(!model->busy());
        QVERIFY(scans->channels().empty());
    } else {
        QCOMPARE(detailName(detail), QStringLiteral("CoolantTemp"));
        QVERIFY(showsExact(model, "CoolantTemp", 300.0));
        QCOMPARE(scans->channels(), (std::vector<std::uint32_t>{Coolant, Coolant}));
    }
    QVERIFY(!scans->timedOut());
}

void TestMdf4DocumentSession::closeFromSelectionNotification_data() {
    QTest::addColumn<int>("row");
    QTest::addColumn<int>("hook");
    for (const int hook : {DetailReset, DetailRawForm, PlotContent}) {
        const char* name = hookName(hook);
        QTest::addRow("channel, %s", name) << int(Engine) << hook;
        QTest::addRow("group, %s", name) << kGroupRow << hook;
    }
}

// An observer closes the session from inside one of a selection's
// notifications: the selection stops there, scans nothing and touches neither
// the destroyed session nor its models.
void TestMdf4DocumentSession::closeFromSelectionNotification() {
    QFETCH(int, row);
    QFETCH(int, hook);
    auto scans = std::make_shared<Scans>(false);
    auto session = openSession(scans);
    bool closed = false;
    callOnSelection(*session, hook, [&] {
        session.reset();
        closed = true;
    });

    session->selectNode(rowKey(*session, row));
    QVERIFY(closed);
    QVERIFY(QThreadPool::globalInstance()->waitForDone(10000));
    QCoreApplication::processEvents();
    QVERIFY(scans->channels().empty());
    QCOMPARE(scans->events(), QStringList{QStringLiteral("source released")});
}

// A tab closed while its channel scans: the close notifies while the session
// is alive; then the session cancels and waits for the scan and is destroyed,
// and the finished scan reaches nobody.
void TestMdf4DocumentSession::closingTabWithScanInFlight() {
    auto scans = std::make_shared<Scans>();
    FormatList formats;
    formats.push_back({FormatId::MDF4, {QStringLiteral("mf4")}, std::make_unique<SessionAdapter>(scans)});
    AppController controller(std::move(formats));
    QSignalSpy loaded(&controller, &AppController::fileLoaded);
    controller.openFile(QUrl::fromLocalFile(QDir::temp().filePath(QStringLiteral("test.mf4"))));
    QVERIFY(loaded.wait(10000));
    auto* session = static_cast<Mdf4DocumentSession*>(controller.tabModel()->tabAt(0)->session());
    QObject::connect(plotOf(*session), &QObject::destroyed, &controller,
                     [scans] { scans->note(QStringLiteral("plot destroyed")); });
    QObject::connect(&controller, &AppController::currentSessionChanged, &controller,
                     [scans] { scans->note(QStringLiteral("tab closed")); });

    controller.selectCurrentNode(channelKey(*session, Engine));
    QVERIFY(scans->waitForScans(1));
    controller.closeTab(0);

    const QStringList events = scans->events();
    const qsizetype closed = events.indexOf(QStringLiteral("tab closed"));
    const qsizetype returned = events.indexOf(QStringLiteral("scan %1 returned").arg(Engine));
    const qsizetype destroyed = events.indexOf(QStringLiteral("plot destroyed"));
    QVERIFY(closed >= 0 && returned >= 0);
    QVERIFY(closed < destroyed);
    QVERIFY(returned < destroyed);
    QVERIFY(QThreadPool::globalInstance()->waitForDone(10000));
    QCoreApplication::processEvents();
    QCOMPARE(controller.tabModel()->rowCount(), 0);
    QCOMPARE(scans->channels(), std::vector<std::uint32_t>{Engine});
    QVERIFY(!scans->timedOut());
}

// The estimate is the admission: a tree needing exactly the allowance is built,
// one byte less refuses it.
void TestMdf4DocumentSession::treeAdmittedUpToItsAllowance() {
    auto scans = std::make_shared<Scans>(false);
    const std::uint64_t required = Mdf4DocumentSession::treeBytes(makeDocument());
    auto admitted = openSession(scans, makeDocument(), Mdf4SessionLimits{required});
    TreeModel* tree = admitted->treeModel();
    QCOMPARE(tree->rowCount(tree->index(0, 0)), 2);
    QCOMPARE(tree->rowCount(tree->index(0, 0, tree->index(0, 0))), 5);
    QVERIFY(!admitted->hasDiagnostics());

    auto refused = openSession(scans, makeDocument(), Mdf4SessionLimits{required - 1});
    QCOMPARE(refused->treeModel()->rowCount(refused->treeModel()->index(0, 0)), 0);
    QVERIFY(refused->hasDiagnostics());
}

// Past the allowance the session still opens: the file row with its details and
// one error saying why no group or channel is listed. No row stands for part of
// the tree.
void TestMdf4DocumentSession::treeOverAllowanceShowsOnlyTheFileRow() {
    auto scans = std::make_shared<Scans>(false);
    auto session = openSession(scans, makeDocument(), Mdf4SessionLimits{1024});
    TreeModel* tree = session->treeModel();
    QCOMPARE(tree->rowCount(), 1);
    const QModelIndex file = tree->index(0, 0);
    QCOMPARE(tree->rowCount(file), 0);
    QCOMPARE(tree->data(file, TreeModel::TitleRole).toString(), QStringLiteral("test.mf4"));
    QCOMPARE(tree->data(file, TreeModel::SubtitleRole).toString(),
             QStringLiteral("MDF 4.20  ·  channel tree not shown"));

    const QList<DiagnosticMessage> diagnostics = session->diagnostics();
    QCOMPARE(diagnostics.size(), 1);
    QCOMPARE(diagnostics.front().severity, DiagnosticSeverity::Error);
    QCOMPARE(diagnostics.front().title, QStringLiteral("Channel tree not shown"));
    QCOMPARE(diagnostics.front().detail,
             QStringLiteral("Listing 6 channels in 2 channel groups needs about 4 KiB; the "
                            "navigation tree's allowance is 1 KiB."));

    session->selectNode(tree->data(file, TreeModel::NodeKeyRole).toULongLong());
    QVERIFY(session->detailModel()->rowCount() > 0);
    QVERIFY(!plotOf(*session)->busy());
    QVERIFY(settle(*session));
    QVERIFY(scans->channels().empty());
}

QTEST_MAIN(TestMdf4DocumentSession)
#include "tst_mdf4documentsession.moc"
