#include "sessions/mdf4documentsession.h"

#include "mdf4/extract.h"
#include "models/signalplotmodel.h"
#include "sessions/mdf4detailpresenter.h"
#include "sessions/presentertext.h"

#include <QFutureWatcher>
#include <QObject>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <cstddef>
#include <utility>

namespace {

PlotSeries decodeSeries(const QString& path,
                        std::uint32_t group,
                        std::uint32_t channel,
                        std::uint64_t firstSample,
                        std::uint64_t sampleCount) {
    mdf4::Series decoded = mdf4::extract::decodeChannel(
        path.toStdString(), group, channel, firstSample, sampleCount);

    PlotSeries series;
    series.time = std::move(decoded.time);
    series.value = std::move(decoded.value);
    return series;
}

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

// decodeChannel is a parsed-file boundary, and the plot binary-searches the
// domain it hands over. Trim the parallel arrays to a common length and, when
// the domain is not usable as a search key, plot the channel against record
// indices rather than silently mislocating its samples.
void normalizeDecoded(PlotSeries& series) {
    const std::size_t count = std::min(series.time.size(), series.value.size());
    series.time.resize(count);
    series.value.resize(count);
    if (nonDecreasing(series.time)) {
        return;
    }

    for (std::size_t i = 0; i < count; ++i) {
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

    // decodeChannel deliberately falls back to record indices when no time
    // master can be resolved. Keep that useful degradation visible instead of
    // presenting an index/angle/distance domain as seconds.
    return {QStringLiteral("Record index"), {}};
}

} // namespace

Mdf4DocumentSession::Mdf4DocumentSession(QString displayName,
                                         QString sourcePath,
                                         mdf4::File document,
                                         QList<DiagnosticMessage> diagnostics,
                                         DecodeFunction decode)
    : AdapterSessionBase(FormatId::MDF4,
                         QStringLiteral("MDF4"),
                         std::move(displayName),
                         std::move(sourcePath),
                         std::move(diagnostics)),
      _document(std::move(document)),
      _plot_model(std::make_unique<SignalPlotModel>()),
      _decode(decode ? std::move(decode) : DecodeFunction{decodeSeries}) {
    setDetailPresenter(std::make_unique<Mdf4DetailPresenter>(_document));
    buildTree();
}

Mdf4DocumentSession::~Mdf4DocumentSession() = default;

QUrl Mdf4DocumentSession::centerPanelSource() const {
    return QUrl(QStringLiteral("qrc:/qt/qml/ExplorerApp/qml/components/SignalPlotView.qml"));
}

QAbstractListModel* Mdf4DocumentSession::centerPanelModel() {
    return _plot_model.get();
}

void Mdf4DocumentSession::selectNode(quint64 key) {
    AdapterSessionBase::selectNode(key);

    const NodeBinding* binding = _registry.resolve(NodeRef{FormatId::MDF4, key});
    if (!binding || !std::holds_alternative<Mdf4Path>(binding->payload)) {
        clearPlot();
        return;
    }

    const Mdf4Path path = std::get<Mdf4Path>(binding->payload);
    if (path.kind != Mdf4EntityKind::Channel) {
        clearPlot();
        return;
    }

    selectChannel(path);
}

void Mdf4DocumentSession::moveModelsToThread(QThread* thread) {
    AdapterSessionBase::moveModelsToThread(thread);
    _plot_model->moveToThread(thread);
}

void Mdf4DocumentSession::buildTree() {
    auto root = std::make_unique<TreeItem>();
    root->title = displayName();
    root->semanticKind = SemanticKind::Root;

    TreeItem* file = appendNode(
        root.get(),
        displayName(),
        _document.version().empty() ? QStringLiteral("MDF4")
                                    : QStringLiteral("MDF %1").arg(text(_document.version())),
        QStringLiteral("file"),
        SemanticKind::Root,
        NodeBinding{SemanticKind::Root, Mdf4Path{Mdf4EntityKind::File, -1, -1}, true});

    for (int groupIndex = 0; groupIndex < _document.groups_size(); ++groupIndex) {
        const auto& group = _document.groups(groupIndex);
        TreeItem* groupItem = appendNode(
            file,
            groupTitle(group, groupIndex),
            QStringLiteral("%1 samples").arg(group.cycle_count()),
            QStringLiteral("channel-group"),
            SemanticKind::Section,
            NodeBinding{SemanticKind::Section,
                        Mdf4Path{Mdf4EntityKind::ChannelGroup, groupIndex, -1},
                        true});

        for (int channelIndex = 0; channelIndex < group.channels_size(); ++channelIndex) {
            const auto& channel = group.channels(channelIndex);
            SemanticKind semanticKind = SemanticKind::Entity;
            if (channel.is_master()) {
                semanticKind = SemanticKind::Attribute;
            } else if (!channel.decodable()) {
                semanticKind = SemanticKind::Diagnostic;
            }
            appendNode(
                groupItem,
                channelTitle(channel, channelIndex),
                channelSubtitle(channel),
                channel.decodable() ? QStringLiteral("channel")
                                    : QStringLiteral("channel-unsupported"),
                semanticKind,
                NodeBinding{semanticKind,
                            Mdf4Path{Mdf4EntityKind::Channel, groupIndex, channelIndex},
                            true});
        }
    }

    setRootItem(std::move(root));
}

void Mdf4DocumentSession::clearPlot() {
    _selected_channel.reset();
    _plot_model->setBusy(false);
    _plot_model->setSeries({});
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
    if (path.groupIndex < 0 || path.groupIndex >= _document.groups_size()) {
        clearPlot();
        return;
    }

    const auto& group = _document.groups(path.groupIndex);
    if (path.channelIndex < 0 || path.channelIndex >= group.channels_size()) {
        clearPlot();
        return;
    }

    const auto& channel = group.channels(path.channelIndex);
    const DomainMetadata domain = domainMetadata(_document, path.groupIndex);
    PlotSeries pending;
    pending.name = channelTitle(channel, path.channelIndex);
    pending.unit = text(channel.unit());
    pending.domainName = domain.name;
    pending.domainUnit = domain.unit;

    // A master is the group's own domain, not a signal against it.
    if (channel.is_master() || !channel.decodable()) {
        _selected_channel.reset();
        _plot_model->setBusy(false);
        _plot_model->setSeries(std::make_shared<const PlotSeries>(std::move(pending)));
        return;
    }

    const ChannelKey key{
        static_cast<std::uint32_t>(path.groupIndex),
        static_cast<std::uint32_t>(path.channelIndex),
    };
    _selected_channel = key;

    if (PlotSeriesPtr cached = cachedSeries(key)) {
        _plot_model->setBusy(false);
        _plot_model->setSeries(std::move(cached));
        return;
    }

    const std::uint64_t sampleCount = channel.sample_count();
    if (sampleCount == 0) {
        _plot_model->setBusy(false);
        _plot_model->setSeries(std::make_shared<const PlotSeries>(std::move(pending)));
        return;
    }

    _plot_model->setSeries(std::make_shared<const PlotSeries>(pending));
    _plot_model->setBusy(true);

    // Re-selecting a channel whose decode is still running waits for that one
    // rather than starting a duplicate.
    if (!_decodes_in_flight.insert(key).second) {
        return;
    }

    const QString pathText = sourcePath();
    const DecodeFunction decode = _decode;
    auto* watcher = new QFutureWatcher<PlotSeries>(_plot_model.get());
    QObject::connect(watcher, &QFutureWatcher<PlotSeries>::finished, _plot_model.get(),
                     [this, watcher, key, header = std::move(pending)]() mutable {
        PlotSeries decoded = watcher->future().takeResult();
        watcher->deleteLater();
        _decodes_in_flight.erase(key);

        PlotSeries series = std::move(header);
        series.time = std::move(decoded.time);
        series.value = std::move(decoded.value);
        normalizeDecoded(series);

        // The samples are valid for their channel whatever is selected now, so
        // the work is always kept; only the plot update follows the selection.
        PlotSeriesPtr stored = std::make_shared<const PlotSeries>(std::move(series));
        cacheSeries(key, stored);
        if (_selected_channel == key) {
            _plot_model->setSeries(std::move(stored));
            _plot_model->setBusy(false);
        }
    });

    watcher->setFuture(QtConcurrent::run(
        [decode, pathText, key, sampleCount]() {
            return decode(pathText, key.first, key.second, 0, sampleCount);
        }));
}
