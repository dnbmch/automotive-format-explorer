#include "sessions/mdf4documentsession.h"

#include "models/signalplotmodel.h"
#include "sessions/presentertext.h"

#include <QObject>
#include <QPointer>
#include <QtConcurrent/QtConcurrentRun>

#include <cstddef>
#include <utility>

namespace {

const QString kNoSamples = QStringLiteral("No samples recorded");

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

// Rejects NaN as well as an out-of-order sample: both break a binary search.
bool nonDecreasing(const std::vector<double>& domain) {
    for (std::size_t i = 1; i < domain.size(); ++i) {
        if (!(domain[i] >= domain[i - 1])) {
            return false;
        }
    }
    return true;
}

// A successful read has equal-length arrays, but the plot binary-searches its
// domain. When the domain is not usable as a search key, plot the channel
// against record indices rather than silently mislocating its samples.
void normalizeDomain(PlotSeries& series) {
    if (nonDecreasing(series.time)) {
        return;
    }

    for (std::size_t i = 0; i < series.time.size(); ++i) {
        series.time[i] = static_cast<double>(i);
    }
    series.domainName = QStringLiteral("Record index");
    series.domainUnit.clear();
}

struct DomainMetadata {
    QString name;
    QString unit;
};

DomainMetadata domainMetadata(const mdf4::File& document, int groupIndex) {
    if (groupIndex < 0 || groupIndex >= document.groups_size()) {
        return {QStringLiteral("Record index"), {}};
    }

    const mdf4::ChannelGroup* owner = &document.groups(groupIndex);
    if (owner->remote_master() && owner->master_resolved() &&
        owner->master_group() < static_cast<std::uint32_t>(document.groups_size())) {
        owner = &document.groups(static_cast<int>(owner->master_group()));
    }

    for (const mdf4::Channel& channel : owner->channels()) {
        if (channel.is_master() && channel.sync_type() == 1) {
            return {
                channel.name().empty() ? QStringLiteral("Time") : text(channel.name()),
                text(channel.unit()),
            };
        }
    }

    // The reader deliberately falls back to record indices when no time
    // master can be resolved. Keep that useful degradation visible instead of
    // presenting an index/angle/distance domain as seconds.
    return {QStringLiteral("Record index"), {}};
}

// A failed read is shown with its reason, never cached as an empty channel.
QString failureText(const mdf4::ReadResult& result) {
    const QString reason = QString::fromUtf8(result.message);
    return result.location.empty()
        ? QStringLiteral("Samples could not be read: %1").arg(reason)
        : QStringLiteral("Samples could not be read: %1 (%2)")
              .arg(reason, QString::fromStdString(result.location));
}

} // namespace

Mdf4DocumentSession::Mdf4DocumentSession(QString displayName,
                                         QString sourcePath,
                                         std::shared_ptr<const mdf4::File> metadata,
                                         ReadFunction read,
                                         QList<DiagnosticMessage> diagnostics)
    : AdapterSessionBase(FormatId::MDF4,
                         QStringLiteral("MDF4"),
                         std::move(displayName),
                         std::move(sourcePath),
                         std::move(diagnostics)),
      _metadata(std::move(metadata)),
      _presenter(*_metadata),
      _read(std::move(read)),
      _plot_model(std::make_unique<SignalPlotModel>()) {
    QObject::connect(&_read_watcher, &QFutureWatcher<mdf4::ReadResult>::finished,
                     &_read_watcher, [this] { onReadFinished(); });
    buildTree();
}

Mdf4DocumentSession::~Mdf4DocumentSession() {
    // The task holds its own reference to the source and never touches this
    // session, so it may still release that reference on its worker after the
    // wait below.
    QObject::disconnect(&_read_watcher, nullptr, nullptr, nullptr);
    _pending_read.reset();
    if (_active_read) {
        QFuture<mdf4::ReadResult> active = _read_watcher.future();
        active.waitForFinished();
        const mdf4::ReadResult discarded = active.takeResult();
    }
}

QUrl Mdf4DocumentSession::centerPanelSource() const {
    return QUrl(QStringLiteral("qrc:/qt/qml/ExplorerApp/qml/components/SignalPlotView.qml"));
}

QAbstractListModel* Mdf4DocumentSession::centerPanelModel() {
    return _plot_model.get();
}

void Mdf4DocumentSession::selectNode(quint64 key) {
    const auto it = _paths.find(key);
    if (it == _paths.end()) {
        _detail_model.setSelection({}, {});
        clearPlot();
        return;
    }

    const Mdf4Path path = it->second;
    _detail_model.setSelection(_presenter.buildDetails(path),
                               [this, path] { return _presenter.buildRawJson(path); });
    if (path.kind != Mdf4EntityKind::Channel) {
        clearPlot();
        return;
    }

    selectChannel(path);
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
    _read_watcher.moveToThread(thread);
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

// Plot model observers run synchronously and may select another node or close
// this session. Every flow therefore settles its state first and notifies last:
// the series, then a busy state derived from the state as it is by then.
void Mdf4DocumentSession::show(PlotSeriesPtr series) {
    QPointer<SignalPlotModel> model(_plot_model.get());
    model->setSeries(std::move(series));
    if (model) {
        model->setBusy(reading());
    }
}

bool Mdf4DocumentSession::reading() const {
    return _selected_channel &&
           (_selected_channel == _active_read || _selected_channel == _pending_read);
}

void Mdf4DocumentSession::clearPlot() {
    _selected_channel.reset();
    _pending_read.reset();
    show(nullptr);
}

PlotSeries Mdf4DocumentSession::seriesHeader(ChannelKey key) const {
    const int groupIndex = static_cast<int>(key.first);
    const int channelIndex = static_cast<int>(key.second);
    const mdf4::Channel& channel = _metadata->groups(groupIndex).channels(channelIndex);
    const DomainMetadata domain = domainMetadata(*_metadata, groupIndex);
    PlotSeries header;
    header.name = channelTitle(channel, channelIndex);
    header.unit = text(channel.unit());
    header.domainName = domain.name;
    header.domainUnit = domain.unit;
    return header;
}

PlotSeriesPtr Mdf4DocumentSession::cachedSeries(ChannelKey key) {
    const auto entry = _decode_cache.find(key);
    if (entry == _decode_cache.end()) {
        return {};
    }
    entry->second.lastUse = ++_cache_clock;
    return entry->second.series;
}

void Mdf4DocumentSession::cacheSeries(ChannelKey key, PlotSeriesPtr series) {
    const std::uint64_t bytes =
        (series->time.size() + series->value.size()) * sizeof(double);
    CacheEntry& entry = _decode_cache[key];
    _cache_bytes -= entry.bytes;
    entry = CacheEntry{std::move(series), bytes, ++_cache_clock};
    _cache_bytes += bytes;

    // Evict least-recently-used channels until the budget holds. The channel on
    // screen and the entry just stored are never the victim, so a single series
    // larger than the whole budget stays cached alone and still plots.
    while (_cache_bytes > kDecodeCacheBudget) {
        auto victim = _decode_cache.end();
        for (auto it = _decode_cache.begin(); it != _decode_cache.end(); ++it) {
            if (it->first == key || _selected_channel == it->first) {
                continue;
            }
            if (victim == _decode_cache.end() ||
                it->second.lastUse < victim->second.lastUse) {
                victim = it;
            }
        }
        if (victim == _decode_cache.end()) {
            return;
        }
        _cache_bytes -= victim->second.bytes;
        _decode_cache.erase(victim);
    }
}

void Mdf4DocumentSession::selectChannel(const Mdf4Path& path) {
    if (path.groupIndex < 0 || path.groupIndex >= _metadata->groups_size()) {
        clearPlot();
        return;
    }

    const auto& group = _metadata->groups(path.groupIndex);
    if (path.channelIndex < 0 || path.channelIndex >= group.channels_size()) {
        clearPlot();
        return;
    }

    const auto& channel = group.channels(path.channelIndex);
    const ChannelKey key{
        static_cast<std::uint32_t>(path.groupIndex),
        static_cast<std::uint32_t>(path.channelIndex),
    };
    PlotSeries header = seriesHeader(key);

    // A master is the group's own domain, not a signal against it.
    if (channel.is_master() || !channel.decodable()) {
        header.placeholderText = channel.is_master()
            ? QStringLiteral("Master channel — this group's time axis")
            : QStringLiteral("This channel type is not plottable");
        _selected_channel.reset();
        _pending_read.reset();
        show(std::make_shared<const PlotSeries>(std::move(header)));
        return;
    }

    _selected_channel = key;
    if (PlotSeriesPtr cached = cachedSeries(key)) {
        _pending_read.reset();
        show(std::move(cached));
        return;
    }

    if (channel.sample_count() == 0) {
        _pending_read.reset();
        header.placeholderText = kNoSamples;
        show(std::make_shared<const PlotSeries>(std::move(header)));
        return;
    }

    // One read at a time: the active one is reused, and a channel selected
    // while another reads waits as the single pending request.
    if (_active_read == key) {
        _pending_read.reset();
    } else if (_active_read) {
        _pending_read = key;
    } else {
        startRead(key);
    }
    show(std::make_shared<const PlotSeries>(std::move(header)));
}

// The task owns a copy of the read function, and with it the source, plus plain
// request values; it never reaches this session or its models.
void Mdf4DocumentSession::startRead(ChannelKey key) {
    const std::uint64_t sampleCount = _metadata->groups(static_cast<int>(key.first))
                                          .channels(static_cast<int>(key.second))
                                          .sample_count();
    _active_read = key;
    _read_watcher.setFuture(QtConcurrent::run([read = _read, key, sampleCount] {
        return read(key.first, key.second, 0, sampleCount);
    }));
}

void Mdf4DocumentSession::onReadFinished() {
    // Take the result and settle which read is active before notifying.
    mdf4::ReadResult result = _read_watcher.future().takeResult();
    const ChannelKey key = *_active_read;
    _active_read.reset();

    PlotSeries series = seriesHeader(key);
    if (result.ok) {
        series.time = std::move(result.series.time);
        series.value = std::move(result.series.value);
        normalizeDomain(series);
        if (series.time.empty()) {
            series.placeholderText = kNoSamples;
        }
    } else {
        series.placeholderText = failureText(result);
    }
    PlotSeriesPtr completed = std::make_shared<const PlotSeries>(std::move(series));

    // Samples are valid for their channel whatever is selected now; only the
    // plot follows the selection.
    if (result.ok) {
        cacheSeries(key, completed);
    }
    if (_pending_read) {
        const ChannelKey next = *_pending_read;
        _pending_read.reset();
        startRead(next);
    }
    if (_selected_channel == key) {
        show(std::move(completed));
    }
}
