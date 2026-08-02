#include "sessions/mdf4documentsession.h"

#include "mdf4/extract.h"
#include "models/signalplotmodel.h"
#include "sessions/mdf4detailpresenter.h"
#include "sessions/presentertext.h"

#include <QFutureWatcher>
#include <QObject>
#include <QtConcurrent/QtConcurrentRun>

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

QString channelSubtitle(const mdf4::Channel& channel) {
    const QString unit = text(channel.unit());
    if (channel.decodable()) {
        return unit;
    }
    return unit.isEmpty() ? QStringLiteral("Not plottable")
                          : QStringLiteral("%1  ·  Not plottable").arg(unit);
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

    ++_selection_generation;
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
            const SemanticKind semanticKind = channel.decodable()
                ? SemanticKind::Entity
                : SemanticKind::Diagnostic;
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
    _plot_model->setBusy(false);
    _plot_model->setSeries({});
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

    if (!channel.decodable()) {
        _plot_model->setBusy(false);
        _plot_model->setSeries(std::move(pending));
        return;
    }

    const ChannelKey cacheKey{
        static_cast<std::uint32_t>(path.groupIndex),
        static_cast<std::uint32_t>(path.channelIndex),
    };
    const auto cached = _decode_cache.find(cacheKey);
    if (cached != _decode_cache.end()) {
        _plot_model->setBusy(false);
        _plot_model->setSeries(cached->second);
        return;
    }

    const std::uint64_t generation = _selection_generation;
    const std::uint64_t sampleCount = channel.sample_count();
    const QString name = pending.name;
    const QString unit = pending.unit;
    const QString domainName = pending.domainName;
    const QString domainUnit = pending.domainUnit;

    if (sampleCount == 0) {
        _decode_cache.emplace(cacheKey, pending);
        _plot_model->setSeries(std::move(pending));
        _plot_model->setBusy(false);
        return;
    }

    _plot_model->setSeries(std::move(pending));
    _plot_model->setBusy(true);
    const QString pathText = sourcePath();
    const DecodeFunction decode = _decode;
    auto* watcher = new QFutureWatcher<PlotSeries>(_plot_model.get());
    QObject::connect(watcher, &QFutureWatcher<PlotSeries>::finished, _plot_model.get(),
                     [this, watcher, cacheKey, generation, name, unit,
                      domainName, domainUnit]() {
        PlotSeries series = watcher->result();
        watcher->deleteLater();

        if (generation != _selection_generation) {
            return;
        }

        series.name = name;
        series.unit = unit;
        series.domainName = domainName;
        series.domainUnit = domainUnit;
        _decode_cache.emplace(cacheKey, series);
        _plot_model->setSeries(std::move(series));
        _plot_model->setBusy(false);
    });

    watcher->setFuture(QtConcurrent::run(
        [decode, pathText, cacheKey, sampleCount]() {
            return decode(pathText, cacheKey.first, cacheKey.second, 0, sampleCount);
        }));
}
