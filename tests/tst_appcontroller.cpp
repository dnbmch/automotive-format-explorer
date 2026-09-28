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
    void note(const QString& event) { update([this, event] { _events.push_back(event); }); }
    QStringList destroyed() { return read([this] { return _destroyed; }); }
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
    void sessionDestroyed(QThread* models, const QString& name) {
        update([this, models, name] {
            _sessionThread = QThread::currentThread();
            _modelThread = models;
            _events.push_back(QStringLiteral("session destroyed"));
            _destroyed.push_back(name);
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
    QStringList _destroyed;
    QThread* _sessionThread = nullptr;
    QThread* _modelThread = nullptr;
};

class FakeSession final : public AdapterSessionBase {
public:
    FakeSession(Probe& probe, const QString& path)
        : AdapterSessionBase(FormatId::Unknown, QStringLiteral("Fake"),
                             QFileInfo(path).fileName(), path),
          _probe(probe) {}
    ~FakeSession() override { _probe.sessionDestroyed(_tree_model.thread(), displayName()); }

    void selectNode(quint64 key) override {
        _probe.note(QStringLiteral("select %1 %2").arg(displayName()).arg(key));
    }

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

const QString A = QStringLiteral("a.fake");
const QString B = QStringLiteral("b.fake");
const QString C = QStringLiteral("c.fake");

// Opens one fake file per name, each delivered as its own tab before the next.
bool openTabs(AppController& controller, const QStringList& names) {
    QSignalSpy loaded(&controller, &AppController::fileLoaded);
    for (const QString& name : names) {
        const qsizetype before = loaded.count();
        controller.openFile(fakeFile(name));
        while (loaded.count() == before) {
            if (!loaded.wait(10000)) {
                return false;
            }
        }
    }
    return true;
}

QStringList titles(AppController& controller) {
    TabModel* tabs = controller.tabModel();
    QStringList result;
    for (int row = 0; row < tabs->rowCount(); ++row) {
        result << tabs->data(tabs->index(row), TabModel::TitleRole).toString();
    }
    return result;
}

QString currentTitle(AppController& controller) {
    TabModel* tabs = controller.tabModel();
    return tabs->data(tabs->index(controller.currentTabIndex()), TabModel::TitleRole).toString();
}

// Records the current-tab notifications in the probe's log, in order with
// session destruction.
void noteNotifications(AppController& controller, Probe& probe) {
    QObject::connect(&controller, &AppController::currentTabIndexChanged, &controller,
                     [&probe] { probe.note(QStringLiteral("index")); });
    QObject::connect(&controller, &AppController::currentSessionChanged, &controller,
                     [&probe] { probe.note(QStringLiteral("session")); });
}

QStringList sorted(QStringList list) {
    list.sort();
    return list;
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
    void closeCurrentTabWithSuccessor();
    void closeTabBeforeCurrent();
    void closeTabAfterCurrent();
    void closeOnlyTab();
    void closedSessionOutlivesNotifications();
    void shutdownFromRowRemovalDuringClose();
    void shutdownFromIndexNotificationDuringClose();
    void shutdownFromSessionNotificationDuringClose();
    void closeAfterShutdown();
    void switchAfterShutdown();
    void everyPublicActionInertAfterShutdown();
    void closeCurrentFromIndexNotification();
    void closeCurrentFromSessionNotification();
    void selectionStaysInCurrentTab();
    void requestsDuringRowRemovalAreDeferred();
    void requestsAfterRowRemovalKeepTheirTabs();
    void deferredRequestsKeepTabIdentity();
    void closeEarlierTabDuringInsertion();
    void closeInsertedTabDuringInsertion();
    void deferredRequestsAfterShutdownAreInert();
    void shutdownDuringRowRemovalFinishesIt();
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
    DocumentSession* session = controller.tabModel()->tabAt(0)->session();
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
        DocumentSession* session = tabs->tabAt(row)->session();
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

// Closing the current tab hands over to its successor at the same row: the row
// value did not change, but the tab behind it did, and both are announced.
void TestAppController::closeCurrentTabWithSuccessor() {
    Probe probe;
    AppController controller(fakeFormats(probe));
    QVERIFY(openTabs(controller, {A, B, C}));
    controller.setCurrentTabIndex(1);
    QSignalSpy index(&controller, &AppController::currentTabIndexChanged);
    QSignalSpy current(&controller, &AppController::currentSessionChanged);

    controller.closeTab(1);

    QCOMPARE(titles(controller), QStringList({A, C}));
    QCOMPARE(controller.currentTabIndex(), 1);
    QCOMPARE(currentTitle(controller), C);
    QCOMPARE(index.count(), 1);
    QCOMPARE(current.count(), 1);
}

// Closing a tab before the current one moves the current tab up a row. Its
// session did not change, so observers are not told it did.
void TestAppController::closeTabBeforeCurrent() {
    Probe probe;
    AppController controller(fakeFormats(probe));
    QVERIFY(openTabs(controller, {A, B, C}));
    QSignalSpy index(&controller, &AppController::currentTabIndexChanged);
    QSignalSpy current(&controller, &AppController::currentSessionChanged);

    controller.closeTab(0);

    QCOMPARE(titles(controller), QStringList({B, C}));
    QCOMPARE(controller.currentTabIndex(), 1);
    QCOMPARE(currentTitle(controller), C);
    QCOMPARE(index.count(), 1);
    QCOMPARE(current.count(), 0);
}

void TestAppController::closeTabAfterCurrent() {
    Probe probe;
    AppController controller(fakeFormats(probe));
    QVERIFY(openTabs(controller, {A, B, C}));
    controller.setCurrentTabIndex(0);
    QSignalSpy index(&controller, &AppController::currentTabIndexChanged);
    QSignalSpy current(&controller, &AppController::currentSessionChanged);

    controller.closeTab(2);

    QCOMPARE(titles(controller), QStringList({A, B}));
    QCOMPARE(currentTitle(controller), A);
    QCOMPARE(index.count(), 0);
    QCOMPARE(current.count(), 0);
}

void TestAppController::closeOnlyTab() {
    Probe probe;
    AppController controller(fakeFormats(probe));
    QVERIFY(openTabs(controller, {A}));
    QSignalSpy index(&controller, &AppController::currentTabIndexChanged);
    QSignalSpy current(&controller, &AppController::currentSessionChanged);

    controller.closeTab(0);

    QCOMPARE(controller.tabModel()->rowCount(), 0);
    QCOMPARE(controller.currentTabIndex(), -1);
    QCOMPARE(index.count(), 1);
    QCOMPARE(current.count(), 1);
    QCOMPARE(probe.destroyed(), QStringList({A}));
}

// The closed session outlives both notifications, so no view is told about a
// destroyed model; it is destroyed on the controller's thread.
void TestAppController::closedSessionOutlivesNotifications() {
    Probe probe;
    AppController controller(fakeFormats(probe));
    QVERIFY(openTabs(controller, {A, B, C}));
    controller.setCurrentTabIndex(1);
    noteNotifications(controller, probe);
    const qsizetype from = probe.events().size();

    controller.closeTab(1);

    QCOMPARE(probe.events().mid(from), QStringList({QStringLiteral("index"),
                                                    QStringLiteral("session"),
                                                    QStringLiteral("session destroyed")}));
    QCOMPARE(probe.destroyed(), QStringList({B}));
    QCOMPARE(probe.sessionDestroyedOn(), QThread::currentThread());
}

// A shutdown from the row removal ends the close there: the removal completes,
// no current-tab notification follows, and the closed session is destroyed when
// closeTab() returns.
void TestAppController::shutdownFromRowRemovalDuringClose() {
    Probe probe;
    AppController controller(fakeFormats(probe));
    QVERIFY(openTabs(controller, {A, B, C}));
    controller.setCurrentTabIndex(1);
    QSignalSpy index(&controller, &AppController::currentTabIndexChanged);
    QSignalSpy current(&controller, &AppController::currentSessionChanged);
    QSignalSpy removed(controller.tabModel(), &QAbstractItemModel::rowsRemoved);
    callOnce(controller.tabModel(), &QAbstractItemModel::rowsRemoved,
             [] { return true; }, [&controller] { controller.shutdown(); });

    controller.closeTab(1);

    QCOMPARE(removed.count(), 1);
    QCOMPARE(index.count(), 0);
    QCOMPARE(current.count(), 0);
    QCOMPARE(titles(controller), QStringList({A, C}));
    QCOMPARE(probe.destroyed(), QStringList({B}));
}

void TestAppController::shutdownFromIndexNotificationDuringClose() {
    Probe probe;
    AppController controller(fakeFormats(probe));
    QVERIFY(openTabs(controller, {A, B, C}));
    QSignalSpy index(&controller, &AppController::currentTabIndexChanged);
    QSignalSpy current(&controller, &AppController::currentSessionChanged);
    callOnce(&controller, &AppController::currentTabIndexChanged,
             [] { return true; }, [&controller] { controller.shutdown(); });

    controller.closeTab(2);

    QCOMPARE(index.count(), 1);
    QCOMPARE(current.count(), 0);
    QCOMPARE(probe.destroyed(), QStringList({C}));
}

// The closed session is destroyed after shutdown returns, when the close does.
void TestAppController::shutdownFromSessionNotificationDuringClose() {
    Probe probe;
    AppController controller(fakeFormats(probe));
    QVERIFY(openTabs(controller, {A, B, C}));
    controller.setCurrentTabIndex(1);
    callOnce(&controller, &AppController::currentSessionChanged, [] { return true; },
             [&controller, &probe] {
                 controller.shutdown();
                 probe.note(QStringLiteral("shutdown returned"));
             });
    const qsizetype from = probe.events().size();

    controller.closeTab(1);

    QCOMPARE(probe.events().mid(from), QStringList({QStringLiteral("shutdown returned"),
                                                    QStringLiteral("session destroyed")}));
}

// After shutdown a close changes nothing and notifies no one; the tabs go with
// the controller.
void TestAppController::closeAfterShutdown() {
    Probe probe;
    auto controller = std::make_unique<AppController>(fakeFormats(probe));
    QVERIFY(openTabs(*controller, {A, B}));
    controller->shutdown();
    QSignalSpy index(controller.get(), &AppController::currentTabIndexChanged);
    QSignalSpy current(controller.get(), &AppController::currentSessionChanged);
    QSignalSpy removed(controller->tabModel(), &QAbstractItemModel::rowsRemoved);

    controller->closeTab(0);
    QCoreApplication::processEvents();

    QCOMPARE(titles(*controller), QStringList({A, B}));
    QCOMPARE(index.count(), 0);
    QCOMPARE(current.count(), 0);
    QCOMPARE(removed.count(), 0);
    QVERIFY(probe.destroyed().isEmpty());
    controller.reset();
    QCOMPARE(sorted(probe.destroyed()), QStringList({A, B}));
}

void TestAppController::switchAfterShutdown() {
    Probe probe;
    AppController controller(fakeFormats(probe));
    QVERIFY(openTabs(controller, {A, B}));
    controller.shutdown();
    QSignalSpy index(&controller, &AppController::currentTabIndexChanged);
    QSignalSpy current(&controller, &AppController::currentSessionChanged);

    controller.setCurrentTabIndex(0);

    QCOMPARE(controller.currentTabIndex(), 1);
    QCOMPARE(index.count(), 0);
    QCOMPARE(current.count(), 0);
}

// Each public action is called with a value that would otherwise change state:
// after shutdown none changes state or notifies.
void TestAppController::everyPublicActionInertAfterShutdown() {
    Probe probe;
    AppController controller(fakeFormats(probe));
    QVERIFY(openTabs(controller, {A, B}));
    controller.openFile(QUrl::fromLocalFile(QDir::temp().filePath(QStringLiteral("notes.txt"))));
    const QString error = controller.lastError();
    QVERIFY(!error.isEmpty());
    const QString status = controller.startupStatusText();
    controller.shutdown();
    const qsizetype events = probe.events().size();
    const int loads = probe.loads();
    QSignalSpy index(&controller, &AppController::currentTabIndexChanged);
    QSignalSpy current(&controller, &AppController::currentSessionChanged);
    QSignalSpy errors(&controller, &AppController::lastErrorChanged);
    QSignalSpy loading(&controller, &AppController::fileLoadingChanged);
    QSignalSpy startup(&controller, &AppController::startupLoadingChanged);
    QSignalSpy statusText(&controller, &AppController::startupStatusTextChanged);
    QSignalSpy loaded(&controller, &AppController::fileLoaded);
    QSignalSpy inserted(controller.tabModel(), &QAbstractItemModel::rowsInserted);
    QSignalSpy removed(controller.tabModel(), &QAbstractItemModel::rowsRemoved);

    controller.openFile(fakeFile(C));
    controller.closeTab(0);
    controller.setCurrentTabIndex(0);
    controller.selectCurrentNode(1);
    controller.clearLastError();
    controller.setStartupLoading(false);
    controller.setStartupStatusText(QStringLiteral("Ready"));
    QVERIFY(QThreadPool::globalInstance()->waitForDone(10000));
    QCoreApplication::processEvents();

    QCOMPARE(titles(controller), QStringList({A, B}));
    QCOMPARE(controller.currentTabIndex(), 1);
    QCOMPARE(controller.lastError(), error);
    QVERIFY(controller.startupLoading());
    QCOMPARE(controller.startupStatusText(), status);
    QVERIFY(!controller.fileLoading());
    QCOMPARE(probe.loads(), loads);
    QCOMPARE(probe.events().size(), events);
    for (const QSignalSpy* spy : {&index, &current, &errors, &loading, &startup, &statusText,
                                  &loaded, &inserted, &removed}) {
        QCOMPARE(spy->count(), 0);
    }
}

// Closing the current tab from inside a close's index notification ends as two
// closes in sequence would. Each closed session is destroyed once, after its
// own close's notifications; the notification the nested close superseded is
// never sent.
void TestAppController::closeCurrentFromIndexNotification() {
    Probe probe;
    AppController controller(fakeFormats(probe));
    QVERIFY(openTabs(controller, {A, B, C}));
    controller.setCurrentTabIndex(1);
    noteNotifications(controller, probe);
    callOnce(&controller, &AppController::currentTabIndexChanged, [] { return true; },
             [&controller] { controller.closeTab(controller.currentTabIndex()); });
    const qsizetype from = probe.events().size();

    controller.closeTab(1);

    QCOMPARE(titles(controller), QStringList({A}));
    QCOMPARE(controller.currentTabIndex(), 0);
    QCOMPARE(probe.events().mid(from), QStringList({QStringLiteral("index"),
                                                    QStringLiteral("index"),
                                                    QStringLiteral("session"),
                                                    QStringLiteral("session destroyed"),
                                                    QStringLiteral("session destroyed")}));
    QCOMPARE(probe.destroyed(), QStringList({C, B}));
}

void TestAppController::closeCurrentFromSessionNotification() {
    Probe probe;
    AppController controller(fakeFormats(probe));
    QVERIFY(openTabs(controller, {A, B, C}));
    controller.setCurrentTabIndex(1);
    noteNotifications(controller, probe);
    callOnce(&controller, &AppController::currentSessionChanged, [] { return true; },
             [&controller] { controller.closeTab(controller.currentTabIndex()); });
    const qsizetype from = probe.events().size();

    controller.closeTab(1);

    QCOMPARE(titles(controller), QStringList({A}));
    QCOMPARE(controller.currentTabIndex(), 0);
    QCOMPARE(probe.events().mid(from), QStringList({QStringLiteral("index"),
                                                    QStringLiteral("session"),
                                                    QStringLiteral("index"),
                                                    QStringLiteral("session"),
                                                    QStringLiteral("session destroyed"),
                                                    QStringLiteral("session destroyed")}));
    QCOMPARE(probe.destroyed(), QStringList({C, B}));
}

// Keys are session-local: the same key selects in the current tab's session only.
void TestAppController::selectionStaysInCurrentTab() {
    Probe probe;
    AppController controller(fakeFormats(probe));
    QVERIFY(openTabs(controller, {A, B}));
    const qsizetype from = probe.events().size();

    controller.selectCurrentNode(1);
    controller.setCurrentTabIndex(0);
    controller.selectCurrentNode(1);

    QCOMPARE(probe.events().mid(from), QStringList({QStringLiteral("select b.fake 1"),
                                                    QStringLiteral("select a.fake 1")}));
}

// From the removal's about-to notification an observer switches to the closing
// tab, still in the model, and closes another. Both wait for the event loop and
// keep their tabs' identities: the closing tab never becomes current, and on
// delivery the switch to it does nothing while the other close proceeds.
void TestAppController::requestsDuringRowRemovalAreDeferred() {
    Probe probe;
    AppController controller(fakeFormats(probe));
    QVERIFY(openTabs(controller, {A, B, C}));
    controller.setCurrentTabIndex(1);
    QSignalSpy removed(controller.tabModel(), &QAbstractItemModel::rowsRemoved);
    callOnce(controller.tabModel(), &QAbstractItemModel::rowsAboutToBeRemoved, [] { return true; },
             [&controller] {
                 controller.setCurrentTabIndex(1);
                 controller.closeTab(0);
             });

    controller.closeTab(1);

    QCOMPARE(removed.count(), 1);
    QCOMPARE(titles(controller), QStringList({A, C}));
    QCOMPARE(currentTitle(controller), C);
    QCOMPARE(probe.destroyed(), QStringList({B}));

    QCoreApplication::processEvents();
    QCOMPARE(titles(controller), QStringList({C}));
    QCOMPARE(currentTitle(controller), C);
    QCOMPARE(probe.destroyed(), QStringList({B, A}));
}

// From the completed removal an observer asks by row for a switch and a close.
// Rows name tabs as they stand then, and the deferred requests keep those tabs.
void TestAppController::requestsAfterRowRemovalKeepTheirTabs() {
    Probe probe;
    AppController controller(fakeFormats(probe));
    QVERIFY(openTabs(controller, {A, B, C}));
    controller.setCurrentTabIndex(1);
    callOnce(controller.tabModel(), &QAbstractItemModel::rowsRemoved, [] { return true; },
             [&controller] {
                 controller.setCurrentTabIndex(0);   // A
                 controller.closeTab(1);             // C, now in B's row
             });

    controller.closeTab(1);
    QCoreApplication::processEvents();

    QCOMPARE(titles(controller), QStringList({A}));
    QCOMPARE(currentTitle(controller), A);
    QCOMPARE(probe.destroyed(), QStringList({B, C}));
}

// Deferred closes name tabs, not rows: delivering the first close shifts the rows,
// and the second still closes the tab it named.
void TestAppController::deferredRequestsKeepTabIdentity() {
    const QString D = QStringLiteral("d.fake");
    Probe probe;
    AppController controller(fakeFormats(probe));
    QVERIFY(openTabs(controller, {A, B, C, D}));
    controller.setCurrentTabIndex(1);
    callOnce(controller.tabModel(), &QAbstractItemModel::rowsAboutToBeRemoved, [] { return true; },
             [&controller] {
                 controller.closeTab(0);   // A
                 controller.closeTab(3);   // D
             });

    controller.closeTab(1);
    QCoreApplication::processEvents();

    QCOMPARE(titles(controller), QStringList({C}));
    QCOMPARE(currentTitle(controller), C);
    QCOMPARE(probe.destroyed(), QStringList({B, A, D}));
}

// While a loaded tab's row is inserted, an observer closes an earlier tab. The
// completion still selects the tab it inserted, by identity, and the close
// follows from the event loop.
void TestAppController::closeEarlierTabDuringInsertion() {
    Probe probe;
    AppController controller(fakeFormats(probe));
    QVERIFY(openTabs(controller, {A, B}));
    callOnce(controller.tabModel(), &QAbstractItemModel::rowsInserted, [] { return true; },
             [&controller] { controller.closeTab(0); });
    QString currentAtLoad;
    QObject::connect(&controller, &AppController::fileLoaded, &controller,
                     [&controller, &currentAtLoad] { currentAtLoad = currentTitle(controller); });

    QVERIFY(openTabs(controller, {C}));
    QCoreApplication::processEvents();

    QCOMPARE(currentAtLoad, C);
    QCOMPARE(titles(controller), QStringList({B, C}));
    QCOMPARE(currentTitle(controller), C);
    QCOMPARE(probe.destroyed(), QStringList({A}));
}

// An observer closes the inserted tab itself: the tab is selected and announced
// as loaded first, then closed from the event loop.
void TestAppController::closeInsertedTabDuringInsertion() {
    Probe probe;
    AppController controller(fakeFormats(probe));
    QVERIFY(openTabs(controller, {A, B}));
    callOnce(controller.tabModel(), &QAbstractItemModel::rowsInserted, [] { return true; },
             [&controller] { controller.closeTab(2); });
    QString currentAtLoad;
    QObject::connect(&controller, &AppController::fileLoaded, &controller,
                     [&controller, &currentAtLoad] { currentAtLoad = currentTitle(controller); });

    QVERIFY(openTabs(controller, {C}));
    QCoreApplication::processEvents();

    QCOMPARE(currentAtLoad, C);
    QCOMPARE(titles(controller), QStringList({A, B}));
    QCOMPARE(currentTitle(controller), B);
    QCOMPARE(probe.destroyed(), QStringList({C}));
}

// Requests deferred from a row insertion are delivered after a shutdown that
// came first: they do nothing.
void TestAppController::deferredRequestsAfterShutdownAreInert() {
    Probe probe;
    AppController controller(fakeFormats(probe));
    QVERIFY(openTabs(controller, {A, B}));
    callOnce(controller.tabModel(), &QAbstractItemModel::rowsInserted, [] { return true; },
             [&controller] {
                 controller.closeTab(0);
                 controller.setCurrentTabIndex(0);
             });
    callOnce(&controller, &AppController::fileLoaded, [] { return true; },
             [&controller] { controller.shutdown(); });
    QSignalSpy loaded(&controller, &AppController::fileLoaded);
    controller.openFile(fakeFile(C));
    QVERIFY(loaded.wait(10000));
    QSignalSpy index(&controller, &AppController::currentTabIndexChanged);
    QSignalSpy current(&controller, &AppController::currentSessionChanged);
    QSignalSpy removed(controller.tabModel(), &QAbstractItemModel::rowsRemoved);

    QCoreApplication::processEvents();

    QCOMPARE(titles(controller), QStringList({A, B, C}));
    QCOMPARE(currentTitle(controller), C);
    QCOMPARE(index.count(), 0);
    QCOMPARE(current.count(), 0);
    QCOMPARE(removed.count(), 0);
    QVERIFY(probe.destroyed().isEmpty());
}

// A shutdown from the removal's about-to notification lets the model finish the
// removal it began, then nothing more: no controller notification, and each
// removed session is destroyed once.
void TestAppController::shutdownDuringRowRemovalFinishesIt() {
    Probe probe;
    auto controller = std::make_unique<AppController>(fakeFormats(probe));
    QVERIFY(openTabs(*controller, {A, B, C}));
    controller->setCurrentTabIndex(1);
    QSignalSpy removed(controller->tabModel(), &QAbstractItemModel::rowsRemoved);
    QSignalSpy index(controller.get(), &AppController::currentTabIndexChanged);
    QSignalSpy current(controller.get(), &AppController::currentSessionChanged);
    callOnce(controller->tabModel(), &QAbstractItemModel::rowsAboutToBeRemoved,
             [] { return true; }, [&controller] { controller->shutdown(); });

    controller->closeTab(1);

    QCOMPARE(removed.count(), 1);
    QCOMPARE(titles(*controller), QStringList({A, C}));
    QCOMPARE(index.count(), 0);
    QCOMPARE(current.count(), 0);
    QCOMPARE(probe.destroyed(), QStringList({B}));
    controller.reset();
    QCOMPARE(sorted(probe.destroyed()), QStringList({A, B, C}));
}

QTEST_GUILESS_MAIN(TestAppController)
#include "tst_appcontroller.moc"
