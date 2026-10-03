#include "sessions/mdf4documentsession.h"

#include "models/signalplotmodel.h"
#include "sessions/presentertext.h"

#include <QObject>
#include <QPointer>
#include <QPromise>
#include <QtConcurrent/QtConcurrentRun>

#include <chrono>
#include <cstddef>
#include <utility>

namespace {

// Scan progress reaches the GUI at most this often.
constexpr std::chrono::milliseconds kProgressInterval{100};

QString groupTitle(const mdf4::ChannelGroup& group, int index) {
    return group.name().empty()
        ? QStringLiteral("Channel Group %1").arg(index + 1)
        : text(group.name());
}

QString channelTitle(const mdf4::Channel& channel, int index) {
    return channel.name().empty()
        ? QStringLiteral("Channel %1").arg(index + 1)
        : text(channel.name());
}

// A master carries the group's domain, so it is described by what it is rather
// than offered as a signal: decoding it returns its own samples in both time
// and value, which plots as a 45-degree line.
QString channelSubtitle(const mdf4::Channel& channel) {
    const QString unit = text(channel.unit());
    QString role;
    if (channel.is_master()) {
        role = QStringLiteral("Master channel");
    } else if (!channel.decodable()) {
        role = QStringLiteral("Not plottable");
    } else {
        return unit;
    }
    return unit.isEmpty() ? role : QStringLiteral("%1  ·  %2").arg(unit, role);
}

QString countText(std::uint64_t count, const char* noun) {
    return QStringLiteral("%1 %2%3")
        .arg(static_cast<qulonglong>(count))
        .arg(QLatin1String(noun), count == 1 ? QString() : QStringLiteral("s"));
}

// Rounded up, so a size never reads smaller than it is.
QString sizeText(std::uint64_t bytes) {
    return bytes < (std::uint64_t{1} << 20)
        ? QStringLiteral("%1 KiB").arg(static_cast<qulonglong>((bytes >> 10) + ((bytes & 0x3FF) != 0)))
        : QStringLiteral("%1 MiB").arg(static_cast<qulonglong>((bytes >> 20) + ((bytes & 0xFFFFF) != 0)));
}

// Why a finished scan shows nothing, in the plot's terms. `what` names the
// samples: the channel's, or the exact ones of a view. Cancelled work shows
// nothing at all and never gets here.
std::pair<PlotNote, QString> noteFor(const mdf4::ScanResult& scan, PlotBuild build,
                                     const QString& what) {
    const mdf4::Outcome& outcome = scan.outcome;
    const QString at = outcome.location.empty()
        ? QString()
        : QStringLiteral(" (%1)").arg(QString::fromStdString(outcome.location));
    if (build == PlotBuild::Fail || outcome.status == mdf4::Status::SourceChanged) {
        return {PlotNote::Failed,
                QStringLiteral("%1 could not be read: the file changed since it was opened; "
                               "reopen it")
                    .arg(what)};
    }
    if (outcome.status != mdf4::Status::ResourceLimit) {
        return {PlotNote::Failed, QStringLiteral("%1 could not be read: %2%3")
                                      .arg(what, QString::fromUtf8(outcome.message), at)};
    }
    switch (outcome.resource) {
    case mdf4::Resource::Consumer:
        // The window's own limit: the plot explains it.
        return {PlotNote::Refused, QString()};
    case mdf4::Resource::Scratch:
        return {PlotNote::Refused,
                QStringLiteral("%1 need more working memory than the decoder allows: %2 for one "
                               "step, %3 allowed%4")
                    .arg(what,
                         outcome.required > 0 ? sizeText(outcome.required)
                                              : QStringLiteral("more"),
                         sizeText(outcome.limit), at)};
    default:
        return {PlotNote::Refused, QStringLiteral("%1 exceed a reader limit: %2%3")
                                       .arg(what, QString::fromUtf8(outcome.message), at)};
    }
}

// Held by someone besides the cache: shown by the plot, pending or in flight
// as the result set of a window, or the result set of a cached window.
bool held(const PlotOverviewPtr& overview, const PlotWindowPtr& window) {
    return window ? window.use_count() > 1 : overview.use_count() > 1;
}

} // namespace

Mdf4DocumentSession::Mdf4DocumentSession(QString displayName,
                                         QString sourcePath,
                                         std::shared_ptr<const mdf4::File> metadata,
                                         ScanFunction scan,
                                         AxisFunction axis,
                                         QList<DiagnosticMessage> diagnostics,
                                         const Mdf4SessionLimits& limits)
    : AdapterSessionBase(FormatId::MDF4,
                         QStringLiteral("MDF4"),
                         std::move(displayName),
                         std::move(sourcePath),
                         std::move(diagnostics)),
      _metadata(std::move(metadata)),
      _limits(limits),
      _presenter(*_metadata),
      _scan(std::move(scan)),
      _axis(std::move(axis)),
      _plot_model(std::make_unique<SignalPlotModel>()) {
    QObject::connect(&_watcher, &QFutureWatcher<Completion>::finished, &_watcher,
                     [this] { onScanFinished(); });
    QObject::connect(&_watcher, &QFutureWatcher<Completion>::progressValueChanged, &_watcher,
                     [this](int permille) { onProgress(permille); });
    QObject::connect(_plot_model.get(), &SignalPlotModel::detailWanted, &_watcher,
                     [this] { onDetailWanted(); });
    buildTree();
}

Mdf4DocumentSession::~Mdf4DocumentSession() {
    // The task holds its own copy of the scan function and never touches this
    // session, so it may still release that copy on its worker after the wait
    // below.
    QObject::disconnect(&_watcher, nullptr, nullptr, nullptr);
    QObject::disconnect(_plot_model.get(), nullptr, &_watcher, nullptr);
    _pending.reset();
    if (_active) {
        _active->cancel->store(true);
        QFuture<Completion> active = _watcher.future();
        active.waitForFinished();
        const Completion discarded = active.takeResult();
    }
}

QUrl Mdf4DocumentSession::centerPanelSource() const {
    return QUrl(QStringLiteral("qrc:/qt/qml/ExplorerApp/qml/components/SignalPlotView.qml"));
}

QAbstractListModel* Mdf4DocumentSession::centerPanelModel() {
    return _plot_model.get();
}

// The detail panel shows the row first, then the plot follows. Observers of
// either may select anew or close this session from inside a notification;
// after each one the flow continues only while the session lives and no newer
// selection has run.
void Mdf4DocumentSession::selectNode(quint64 key) {
    const std::uint64_t selection = ++_selections;
    QPointer<SignalPlotModel> model(_plot_model.get());
    const auto it = _paths.find(key);
    const Mdf4Path path = it == _paths.end() ? Mdf4Path{} : it->second;
    if (it == _paths.end()) {
        _detail_model.setSelection({}, {});
    } else {
        _detail_model.setSelection(_presenter.buildDetails(path),
                                   [this, path] { return _presenter.buildRawJson(path); });
    }
    if (!model || selection != _selections) {
        return;
    }
    if (it != _paths.end() && path.kind == Mdf4EntityKind::Channel) {
        selectChannel(path, selection);
        return;
    }
    // A row that is no channel: nothing to plot, no scan wanted.
    _selected.reset();
    _pending.reset();
    cancelActive();
    model->clear();
    if (model && selection == _selections) {
        updateBusy();
    }
}

TreeItem* Mdf4DocumentSession::appendEntity(TreeItem* parent, const QString& title,
                                            const QString& subtitle, const QString& iconKey,
                                            SemanticKind semanticKind, Mdf4Path path) {
    TreeItem* item = appendNode(parent, title, subtitle, iconKey, semanticKind, true);
    _paths.emplace(item->nodeKey, path);
    return item;
}

void Mdf4DocumentSession::moveModelsToThread(QThread* thread) {
    AdapterSessionBase::moveModelsToThread(thread);
    _plot_model->moveToThread(thread);
    _watcher.moveToThread(thread);
}

Mdf4DocumentSession::ResultBytes Mdf4DocumentSession::resultBytes() const {
    ResultBytes bytes;
    bytes.retained = _retained_bytes;
    bytes.reserved = _active ? _active->reserved : 0;
    bytes.results = _retained.size();
    for (const Retained& entry : _retained) {
        if (held(entry.overview, entry.window)) {
            ++bytes.held;
        }
    }
    return bytes;
}

// The domain is the axis the source resolved at opening: its time master, or
// the sample index when the group has none.
PlotHeader Mdf4DocumentSession::header(ChannelKey key) const {
    const int channelIndex = static_cast<int>(key.second);
    const mdf4::Channel& channel =
        _metadata->groups(static_cast<int>(key.first)).channels(channelIndex);
    PlotHeader result{channelTitle(channel, channelIndex), text(channel.unit()),
                      QStringLiteral("Sample index"), {}};
    const mdf4::Axis axis = _axis(key.first);
    if (axis.kind == mdf4::AxisKind::Master) {
        const mdf4::Channel& master = _metadata->groups(static_cast<int>(axis.group))
                                          .channels(static_cast<int>(axis.channel));
        result.domainName = master.name().empty() ? QStringLiteral("Time") : text(master.name());
        result.domainUnit = text(master.unit());
    }
    return result;
}

std::uint64_t Mdf4DocumentSession::statedCount(ChannelKey key) const {
    return _metadata->groups(static_cast<int>(key.first))
        .channels(static_cast<int>(key.second))
        .sample_count();
}

// Plot model observers run synchronously and may select another node or close
// this session. Every flow therefore settles the scans, the cache and the
// selection first and notifies last, the busy state last of all, and touches
// nothing after a notification that destroyed the session. A selection flow
// also stops after a notification inside which a newer selection ran.
void Mdf4DocumentSession::selectChannel(const Mdf4Path& path, std::uint64_t selection) {
    const ChannelKey key{static_cast<std::uint32_t>(path.groupIndex),
                         static_cast<std::uint32_t>(path.channelIndex)};
    const mdf4::Channel& channel =
        _metadata->groups(path.groupIndex).channels(path.channelIndex);
    QPointer<SignalPlotModel> model(_plot_model.get());

    // A master is the group's own domain, not a signal against it.
    if (channel.is_master() || !channel.decodable()) {
        _selected.reset();
        _pending.reset();
        cancelActive();
        model->setSignal(header(key), {}, PlotNote::Empty,
                         channel.is_master()
                             ? QStringLiteral("Master channel — this group's time axis")
                             : QStringLiteral("This channel type is not plottable"));
        if (model && selection == _selections) {
            updateBusy();
        }
        return;
    }

    _selected = key;
    if (Retained* cached = retainedOverview(key)) {
        cached->lastUse = ++_clock;
        const PlotOverviewPtr overview = cached->overview;
        _pending.reset();
        cancelActive();
        // Showing it asks for the detail of the whole view.
        model->setSignal(header(key), overview);
        if (model && selection == _selections) {
            updateBusy();
        }
        return;
    }

    // The overview in flight for this channel serves; anything else is obsolete.
    if (_active && !_active->cancel->load() && !_active->request.set &&
        _active->request.channel == key) {
        _pending.reset();
    } else {
        cancelActive();
        _pending = Request{key, nullptr, {}};
    }
    model->setSignal(header(key));
    if (!model || selection != _selections) {
        return;
    }
    startPending();
    if (model && selection == _selections) {
        updateBusy();
    }
}

// The view shows more than the plot's window covers: install a cached window
// that covers it, keep the scan in flight whose range covers it, or ask for the
// window the plot names. Windows for other views are obsolete.
void Mdf4DocumentSession::onDetailWanted() {
    const PlotOverviewPtr set = _plot_model->overview();
    if (!_selected || !set) {
        return;
    }
    const ChannelKey key = *_selected;
    const double start = _plot_model->viewStart();
    const double end = _plot_model->viewEnd();
    const std::optional<PlotWindowRequest> wanted = _plot_model->detailRequest();
    const bool windowActive = _active && _active->request.set;
    QPointer<SignalPlotModel> model(_plot_model.get());

    if (_pending && _pending->set) {
        _pending.reset();
    }
    if (!wanted) {
        // Too many samples in view for one window.
        if (windowActive) {
            cancelActive();
        }
        updateBusy();
        return;
    }
    if (Retained* cached = retainedWindow(key, set.get(), start, end)) {
        cached->lastUse = ++_clock;
        const PlotWindowPtr window = cached->window;
        if (windowActive) {
            cancelActive();
        }
        model->setWindow(window);
        if (model) {
            updateBusy();
        }
        return;
    }
    if (windowActive && !_active->cancel->load() && _active->request.channel == key &&
        _active->request.set == set && _active->request.window.start <= start &&
        end <= _active->request.window.end) {
        updateBusy();
        return;
    }
    if (windowActive) {
        cancelActive();
    }
    _pending = Request{key, set, *wanted};
    startPending();
    if (model) {
        updateBusy();
    }
}

// Takes the completion and settles before notifying.
void Mdf4DocumentSession::onScanFinished() {
    QPointer<SignalPlotModel> model(_plot_model.get());
    if (_active->cancel->load()) {
        // Cancelled work keeps nothing, even when its scan finished first.
        // Taking the completion releases its result while the reservation still
        // covers it; the request goes with the reservation, before anything
        // else is admitted or announced.
        _watcher.future().takeResult();
        _active.reset();
    } else {
        // The reservation becomes the result's own charge in one step. The
        // work is for the selection and view as they stand.
        Completion completion = _watcher.future().takeResult();
        const Request request = std::move(_active->request);
        if (completion.overview) {
            retain({request.channel, completion.overview, {}, completion.overview->bytes(),
                    ++_clock});
        } else if (completion.window) {
            retain({request.channel, request.set, completion.window, completion.window->bytes(),
                    ++_clock});
        }
        _active.reset();

        if (!request.set) {
            if (completion.overview) {
                model->setSignal(header(request.channel), completion.overview);
            } else {
                const auto [note, text] =
                    noteFor(completion.scan, completion.build, QStringLiteral("Samples"));
                model->setSignal(header(request.channel), {}, note, text);
            }
        } else if (completion.window) {
            model->setWindow(completion.window);
        } else {
            const auto [note, text] =
                noteFor(completion.scan, completion.build, QStringLiteral("Exact samples"));
            model->setWindow({}, note, text);
        }
        if (!model) {
            return;
        }
    }
    startPending();
    if (model) {
        updateBusy();
    }
}

// Progress of the scan in flight, already coalesced on the worker. Progress of
// cancelled work, or of another channel's, never reaches the plot.
void Mdf4DocumentSession::onProgress(int permille) {
    if (!_active || _active->cancel->load() || _selected != _active->request.channel) {
        return;
    }
    _active->progress = permille / 1000.0;
    _plot_model->setProgress(_active->progress);
}

void Mdf4DocumentSession::cancelActive() {
    if (_active) {
        _active->cancel->store(true);
    }
}

void Mdf4DocumentSession::startPending() {
    if (_active || !_pending) {
        return;
    }
    Request request = std::move(*_pending);
    _pending.reset();
    const std::uint64_t bytes = request.set
        ? PlotWindowBuilder::reservation(request.window)
        : PlotOverviewBuilder::reservation(statedCount(request.channel));
    if (admit(bytes)) {
        launch(std::move(request), bytes);
        return;
    }

    // Results in use leave no room: say so where the result would have shown.
    const QString text =
        QStringLiteral("%1 need %2 of plot memory, but results in use hold %3 of the plot's %4")
            .arg(request.set ? QStringLiteral("Exact samples") : QStringLiteral("Samples"),
                 sizeText(bytes), sizeText(heldBytes()), sizeText(_limits.resultBytes));
    if (request.set) {
        _plot_model->setWindow({}, PlotNote::Refused, text);
    } else {
        _plot_model->setSignal(header(request.channel), {}, PlotNote::Refused, text);
    }
}

// The task owns a copy of the scan function, and with it the source, plus plain
// request values: it never reaches this session, its models or its results. It
// builds its result from the scan's chunks and hands it over only after an Ok
// outcome, when the reader has checked the source after the traversal.
void Mdf4DocumentSession::launch(Request request, std::uint64_t reserved) {
    auto cancel = std::make_shared<std::atomic<bool>>(false);
    const ChannelKey channel = request.channel;
    const bool window = request.set != nullptr;
    const PlotWindowRequest cover = request.window;
    const std::uint64_t stated = statedCount(channel);
    _active = Active{std::move(request), cancel, reserved, -1.0};
    _watcher.setFuture(QtConcurrent::run(
        [scan = _scan, channel, window, cover, stated, cancel](QPromise<Completion>& promise) {
            // At most one progress value per interval, and only when it grew.
            promise.setProgressRange(0, 1000);
            auto shownAt = std::chrono::steady_clock::now() - kProgressInterval;
            int shown = 0;
            mdf4::Control control;
            control.cancel = cancel.get();
            control.progress = [&](std::uint64_t done, std::uint64_t total) {
                const auto now = std::chrono::steady_clock::now();
                if (total == 0 || now - shownAt < kProgressInterval) {
                    return;
                }
                const int permille = static_cast<int>(
                    static_cast<double>(std::min(done, total)) * 1000.0 /
                    static_cast<double>(total));
                if (permille > shown) {
                    shown = permille;
                    shownAt = now;
                    promise.setProgressValue(permille);
                }
            };

            Completion completion;
            if (window) {
                PlotWindowBuilder builder(cover);
                completion.scan = scan(
                    channel.first, channel.second, cover.firstSample, cover.sampleCount, control,
                    [&builder](const mdf4::SampleChunk& chunk) {
                        switch (builder.add(chunk.firstSample, chunk.time, chunk.value, chunk.size)) {
                        case PlotBuild::Continue: return mdf4::Visit::Continue;
                        case PlotBuild::Refuse: return mdf4::Visit::ResourceLimit;
                        case PlotBuild::Fail: break;
                        }
                        return mdf4::Visit::Cancel;
                    });
                if (completion.scan.outcome.status == mdf4::Status::Ok) {
                    completion.window = builder.finish();
                }
                completion.build = builder.state();
            } else {
                PlotOverviewBuilder builder(stated);
                completion.scan = scan(channel.first, channel.second, 0, stated, control,
                                       [&builder](const mdf4::SampleChunk& chunk) {
                                           builder.add(chunk.firstSample, chunk.time,
                                                       chunk.value, chunk.size);
                                           return mdf4::Visit::Continue;
                                       });
                if (completion.scan.outcome.status == mdf4::Status::Ok) {
                    completion.overview = builder.finish(completion.scan.sampleCount);
                }
            }
            promise.addResult(std::move(completion));
        }));
}

// Busy while the selected channel has a scan pending or in flight that was not
// cancelled; progress is that scan's. An observer of the progress may select or
// view anew and start a scan, so the busy state is read after it returns.
void Mdf4DocumentSession::updateBusy() {
    const auto running = [this] {
        return _active && !_active->cancel->load() && _selected == _active->request.channel;
    };
    QPointer<SignalPlotModel> model(_plot_model.get());
    model->setProgress(running() ? _active->progress : -1.0);
    if (model) {
        model->setBusy(running() || (_pending && _selected == _pending->channel));
    }
}

bool Mdf4DocumentSession::admit(std::uint64_t bytes) {
    while (_retained_bytes + bytes > _limits.resultBytes) {
        auto victim = _retained.end();
        for (auto it = _retained.begin(); it != _retained.end(); ++it) {
            if (!held(it->overview, it->window) &&
                (victim == _retained.end() || it->lastUse < victim->lastUse)) {
                victim = it;
            }
        }
        if (victim == _retained.end()) {
            return false;
        }
        _retained_bytes -= victim->bytes;
        _retained.erase(victim);
    }
    return true;
}

std::uint64_t Mdf4DocumentSession::heldBytes() const {
    std::uint64_t bytes = 0;
    for (const Retained& entry : _retained) {
        bytes += held(entry.overview, entry.window) ? entry.bytes : 0;
    }
    return bytes;
}

Mdf4DocumentSession::Retained* Mdf4DocumentSession::retainedOverview(ChannelKey key) {
    for (Retained& entry : _retained) {
        if (!entry.window && entry.channel == key) {
            return &entry;
        }
    }
    return nullptr;
}

Mdf4DocumentSession::Retained* Mdf4DocumentSession::retainedWindow(ChannelKey key,
                                                                    const PlotOverview* set,
                                                                    double start, double end) {
    for (Retained& entry : _retained) {
        if (entry.window && entry.channel == key && entry.overview.get() == set &&
            entry.window->covers(start, end)) {
            return &entry;
        }
    }
    return nullptr;
}

void Mdf4DocumentSession::retain(Retained entry) {
    _retained_bytes += entry.bytes;
    _retained.push_back(std::move(entry));
}

// Measured with the process allocator, a row costs 318 to 322 bytes of commit
// before its text; UTF-16 text costs at most two bytes per UTF-8 byte, and the
// allocator rounds long strings up by less than an eighth.
std::uint64_t Mdf4DocumentSession::treeBytes(const mdf4::File& document) {
    constexpr std::uint64_t kRowBytes = 352;
    std::uint64_t rows = 1;
    std::uint64_t textBytes = 64 + document.version().size();
    for (const mdf4::ChannelGroup& group : document.groups()) {
        ++rows;
        // The name or "Channel Group <n>", then "<cycle count> samples".
        textBytes += (group.name().empty() ? 24 : group.name().size()) + 28;
        for (const mdf4::Channel& channel : group.channels()) {
            ++rows;
            // The name or "Channel <n>", then the unit and an axis or unplottable role.
            textBytes += (channel.name().empty() ? 18 : channel.name().size()) +
                         channel.unit().size() +
                         (channel.is_master() || !channel.decodable() ? 19 : 0);
        }
    }
    return rows * kRowBytes + textBytes * 9 / 4;
}

void Mdf4DocumentSession::buildTree() {
    auto root = std::make_unique<TreeItem>();
    root->title = displayName();
    root->semanticKind = SemanticKind::Root;

    TreeItem* file = appendEntity(
        root.get(),
        displayName(),
        _metadata->version().empty() ? QStringLiteral("MDF4")
                                     : QStringLiteral("MDF %1").arg(text(_metadata->version())),
        QStringLiteral("file"),
        SemanticKind::Root,
        Mdf4Path{Mdf4EntityKind::File, -1, -1});

    // The tree is admitted before any group or channel row exists. Past its
    // allowance the file row stands alone, with its details, and a diagnostic
    // says why nothing is listed under it.
    const std::uint64_t required = treeBytes(*_metadata);
    if (required > _limits.treeBytes) {
        std::uint64_t channels = 0;
        for (const mdf4::ChannelGroup& group : _metadata->groups()) {
            channels += static_cast<std::uint64_t>(group.channels_size());
        }
        const auto groups = static_cast<std::uint64_t>(_metadata->groups_size());
        file->subtitle = QStringLiteral("%1  ·  channel tree not shown").arg(file->subtitle);
        addDiagnostic({DiagnosticSeverity::Error,
                       QStringLiteral("Channel tree not shown"),
                       QStringLiteral("Listing %1 in %2 needs about %3; the navigation tree's "
                                      "allowance is %4.")
                           .arg(countText(channels, "channel"), countText(groups, "channel group"),
                                sizeText(required), sizeText(_limits.treeBytes))});
        setRootItem(std::move(root));
        return;
    }

    for (int groupIndex = 0; groupIndex < _metadata->groups_size(); ++groupIndex) {
        const auto& group = _metadata->groups(groupIndex);
        TreeItem* groupItem = appendEntity(
            file,
            groupTitle(group, groupIndex),
            QStringLiteral("%1 samples").arg(group.cycle_count()),
            QStringLiteral("channel-group"),
            SemanticKind::Section,
            Mdf4Path{Mdf4EntityKind::ChannelGroup, groupIndex, -1});

        for (int channelIndex = 0; channelIndex < group.channels_size(); ++channelIndex) {
            const auto& channel = group.channels(channelIndex);
            SemanticKind semanticKind = SemanticKind::Entity;
            if (channel.is_master()) {
                semanticKind = SemanticKind::Attribute;
            } else if (!channel.decodable()) {
                semanticKind = SemanticKind::Diagnostic;
            }
            appendEntity(
                groupItem,
                channelTitle(channel, channelIndex),
                channelSubtitle(channel),
                channel.decodable() ? QStringLiteral("channel")
                                    : QStringLiteral("channel-unsupported"),
                semanticKind,
                Mdf4Path{Mdf4EntityKind::Channel, groupIndex, channelIndex});
        }
    }

    setRootItem(std::move(root));
}

