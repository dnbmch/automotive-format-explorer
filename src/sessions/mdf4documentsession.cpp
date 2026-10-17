#include "sessions/mdf4documentsession.h"

#include "models/signalplotmodel.h"
#include "models/valuetablemodel.h"
#include "sessions/presentertext.h"

#include <QObject>
#include <QPointer>
#include <QPromise>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <utility>

namespace {

// Read progress reaches the GUI at most this often.
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

QString countText(std::uint64_t count, const char* noun) {
    return QStringLiteral("%1 %2%3")
        .arg(static_cast<qulonglong>(count))
        .arg(QLatin1String(noun), count == 1 ? QString() : QStringLiteral("s"));
}

// The channels of `group` whose member_of is `channel`: a structure's direct
// members.
std::uint64_t memberCount(const mdf4::ChannelGroup& group, std::uint32_t channel) {
    std::uint64_t members = 0;
    for (const mdf4::Channel& member : group.channels()) {
        if (member.has_member_of() && member.member_of() == channel) {
            ++members;
        }
    }
    return members;
}

// A master-typed member of a compact structure is never its group's axis (MDF
// Bus Logging AS §4.2.4; the reader reads it as a value channel), so only a
// master outside a structure is the axis.
bool isAxis(const mdf4::Channel& channel) {
    return channel.is_master() && !channel.has_member_of();
}

// A master carries the group's domain, so it is described by what it is rather
// than offered as a signal: decoding it returns its own samples in both time
// and value, which plots as a 45-degree line. A text channel's samples are text,
// a Byte channel's raw Bytes, a structure's Bytes beside its `members` direct
// members, and an array channel's sample is a whole array, so their roles say
// so, the array's as its type and shape. A plottable channel has no role.
QString channelRole(const mdf4::Channel& channel, std::uint64_t members) {
    if (isAxis(channel)) {
        return QStringLiteral("Master channel");
    }
    if (channel.textual()) {
        return QStringLiteral("Text channel");
    }
    if (channel.binary()) {
        return members == 0 ? QStringLiteral("Byte channel")
                            : QStringLiteral("Structure of %1").arg(countText(members, "member"));
    }
    if (channel.has_array()) {
        return QStringLiteral("%1 %2").arg(arrayTypeText(channel.array().ca_type()),
                                           arrayShapeText(channel.array()));
    }
    if (!channel.decodable()) {
        return QStringLiteral("Not plottable");
    }
    return {};
}

QString channelSubtitle(const mdf4::Channel& channel, std::uint64_t members) {
    const QString unit = text(channel.unit());
    const QString role = channelRole(channel, members);
    if (role.isEmpty()) {
        return unit;
    }
    return unit.isEmpty() ? role : QStringLiteral("%1  ·  %2").arg(unit, role);
}

// Rounded up, so a size never reads smaller than it is.
QString sizeText(std::uint64_t bytes) {
    return bytes < (std::uint64_t{1} << 20)
        ? QStringLiteral("%1 KiB").arg(static_cast<qulonglong>((bytes >> 10) + ((bytes & 0x3FF) != 0)))
        : QStringLiteral("%1 MiB").arg(static_cast<qulonglong>((bytes >> 20) + ((bytes & 0xFFFFF) != 0)));
}

// Why a finished read shows nothing, in the plot's terms. `what` names the
// values: the channel's samples, the exact ones of a view, or a table's rows.
// Cancelled work shows nothing at all and never gets here.
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

// The same note in the table's terms; noteFor() gives only failures and
// refusals.
TableNote tableNote(PlotNote note) {
    return note == PlotNote::Refused ? TableNote::Refused : TableNote::Failed;
}

// The name of dimension `d`'s axis: its axis channel, titled as the tree titles
// it, else the dimension as the detail panel's Array card counts it.
QString axisName(const mdf4::File& document, const mdf4::ChannelArray& array, int d) {
    const mdf4::ArrayDimension& dimension = array.dimensions(d);
    if (!dimension.axis_from_channel()) {
        return QStringLiteral("Axis %1").arg(d + 1);
    }
    return text(mdf4page::channelTitle(document.groups(static_cast<int>(dimension.axis_group())),
                                       static_cast<int>(dimension.axis_channel())));
}

TableAxis tableAxis(mdf4array::AxisLabels&& labels) {
    TableAxis axis;
    switch (labels.kind) {
    case mdf4array::AxisLabels::Kind::Index:
        axis.kind = TableAxis::Kind::Index;
        break;
    case mdf4array::AxisLabels::Kind::Points:
        axis.kind = TableAxis::Kind::Points;
        break;
    case mdf4array::AxisLabels::Kind::Intervals:
        axis.kind = TableAxis::Kind::Intervals;
        break;
    }
    axis.values = std::move(labels.values);
    axis.leftOpen = labels.leftOpen;
    axis.note = std::move(labels.note);
    return axis;
}

// The slice holds its sample's moved vectors in a smaller structure, so its
// bytes() stays within the sample's reservation.
static_assert(sizeof(ArraySlice) <= sizeof(mdf4array::ArraySample));

ArraySlicePtr sliceOf(mdf4array::ArraySample&& sample, std::uint64_t index) {
    auto slice = std::make_shared<ArraySlice>();
    slice->sample = index;
    slice->time = sample.time;
    slice->rows = sample.rows;
    slice->columns = sample.columns;
    slice->values = std::move(sample.values);
    slice->rowAxis = tableAxis(std::move(sample.rowLabels));
    slice->columnAxis = tableAxis(std::move(sample.columnLabels));
    return slice;
}

// Why array sample `sample` shows nothing although its read succeeded: the
// source holds fewer samples than it states, and no more than `sample`.
QString pastEndText(std::uint64_t sample) {
    return sample == 0
        ? QStringLiteral("No samples: the recording ends before its first one")
        : QStringLiteral("No sample %1: the recording ends after at most %1 samples")
              .arg(QString::number(static_cast<qulonglong>(sample)));
}

// The control of one read: its cancellation flag, and progress passed on at
// most once per interval and only when it grew.
template <typename Promise>
mdf4::Control readControl(Promise& promise, const std::atomic<bool>* cancel) {
    promise.setProgressRange(0, 1000);
    mdf4::Control control;
    control.cancel = cancel;
    control.progress = [&promise, shownAt = std::chrono::steady_clock::now() - kProgressInterval,
                        shown = 0](std::uint64_t done, std::uint64_t total) mutable {
        const auto now = std::chrono::steady_clock::now();
        if (total == 0 || now - shownAt < kProgressInterval) {
            return;
        }
        const int permille = static_cast<int>(static_cast<double>(std::min(done, total)) *
                                              1000.0 / static_cast<double>(total));
        if (permille > shown) {
            shown = permille;
            shownAt = now;
            promise.setProgressValue(permille);
        }
    };
    return control;
}

} // namespace

Mdf4DocumentSession::Mdf4DocumentSession(QString displayName,
                                         QString sourcePath,
                                         std::shared_ptr<const mdf4::File> metadata,
                                         Mdf4Source source,
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
      _source(std::move(source)),
      _plot_model(std::make_unique<SignalPlotModel>()),
      _table_model(std::make_unique<ValueTableModel>()) {
    QObject::connect(&_watcher, &QFutureWatcher<Completion>::finished, &_watcher,
                     [this] { onScanFinished(); });
    QObject::connect(&_watcher, &QFutureWatcher<Completion>::progressValueChanged, &_watcher,
                     [this](int permille) { onProgress(permille); });
    QObject::connect(_plot_model.get(), &SignalPlotModel::detailWanted, &_watcher,
                     [this] { onDetailWanted(); });
    QObject::connect(_table_model.get(), &ValueTableModel::requestWanted, &_watcher,
                     [this] { onTableWanted(); });
    buildTree();
}

Mdf4DocumentSession::~Mdf4DocumentSession() {
    // The task holds its own copy of the source function and never touches this
    // session, so it may still release that copy on its worker after the wait
    // below.
    QObject::disconnect(&_watcher, nullptr, nullptr, nullptr);
    QObject::disconnect(_plot_model.get(), nullptr, &_watcher, nullptr);
    QObject::disconnect(_table_model.get(), nullptr, &_watcher, nullptr);
    _pending.reset();
    if (_active) {
        _active->cancel->store(true);
        QFuture<Completion> active = _watcher.future();
        active.waitForFinished();
        const Completion discarded = active.takeResult();
    }
}

QUrl Mdf4DocumentSession::centerPanelSource() const {
    return QUrl(_center == Center::Table
                    ? QStringLiteral("qrc:/qt/qml/ExplorerApp/qml/components/ValueTableView.qml")
                    : QStringLiteral("qrc:/qt/qml/ExplorerApp/qml/components/SignalPlotView.qml"));
}

QAbstractItemModel* Mdf4DocumentSession::centerPanelModel() {
    if (_center == Center::Table) {
        return _table_model.get();
    }
    return _plot_model.get();
}

// The detail panel shows the row first, then the center follows. Observers of
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
    // A row that is no channel: nothing to show, no read wanted.
    _selected.reset();
    _pending.reset();
    cancelActive();
    if (!showPlot(selection)) {
        return;
    }
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
    _table_model->moveToThread(thread);
    _watcher.moveToThread(thread);
}

Mdf4DocumentSession::ResultBytes Mdf4DocumentSession::resultBytes() const {
    ResultBytes bytes;
    bytes.retained = _retained_bytes;
    bytes.reserved = _active ? _active->reserved : 0;
    bytes.results = _retained.size();
    for (const Retained& entry : _retained) {
        if (entry.held()) {
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
    const mdf4::Axis axis = _source.axis(key.first);
    if (axis.kind == mdf4::AxisKind::Master) {
        const mdf4::Channel& master = _metadata->groups(static_cast<int>(axis.group))
                                          .channels(static_cast<int>(axis.channel));
        result.domainName = master.name().empty() ? QStringLiteral("Time") : text(master.name());
        result.domainUnit = text(master.unit());
    }
    return result;
}

// The time is the time master's: a page's time column, an array sample's time.
// A group without one shows its samples by their indices alone. A page's
// columns are its plan's lanes, so the header and the read agree on their
// order. An array names the axes of its rows and columns, its last two
// dimensions, or of its rows alone when it has one.
TableHeader Mdf4DocumentSession::tableHeader(ChannelKey key) const {
    const mdf4::ChannelGroup& group = _metadata->groups(static_cast<int>(key.first));
    const mdf4::Channel& channel = group.channels(static_cast<int>(key.second));
    const PlotHeader plot = header(key);
    TableHeader result;
    result.name = plot.name;
    result.role = channelRole(channel, memberCount(group, key.second));
    result.unit = plot.unit;
    result.sampleCount = statedCount(key);
    if (_source.axis(key.first).kind == mdf4::AxisKind::Master) {
        result.timeName = plot.domainName;
        result.timeUnit = plot.domainUnit;
    }
    if (!channel.has_array()) {
        const mdf4page::PagePlan plan = mdf4page::pagePlan(*_metadata, key.first, key.second, 0);
        result.values = QString::fromLatin1(plan.values);
        for (const mdf4page::Lane& lane : plan.members) {
            result.columns.push_back({text(lane.name), lane.kind == mdf4page::Lane::Kind::Values});
        }
        result.columns.push_back({plan.kind == mdf4page::Lane::Kind::Text ? QStringLiteral("Text")
                                                                          : QStringLiteral("Bytes"),
                                  false});
    } else {
        result.values = QStringLiteral("Values");
        const mdf4::ChannelArray& array = channel.array();
        for (const mdf4::ArrayDimension& dimension : array.dimensions()) {
            result.dimensions.push_back(dimension.size());
        }
        const int dimensions = array.dimensions_size();
        if (dimensions == 1) {
            result.rowAxis = axisName(*_metadata, array, 0);
        } else {
            result.rowAxis = axisName(*_metadata, array, dimensions - 2);
            result.columnAxis = axisName(*_metadata, array, dimensions - 1);
        }
    }
    return result;
}

Mdf4DocumentSession::Request Mdf4DocumentSession::tableRequest(
    ChannelKey key, std::uint64_t sample, const std::vector<std::uint64_t>& slice) const {
    const mdf4::Channel& channel = _metadata->groups(static_cast<int>(key.first))
                                       .channels(static_cast<int>(key.second));
    if (!channel.has_array()) {
        Request request{Request::Kind::Page, key, nullptr, {}, pageFirst(sample)};
        request.page = mdf4page::pagePlan(*_metadata, key.first, key.second, request.first);
        return request;
    }
    Request request{Request::Kind::ArraySample, key, nullptr, {}, sample};
    request.plan = mdf4array::arrayPlan(*_metadata, _source.axis, key.first, key.second, sample,
                                        slice);
    return request;
}

std::uint64_t Mdf4DocumentSession::statedCount(ChannelKey key) const {
    return _metadata->groups(static_cast<int>(key.first))
        .channels(static_cast<int>(key.second))
        .sample_count();
}

// Plot and table observers run synchronously and may select another node or
// close this session. Every flow therefore settles the reads, the cache, the
// selection and the center first and notifies last, the busy state last of
// all, and touches nothing after a notification that destroyed the session. A
// selection flow also stops after a notification inside which a newer
// selection ran.
void Mdf4DocumentSession::selectChannel(const Mdf4Path& path, std::uint64_t selection) {
    const ChannelKey key{static_cast<std::uint32_t>(path.groupIndex),
                         static_cast<std::uint32_t>(path.channelIndex)};
    const mdf4::Channel& channel =
        _metadata->groups(path.groupIndex).channels(path.channelIndex);
    if (!isAxis(channel) && channel.decodable() &&
        (channel.textual() || channel.binary() || channel.has_array())) {
        selectTable(key, selection);
        return;
    }
    QPointer<SignalPlotModel> model(_plot_model.get());

    // A master is the group's own domain, not a signal against it.
    if (isAxis(channel) || !channel.decodable()) {
        _selected.reset();
        _pending.reset();
        cancelActive();
        if (!showPlot(selection)) {
            return;
        }
        model->setSignal(header(key), {}, PlotNote::Empty,
                         isAxis(channel) ? QStringLiteral("Master channel — this group's time axis")
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
        if (!showPlot(selection)) {
            return;
        }
        // Showing it asks for the detail of the whole view.
        model->setSignal(header(key), overview);
        if (model && selection == _selections) {
            updateBusy();
        }
        return;
    }

    // The overview in flight for this channel serves; anything else is obsolete.
    if (_active && !_active->cancel->load() && _active->request.kind == Request::Kind::Overview &&
        _active->request.channel == key) {
        _pending.reset();
    } else {
        cancelActive();
        _pending = Request{Request::Kind::Overview, key, nullptr, {}, 0};
    }
    if (!showPlot(selection)) {
        return;
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

// A decodable text, Byte or array channel: the table shows the first page, or
// the array's sample 0 with every slice index 0, from the cache at once, from
// the read already in flight for it, or from a new read.
void Mdf4DocumentSession::selectTable(ChannelKey key, std::uint64_t selection) {
    Request wanted = tableRequest(key, 0, {});
    const bool array = wanted.kind == Request::Kind::ArraySample;
    _selected = key;
    _center = Center::Table;
    TablePagePtr page;
    ArraySlicePtr slice;
    if (Retained* cached = retainedTable(wanted)) {
        cached->lastUse = ++_clock;
        page = cached->page;
        slice = cached->slice;
        _pending.reset();
        cancelActive();
    } else if (_active && !_active->cancel->load() && _active->request.sameTableRead(wanted)) {
        _pending.reset();
    } else {
        cancelActive();
        _pending = std::move(wanted);
    }
    QPointer<SignalPlotModel> model(_plot_model.get());
    model->clear();
    if (!model || selection != _selections) {
        return;
    }
    if (array) {
        _table_model->setArrayChannel(tableHeader(key), std::move(slice));
    } else {
        _table_model->setPageChannel(tableHeader(key), std::move(page));
    }
    if (!model || selection != _selections) {
        return;
    }
    startPending();
    if (model && selection == _selections) {
        updateBusy();
    }
}

bool Mdf4DocumentSession::showPlot(std::uint64_t selection) {
    if (_center == Center::Plot) {
        return true;
    }
    _center = Center::Plot;
    QPointer<ValueTableModel> table(_table_model.get());
    table->clear();
    return table && selection == _selections;
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
    const bool windowActive = _active && _active->request.kind == Request::Kind::Window;
    QPointer<SignalPlotModel> model(_plot_model.get());

    if (_pending && _pending->kind == Request::Kind::Window) {
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
    _pending = Request{Request::Kind::Window, key, set, *wanted, 0};
    startPending();
    if (model) {
        updateBusy();
    }
}

// The table wants another page, sample or slice: install a cached one, keep the
// read in flight for it, or read it. A read of anything else is obsolete.
void Mdf4DocumentSession::onTableWanted() {
    if (_center != Center::Table) {
        return;
    }
    Request wanted =
        tableRequest(*_selected, _table_model->wantedSample(), _table_model->wantedSlice());
    QPointer<ValueTableModel> table(_table_model.get());
    if (Retained* cached = retainedTable(wanted)) {
        cached->lastUse = ++_clock;
        const TablePagePtr page = cached->page;
        const ArraySlicePtr slice = cached->slice;
        _pending.reset();
        cancelActive();
        if (wanted.kind == Request::Kind::ArraySample) {
            table->setArraySlice(slice);
        } else {
            table->setPage(page);
        }
        if (table) {
            updateBusy();
        }
        return;
    }
    if (_active && !_active->cancel->load() && _active->request.sameTableRead(wanted)) {
        _pending.reset();
        updateBusy();
        return;
    }
    cancelActive();
    _pending = std::move(wanted);
    startPending();
    if (table) {
        updateBusy();
    }
}

// Takes the completion and settles before notifying.
void Mdf4DocumentSession::onScanFinished() {
    QPointer<SignalPlotModel> model(_plot_model.get());
    if (_active->cancel->load()) {
        // Cancelled work keeps nothing, even when its read finished first.
        // Taking the completion releases its result while the reservation still
        // covers it; the request goes with the reservation, before anything
        // else is admitted or announced.
        _watcher.future().takeResult();
        _active.reset();
    } else {
        // The reservation becomes the result's own charge in one step. The
        // work is for the selection, view and page as they stand.
        Completion completion = _watcher.future().takeResult();
        const Request request = std::move(_active->request);
        switch (request.kind) {
        case Request::Kind::Overview:
            if (completion.overview) {
                retain({request.kind, request.channel, completion.overview, {}, {}, {}, 0, {},
                        completion.overview->bytes(), ++_clock});
            }
            break;
        case Request::Kind::Window:
            if (completion.window) {
                retain({request.kind, request.channel, request.set, completion.window, {}, {}, 0,
                        {}, completion.window->bytes(), ++_clock});
            }
            break;
        case Request::Kind::Page:
            if (completion.page) {
                retain({request.kind, request.channel, {}, {}, completion.page, {}, request.first,
                        {}, completion.page->bytes(), ++_clock});
            }
            break;
        case Request::Kind::ArraySample:
            if (completion.slice) {
                retain({request.kind, request.channel, {}, {}, {}, completion.slice, request.first,
                        request.plan.window, completion.slice->bytes(), ++_clock});
            }
            break;
        }
        _active.reset();

        switch (request.kind) {
        case Request::Kind::Overview:
            if (completion.overview) {
                model->setSignal(header(request.channel), completion.overview);
            } else {
                const auto [note, text] =
                    noteFor(completion.scan, completion.build, QStringLiteral("Samples"));
                model->setSignal(header(request.channel), {}, note, text);
            }
            break;
        case Request::Kind::Window:
            if (completion.window) {
                model->setWindow(completion.window);
            } else {
                const auto [note, text] =
                    noteFor(completion.scan, completion.build, QStringLiteral("Exact samples"));
                model->setWindow({}, note, text);
            }
            break;
        case Request::Kind::Page:
            if (completion.page) {
                _table_model->setPage(completion.page);
            } else {
                const auto [note, text] = noteFor(completion.scan, completion.build,
                                                  QString::fromLatin1(request.page.values));
                _table_model->setPage({}, tableNote(note), text);
            }
            break;
        case Request::Kind::ArraySample:
            if (completion.slice) {
                _table_model->setArraySlice(completion.slice);
            } else if (completion.scan.outcome.status == mdf4::Status::Ok) {
                _table_model->setArraySlice({}, TableNote::Empty, pastEndText(request.first));
            } else {
                const auto [note, text] =
                    noteFor(completion.scan, completion.build, QStringLiteral("Values"));
                _table_model->setArraySlice({}, tableNote(note), text);
            }
            break;
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

// Progress of the read in flight, already coalesced on the worker. Progress of
// cancelled work, or of another channel's, never reaches the plot or the table.
void Mdf4DocumentSession::onProgress(int permille) {
    if (!_active || _active->cancel->load() || _selected != _active->request.channel) {
        return;
    }
    _active->progress = permille / 1000.0;
    const Request::Kind kind = _active->request.kind;
    if (kind == Request::Kind::Page || kind == Request::Kind::ArraySample) {
        _table_model->setProgress(_active->progress);
    } else {
        _plot_model->setProgress(_active->progress);
    }
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
    std::uint64_t bytes = 0;
    QString what;
    switch (request.kind) {
    case Request::Kind::Overview:
        bytes = PlotOverviewBuilder::reservation(statedCount(request.channel));
        what = QStringLiteral("Samples");
        break;
    case Request::Kind::Window:
        bytes = PlotWindowBuilder::reservation(request.window);
        what = QStringLiteral("Exact samples");
        break;
    case Request::Kind::Page:
        bytes = mdf4page::reservation(request.page);
        what = QString::fromLatin1(request.page.values);
        break;
    case Request::Kind::ArraySample:
        bytes = mdf4array::reservation(request.plan);
        what = QStringLiteral("Values");
        break;
    }
    if (admit(bytes)) {
        launch(std::move(request), bytes);
        return;
    }

    // Results in use leave no room: say so where the result would have shown.
    const QString text =
        QStringLiteral("%1 need %2 of result memory, but results in use hold %3 of this "
                       "file's %4")
            .arg(what, sizeText(bytes), sizeText(heldBytes()), sizeText(_limits.resultBytes));
    switch (request.kind) {
    case Request::Kind::Overview:
        _plot_model->setSignal(header(request.channel), {}, PlotNote::Refused, text);
        break;
    case Request::Kind::Window:
        _plot_model->setWindow({}, PlotNote::Refused, text);
        break;
    case Request::Kind::Page:
        _table_model->setPage({}, TableNote::Refused, text);
        break;
    case Request::Kind::ArraySample:
        _table_model->setArraySlice({}, TableNote::Refused, text);
        break;
    }
}

// The task owns copies of the source functions it reads with, and with them the
// source, plus plain request values; an array sample's task also owns the
// metadata its plan refers into. It never reaches this session, its models or
// its results. It builds its result from the read's chunks and hands it over
// only after an Ok outcome, when the reader has checked the source after the
// traversal.
void Mdf4DocumentSession::launch(Request request, std::uint64_t reserved) {
    auto cancel = std::make_shared<std::atomic<bool>>(false);
    const Request::Kind kind = request.kind;
    const ChannelKey channel = request.channel;
    const PlotWindowRequest cover = request.window;
    const std::uint64_t stated = statedCount(channel);
    mdf4array::ArrayPlan plan = request.plan;
    mdf4page::PagePlan page = request.page;
    _active = Active{std::move(request), cancel, reserved, -1.0};

    if (kind == Request::Kind::ArraySample) {
        // The sample's elements, then its axes' labels: every read of the
        // sample runs in this one task, so none overlaps another operation.
        // `metadata` keeps alive the fixed axis points the plan refers to.
        _watcher.setFuture(QtConcurrent::run(
            [readArray = _source.readArray, scan = _source.scan, metadata = _metadata,
             plan = std::move(plan), cancel](QPromise<Completion>& promise) {
                const mdf4::Control control = readControl(promise, cancel.get());
                mdf4array::ArraySample sample =
                    mdf4array::readArraySample(plan, readArray, scan, control);
                Completion completion;
                completion.scan.outcome = sample.outcome;
                completion.scan.samplesRead = sample.samplesRead;
                if (sample.outcome.status == mdf4::Status::Ok && sample.samplesRead > 0) {
                    completion.slice = sliceOf(std::move(sample), plan.sample);
                }
                promise.addResult(std::move(completion));
            }));
        return;
    }

    if (kind == Request::Kind::Page) {
        // The page's own lane, then each member's, in this one task, each read
        // of the page's rows, as many as the builder holds: the reader delivers
        // no more, whatever it clamps the request to.
        _watcher.setFuture(QtConcurrent::run(
            [scan = _source.scan, scanText = _source.scanText, scanBytes = _source.scanBytes,
             page = std::move(page), cancel](QPromise<Completion>& promise) {
                const mdf4::Control control = readControl(promise, cancel.get());
                mdf4page::PageResult result =
                    mdf4page::readPage(page, scan, scanText, scanBytes, control);
                Completion completion;
                completion.scan = std::move(result.scan);
                completion.page = std::move(result.page);
                promise.addResult(std::move(completion));
            }));
        return;
    }

    const bool window = kind == Request::Kind::Window;
    _watcher.setFuture(QtConcurrent::run(
        [scan = _source.scan, channel, window, cover, stated, cancel](QPromise<Completion>& promise) {
            const mdf4::Control control = readControl(promise, cancel.get());
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

// The center's model is busy while the selected channel has a read pending or
// in flight that was not cancelled; progress is that read's. The other model is
// idle. An observer of a notification may select or view anew and start a read,
// so the state is read again after each one.
void Mdf4DocumentSession::updateBusy() {
    const auto running = [this] {
        return _active && !_active->cancel->load() && _selected == _active->request.channel;
    };
    const auto busy = [&] { return running() || (_pending && _selected == _pending->channel); };
    const auto table = [this] { return _center == Center::Table; };
    QPointer<SignalPlotModel> model(_plot_model.get());
    model->setProgress(!table() && running() ? _active->progress : -1.0);
    if (!model) {
        return;
    }
    model->setBusy(!table() && busy());
    if (!model) {
        return;
    }
    _table_model->setProgress(table() && running() ? _active->progress : -1.0);
    if (!model) {
        return;
    }
    _table_model->setBusy(table() && busy());
}

bool Mdf4DocumentSession::admit(std::uint64_t bytes) {
    // More than the whole allowance is refused outright: nothing to evict for it,
    // and a saturated reservation must not wrap the sum.
    if (bytes > _limits.resultBytes) {
        return false;
    }
    while (_retained_bytes + bytes > _limits.resultBytes) {
        auto victim = _retained.end();
        for (auto it = _retained.begin(); it != _retained.end(); ++it) {
            if (!it->held() && (victim == _retained.end() || it->lastUse < victim->lastUse)) {
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
        bytes += entry.held() ? entry.bytes : 0;
    }
    return bytes;
}

bool Mdf4DocumentSession::Retained::held() const {
    switch (kind) {
    case Request::Kind::Overview:
        return overview.use_count() > 1;
    case Request::Kind::Window:
        return window.use_count() > 1;
    case Request::Kind::Page:
        return page.use_count() > 1;
    case Request::Kind::ArraySample:
        return slice.use_count() > 1;
    }
    return false;
}

Mdf4DocumentSession::Retained* Mdf4DocumentSession::retainedOverview(ChannelKey key) {
    for (Retained& entry : _retained) {
        if (entry.kind == Request::Kind::Overview && entry.channel == key) {
            return &entry;
        }
    }
    return nullptr;
}

Mdf4DocumentSession::Retained* Mdf4DocumentSession::retainedWindow(ChannelKey key,
                                                                    const PlotOverview* set,
                                                                    double start, double end) {
    for (Retained& entry : _retained) {
        if (entry.kind == Request::Kind::Window && entry.channel == key &&
            entry.overview.get() == set && entry.window->covers(start, end)) {
            return &entry;
        }
    }
    return nullptr;
}

// A page by its channel and first sample; an array slice by its channel,
// sample and element window. A page read has no element window: both sides
// carry the default.
Mdf4DocumentSession::Retained* Mdf4DocumentSession::retainedTable(const Request& wanted) {
    for (Retained& entry : _retained) {
        if (entry.kind == wanted.kind && entry.channel == wanted.channel &&
            entry.first == wanted.first && entry.elements.first == wanted.plan.window.first &&
            entry.elements.count == wanted.plan.window.count) {
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
            // The name or "Channel <n>", then the unit and an axis, text or
            // unplottable role, a Byte channel's or a structure's ("Structure
            // of <count> members", the count within the opening allowance's
            // channels), or an array's: 28 for the separator and type, 24 for
            // each printed size with its " × ", and 24 for " … (<count>
            // dimensions)", whose count may take ten digits because the first
            // size has no " × ".
            std::uint64_t role = 0;
            if (channel.has_array()) {
                const int dimensions = channel.array().dimensions_size();
                role = 28 + 24 * static_cast<std::uint64_t>(std::min(dimensions, kArrayDimensionsShown)) +
                       (dimensions > kArrayDimensionsShown ? 24 : 0);
            } else if (channel.binary()) {
                role = 36;
            } else if (isAxis(channel) || channel.textual() || !channel.decodable()) {
                role = 20;
            }
            textBytes += (channel.name().empty() ? 18 : channel.name().size()) +
                         channel.unit().size() + role;
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

        // A structure's member follows its parent in the walk and is listed
        // under it, by the rest of its name; each channel's row and its count
        // of direct members, by channel index.
        std::vector<TreeItem*> items(static_cast<std::size_t>(group.channels_size()), nullptr);
        std::vector<std::uint64_t> members(items.size(), 0);
        for (const mdf4::Channel& channel : group.channels()) {
            if (channel.has_member_of()) {
                ++members[channel.member_of()];
            }
        }
        for (int channelIndex = 0; channelIndex < group.channels_size(); ++channelIndex) {
            const auto& channel = group.channels(channelIndex);
            SemanticKind semanticKind = SemanticKind::Entity;
            if (isAxis(channel)) {
                semanticKind = SemanticKind::Attribute;
            } else if (!channel.decodable()) {
                semanticKind = SemanticKind::Diagnostic;
            }
            const auto index = static_cast<std::size_t>(channelIndex);
            items[index] = appendEntity(
                channel.has_member_of() ? items[channel.member_of()] : groupItem,
                text(mdf4page::channelTitle(group, channelIndex)),
                channelSubtitle(channel, members[index]),
                channel.decodable() ? QStringLiteral("channel")
                                    : QStringLiteral("channel-unsupported"),
                semanticKind,
                Mdf4Path{Mdf4EntityKind::Channel, groupIndex, channelIndex});
        }
    }

    setRootItem(std::move(root));
}
