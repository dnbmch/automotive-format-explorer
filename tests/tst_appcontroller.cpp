#include "core/appcontroller.h"
#include "sessions/adaptersessionbase.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTest>
#include <QThread>
#include <QThreadPool>

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

namespace {

using namespace std::chrono_literals;

// Shared by a test and the fake adapter/session it composes into the
// controller; outlives the controller. Waits are bounded only as deadlock
// guards: every one normally ends on a state change.
class Probe {
    // Defined first: the accessors below deduce their types from read().
    bool waitUntil(const bool& flag) {
        std::unique_lock<std::mutex> lock(_mutex);
        return _changed.wait_for(lock, 10s, [&flag] { return flag; });
    }
    template <typename F>
    void update(F change) {
        std::lock_guard<std::mutex> lock(_mutex);
        change();
        _changed.notify_all();
    }
    template <typename F>
    auto read(F value) {
        std::lock_guard<std::mutex> lock(_mutex);
        return value();
    }

public:
    bool gateOpen = true;    // load() blocks until the gate opens
    bool failLoad = false;

    void openGate() { update([this] { gateOpen = true; }); }
    void startTeardown() { update([this] { _teardown = true; }); }
    bool waitUntilLoading() { return waitUntil(_loadActive); }
    bool waitUntilTeardown() { return waitUntil(_teardown); }
    void noteTimeout() { update([this] { _timedOut = true; }); }
    bool timedOut() { return read([this] { return _timedOut; }); }
    int loads() { return read([this] { return _loads; }); }
    QStringList events() { return read([this] { return _events; }); }
    QThread* sessionDestroyedOn() { return read([this] { return _sessionThread; }); }
    QThread* modelsOnAtDestruction() { return read([this] { return _modelThread; }); }

    // Fake adapter side.
    bool enterLoad() {
        std::unique_lock<std::mutex> lock(_mutex);
        _loads++;
        _loadActive = true;
        _changed.notify_all();
        if (!_changed.wait_for(lock, 10s, [this] { return gateOpen; })) {
            _timedOut = true;
        }
        return failLoad;
    }
    void leaveLoad() {
        update([this] {
            _loadActive = false;
            _events.push_back(QStringLiteral("load returned"));
        });
    }
    void sessionDestroyed(QThread* models) {
        update([this, models] {
            _sessionThread = QThread::currentThread();
            _modelThread = models;
            _events.push_back(QStringLiteral("session destroyed"));
        });
    }
    void adapterDestroyed() {
        update([this] {
            _events.push_back(_loadActive ? QStringLiteral("adapter destroyed during load")
                                          : QStringLiteral("adapter destroyed"));
        });
    }

private:
    std::mutex _mutex;
    std::condition_variable _changed;
    bool _teardown = false;
    bool _timedOut = false;
    bool _loadActive = false;
    int _loads = 0;
    QStringList _events;
    QThread* _sessionThread = nullptr;
    QThread* _modelThread = nullptr;
};

class FakeSession final : public AdapterSessionBase {
public:
    FakeSession(Probe& probe, const QString& path)
        : AdapterSessionBase(FormatId::Unknown, QStringLiteral("Fake"),
                             QFileInfo(path).fileName(), path),
          _probe(probe) {}
    ~FakeSession() override { _probe.sessionDestroyed(_tree_model.thread()); }

private:
    Probe& _probe;
};

class FakeAdapter final : public FormatAdapter {
public:
    explicit FakeAdapter(Probe& probe) : _probe(probe) {}
    ~FakeAdapter() override { _probe.adapterDestroyed(); }

    LoadResult load(const QString& path) const override {
        LoadResult result;
        if (_probe.enterLoad()) {
            result.diagnostics.push_back(
                {DiagnosticSeverity::Error, QStringLiteral("Failed"), QStringLiteral("fake failure")});
        } else {
            result.session = std::make_unique<FakeSession>(_probe, path);
        }
        _probe.leaveLoad();
        return result;
    }

private:
    Probe& _probe;
};

FormatList fakeFormats(Probe& probe) {
    FormatList formats;
    formats.push_back({FormatId::Unknown, {QStringLiteral("fake")}, std::make_unique<FakeAdapter>(probe)});
    return formats;
}

QUrl fakeFile(const QString& name = QStringLiteral("doc.fake")) {
    return QUrl::fromLocalFile(QDir::temp().filePath(name));
}

// Opens the gate from another thread once teardown has been requested, so the
// load is still blocked when destruction starts on the test thread. The worker
// may return just before shutdown begins waiting or during that wait; teardown
// must give the same observable result either way. Destruction releases and
// joins the thread on every path, including a failed check; declare it after
// the controller so it is destroyed first.
class GateReleaser {
public:
    explicit GateReleaser(Probe& probe)
        : _probe(probe),
          _thread([this] {
              if (!_probe.waitUntilTeardown()) {
                  _probe.noteTimeout();
              }
              _probe.openGate();
          }) {}
    ~GateReleaser() {
        _probe.startTeardown();
        _probe.openGate();
        _thread.join();
    }

private:
    Probe& _probe;
    std::thread _thread;
};

// Requests teardown, which lets a GateReleaser open the gate, then destroys the
// controller on this thread.
void destroy(std::unique_ptr<AppController>& controller, Probe& probe) {
    probe.startTeardown();
    controller.reset();
}

// Runs action once, from inside the first emission of signal for which when()
// holds: an ordinary same-thread observer calling back into the controller.
template <typename Sender, typename Signal, typename When, typename Action>
void callOnce(Sender* sender, Signal signal, When when, Action action) {
    auto done = std::make_shared<bool>(false);
    QObject::connect(sender, signal, sender, [done, when, action]() {
        if (*done || !when()) {
            return;
        }
        *done = true;
        action();
    });
}

} // namespace

class TestAppController : public QObject {
    Q_OBJECT

private slots:
    void deliversSuccessfulLoad();
    void reportsFailedLoad();
    void rejectsUnsupportedAndConcurrentOpens();
    void destructionWithoutLoad();
    void destructionWaitsForRunningLoad();
    void destructionDisposesUndeliveredResult();
    void destructionAfterFailedLoad();
    void shutdownStopsDeliveryAndOpens();
    void shutdownFromBusyNotification();
    void shutdownFromIdleNotification();
    void openFromIdleNotificationDeliversBoth();
    void shutdownFromClearedErrorRefusesOpen();
    void shutdownDuringDeliveryStopsFileLoaded();
    void shutdownFromCurrentTabIndexStopsDelivery();
};

void TestAppController::deliversSuccessfulLoad() {
    Probe probe;
    AppController controller(fakeFormats(probe));
    QSignalSpy index(&controller, &AppController::currentTabIndexChanged);
    QSignalSpy current(&controller, &AppController::currentSessionChanged);
    QSignalSpy loaded(&controller, &AppController::fileLoaded);

    controller.openFile(fakeFile());
    QVERIFY(controller.fileLoading());
    QVERIFY(loaded.wait(10000));

    QCOMPARE(index.count(), 1);
    QCOMPARE(current.count(), 1);
    QCOMPARE(loaded.count(), 1);
    QCOMPARE(loaded.first().first().toString(), QStringLiteral("doc.fake"));
    QVERIFY(!controller.fileLoading());
    QVERIFY(controller.lastError().isEmpty());
    QCOMPARE(controller.tabModel()->rowCount(), 1);
    QCOMPARE(controller.currentTabIndex(), 0);
    DocumentSession* session = controller.tabModel()->sessionAt(0);
    QCOMPARE(session->formatName(), QStringLiteral("Fake"));
    QCOMPARE(session->treeModel()->thread(), controller.thread());
    QCOMPARE(session->detailModel()->thread(), controller.thread());
}

void TestAppController::reportsFailedLoad() {
    Probe probe;
    probe.failLoad = true;
    AppController controller(fakeFormats(probe));
    QSignalSpy errors(&controller, &AppController::lastErrorChanged);
    QSignalSpy loaded(&controller, &AppController::fileLoaded);

    controller.openFile(fakeFile());
    QVERIFY(errors.wait(10000));

    QCOMPARE(controller.lastError(), QStringLiteral("fake failure"));
    QVERIFY(!controller.fileLoading());
    QCOMPARE(controller.tabModel()->rowCount(), 0);
    QCOMPARE(loaded.count(), 0);
}

void TestAppController::rejectsUnsupportedAndConcurrentOpens() {
    Probe probe;
    probe.gateOpen = false;
    auto controller = std::make_unique<AppController>(fakeFormats(probe));
    GateReleaser releaser(probe);
    QSignalSpy loaded(controller.get(), &AppController::fileLoaded);

    controller->openFile(QUrl::fromLocalFile(QDir::temp().filePath(QStringLiteral("notes.txt"))));
    QCOMPARE(controller->lastError(), QStringLiteral("Unsupported file type: notes.txt"));
    QVERIFY(!controller->fileLoading());

    controller->openFile(fakeFile(QStringLiteral("first.fake")));
    QVERIFY(controller->fileLoading());
    controller->openFile(fakeFile(QStringLiteral("second.fake")));
    QCOMPARE(controller->lastError(), QStringLiteral("Another file is already loading."));

    probe.openGate();
    QVERIFY(loaded.wait(10000));
    QCOMPARE(loaded.first().first().toString(), QStringLiteral("first.fake"));
    QCOMPARE(probe.loads(), 1);
    QVERIFY(!probe.timedOut());
}

void TestAppController::destructionWithoutLoad() {
    Probe probe;
    auto controller = std::make_unique<AppController>(fakeFormats(probe));
    destroy(controller, probe);

    QCOMPARE(probe.events(), QStringList({QStringLiteral("adapter destroyed")}));
    QCOMPARE(probe.loads(), 0);
}

// The worker is blocked inside load() when teardown is requested and is
// released only afterwards, from another thread: teardown must observe its
// return before the adapter goes, and destroy the undelivered session here.
void TestAppController::destructionWaitsForRunningLoad() {
    Probe probe;
    probe.gateOpen = false;
    auto controller = std::make_unique<AppController>(fakeFormats(probe));
    GateReleaser releaser(probe);
    QSignalSpy loaded(controller.get(), &AppController::fileLoaded);
    QSignalSpy inserted(controller->tabModel(), &QAbstractItemModel::rowsInserted);

    controller->openFile(fakeFile());
    QVERIFY(probe.waitUntilLoading());
    destroy(controller, probe);

    QCOMPARE(probe.events(), QStringList({QStringLiteral("load returned"),
                                          QStringLiteral("session destroyed"),
                                          QStringLiteral("adapter destroyed")}));
    QCOMPARE(probe.sessionDestroyedOn(), QThread::currentThread());
    QCOMPARE(probe.modelsOnAtDestruction(), QThread::currentThread());
    QCoreApplication::processEvents();
    QCOMPARE(loaded.count(), 0);
    QCOMPARE(inserted.count(), 0);
    QVERIFY(!probe.timedOut());
}

// The worker has published its result, but the queued completion has not run
// because this thread's event loop has not: teardown consumes it here.
void TestAppController::destructionDisposesUndeliveredResult() {
    Probe probe;
    auto controller = std::make_unique<AppController>(fakeFormats(probe));
    QSignalSpy loaded(controller.get(), &AppController::fileLoaded);
    QSignalSpy inserted(controller->tabModel(), &QAbstractItemModel::rowsInserted);

    controller->openFile(fakeFile());
    QVERIFY(QThreadPool::globalInstance()->waitForDone(10000));
    QVERIFY(controller->fileLoading());
    QCOMPARE(probe.events(), QStringList({QStringLiteral("load returned")}));
    destroy(controller, probe);

    QCOMPARE(probe.events(), QStringList({QStringLiteral("load returned"),
                                          QStringLiteral("session destroyed"),
                                          QStringLiteral("adapter destroyed")}));
    QCOMPARE(probe.sessionDestroyedOn(), QThread::currentThread());
    QCOMPARE(probe.modelsOnAtDestruction(), QThread::currentThread());
    QCoreApplication::processEvents();
    QCOMPARE(loaded.count(), 0);
    QCOMPARE(inserted.count(), 0);
}

void TestAppController::destructionAfterFailedLoad() {
    Probe probe;
    probe.gateOpen = false;
    probe.failLoad = true;
    auto controller = std::make_unique<AppController>(fakeFormats(probe));
    GateReleaser releaser(probe);
    QSignalSpy errors(controller.get(), &AppController::lastErrorChanged);

    controller->openFile(fakeFile());
    QVERIFY(probe.waitUntilLoading());
    destroy(controller, probe);

    QCOMPARE(probe.events(), QStringList({QStringLiteral("load returned"),
                                          QStringLiteral("adapter destroyed")}));
    QCOMPARE(errors.count(), 0);
    QVERIFY(!probe.timedOut());
}

// An explicit shutdown with the controller alive: the finished result is
// disposed, running the event loop delivers nothing, and later opens are
// refused. Repeated shutdown, and the destructor's, are harmless.
void TestAppController::shutdownStopsDeliveryAndOpens() {
    Probe probe;
    AppController controller(fakeFormats(probe));
    QSignalSpy loaded(&controller, &AppController::fileLoaded);
    QSignalSpy inserted(controller.tabModel(), &QAbstractItemModel::rowsInserted);
    QSignalSpy errors(&controller, &AppController::lastErrorChanged);
    QSignalSpy loading(&controller, &AppController::fileLoadingChanged);

    controller.openFile(fakeFile());
    QVERIFY(QThreadPool::globalInstance()->waitForDone(10000));
    controller.shutdown();

    QCOMPARE(probe.events(), QStringList({QStringLiteral("load returned"),
                                          QStringLiteral("session destroyed")}));
    QCOMPARE(probe.sessionDestroyedOn(), QThread::currentThread());
    QVERIFY(!controller.fileLoading());
    QCoreApplication::processEvents();
    QCOMPARE(loaded.count(), 0);
    QCOMPARE(inserted.count(), 0);
    QCOMPARE(controller.tabModel()->rowCount(), 0);

    controller.openFile(fakeFile(QStringLiteral("later.fake")));
    controller.shutdown();
    QCoreApplication::processEvents();
    QCOMPARE(probe.loads(), 1);
    QCOMPARE(errors.count(), 0);
    QCOMPARE(loading.count(), 1);   // the first open only
    QCOMPARE(probe.events().size(), 2);
}

// Observers run synchronously inside the controller's notifications. Shutting
// down from the busy notification must consume the load just announced.
void TestAppController::shutdownFromBusyNotification() {
    Probe probe;
    AppController controller(fakeFormats(probe));
    QSignalSpy loaded(&controller, &AppController::fileLoaded);
    QSignalSpy inserted(controller.tabModel(), &QAbstractItemModel::rowsInserted);
    callOnce(&controller, &AppController::fileLoadingChanged,
             [&controller] { return controller.fileLoading(); },
             [&controller] { controller.shutdown(); });

    controller.openFile(fakeFile());

    QCOMPARE(probe.events(), QStringList({QStringLiteral("load returned"),
                                          QStringLiteral("session destroyed")}));
    QCOMPARE(probe.sessionDestroyedOn(), QThread::currentThread());
    QCOMPARE(probe.modelsOnAtDestruction(), QThread::currentThread());
    QVERIFY(!controller.fileLoading());
    QCoreApplication::processEvents();
    QCOMPARE(loaded.count(), 0);
    QCOMPARE(inserted.count(), 0);
}

// Shutting down from the idle notification leaves the just-finished result
// undelivered: no tab, no fileLoaded, destroyed on this thread.
void TestAppController::shutdownFromIdleNotification() {
    Probe probe;
    AppController controller(fakeFormats(probe));
    QSignalSpy loading(&controller, &AppController::fileLoadingChanged);
    QSignalSpy loaded(&controller, &AppController::fileLoaded);
    QSignalSpy inserted(controller.tabModel(), &QAbstractItemModel::rowsInserted);
    callOnce(&controller, &AppController::fileLoadingChanged,
             [&controller] { return !controller.fileLoading(); },
             [&controller] { controller.shutdown(); });

    controller.openFile(fakeFile());
    QVERIFY(loading.wait(10000));   // the idle notification

    QCoreApplication::processEvents();
    QCOMPARE(loaded.count(), 0);
    QCOMPARE(inserted.count(), 0);
    QCOMPARE(controller.tabModel()->rowCount(), 0);
    QCOMPARE(probe.events(), QStringList({QStringLiteral("load returned"),
                                          QStringLiteral("session destroyed")}));
    QCOMPARE(probe.sessionDestroyedOn(), QThread::currentThread());
    QCOMPARE(probe.modelsOnAtDestruction(), QThread::currentThread());
}

// The idle notification is a legitimate point to open the next file: each
// result is delivered once, as its own tab, in order.
void TestAppController::openFromIdleNotificationDeliversBoth() {
    Probe probe;
    AppController controller(fakeFormats(probe));
    QSignalSpy loaded(&controller, &AppController::fileLoaded);
    callOnce(&controller, &AppController::fileLoadingChanged,
             [&controller] { return !controller.fileLoading(); },
             [&controller] { controller.openFile(fakeFile(QStringLiteral("second.fake"))); });

    controller.openFile(fakeFile(QStringLiteral("first.fake")));
    while (loaded.count() < 2) {
        QVERIFY(loaded.wait(10000));
    }
    QCoreApplication::processEvents();

    QCOMPARE(loaded.count(), 2);
    QCOMPARE(loaded.at(0).first().toString(), QStringLiteral("first.fake"));
    QCOMPARE(loaded.at(1).first().toString(), QStringLiteral("second.fake"));
    QCOMPARE(probe.loads(), 2);
    TabModel* tabs = controller.tabModel();
    QCOMPARE(tabs->rowCount(), 2);
    for (int row = 0; row < 2; row++) {
        DocumentSession* session = tabs->sessionAt(row);
        QCOMPARE(session->displayName(),
                 row == 0 ? QStringLiteral("first.fake") : QStringLiteral("second.fake"));
        QCOMPARE(session->treeModel()->thread(), controller.thread());
    }
    QVERIFY(!controller.fileLoading());
    QVERIFY(controller.lastError().isEmpty());
}

// Clearing the previous error is a notification too: an observer that shuts
// down there must stop the open that cleared it.
void TestAppController::shutdownFromClearedErrorRefusesOpen() {
    Probe probe;
    AppController controller(fakeFormats(probe));
    controller.openFile(QUrl::fromLocalFile(QDir::temp().filePath(QStringLiteral("notes.txt"))));
    QVERIFY(!controller.lastError().isEmpty());
    QSignalSpy loading(&controller, &AppController::fileLoadingChanged);
    callOnce(&controller, &AppController::lastErrorChanged,
             [&controller] { return controller.lastError().isEmpty(); },
             [&controller] { controller.shutdown(); });

    controller.openFile(fakeFile());
    QVERIFY(QThreadPool::globalInstance()->waitForDone(10000));   // a started load would finish here

    QCOMPARE(probe.loads(), 0);
    QVERIFY(!controller.fileLoading());
    QCOMPARE(loading.count(), 0);
}

// Delivery itself notifies observers several times. A shutdown from the tab
// insertion ends it: no current-tab change and no fileLoaded follow.
void TestAppController::shutdownDuringDeliveryStopsFileLoaded() {
    Probe probe;
    AppController controller(fakeFormats(probe));
    QSignalSpy loaded(&controller, &AppController::fileLoaded);
    QSignalSpy current(&controller, &AppController::currentSessionChanged);
    callOnce(controller.tabModel(), &QAbstractItemModel::rowsInserted,
             [] { return true; }, [&controller] { controller.shutdown(); });

    controller.openFile(fakeFile());
    QVERIFY(QThreadPool::globalInstance()->waitForDone(10000));
    QCoreApplication::processEvents();   // delivers the completion

    QCOMPARE(controller.tabModel()->rowCount(), 1);
    QCOMPARE(current.count(), 0);
    QCOMPARE(loaded.count(), 0);
    QCOMPARE(controller.currentTabIndex(), -1);
}

// The current-tab change notifies twice. A shutdown from the index
// notification ends delivery between them: no current-session change and no
// fileLoaded follow.
void TestAppController::shutdownFromCurrentTabIndexStopsDelivery() {
    Probe probe;
    AppController controller(fakeFormats(probe));
    QSignalSpy index(&controller, &AppController::currentTabIndexChanged);
    QSignalSpy current(&controller, &AppController::currentSessionChanged);
    QSignalSpy loaded(&controller, &AppController::fileLoaded);
    callOnce(&controller, &AppController::currentTabIndexChanged,
             [] { return true; }, [&controller] { controller.shutdown(); });

    controller.openFile(fakeFile());
    QVERIFY(QThreadPool::globalInstance()->waitForDone(10000));
    QCoreApplication::processEvents();   // delivers the completion

    QCOMPARE(controller.currentTabIndex(), 0);
    QCOMPARE(index.count(), 1);
    QCOMPARE(current.count(), 0);
    QCOMPARE(loaded.count(), 0);
}

QTEST_GUILESS_MAIN(TestAppController)
#include "tst_appcontroller.moc"
