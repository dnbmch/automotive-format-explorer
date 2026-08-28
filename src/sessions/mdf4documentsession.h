#pragma once

#include "models/plotseries.h"
#include "sessions/adaptersessionbase.h"

#include "mdf4/mdf4.pb.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <utility>

class SignalPlotModel;

class Mdf4DocumentSession final : public AdapterSessionBase {
public:
    using DecodeFunction = std::function<PlotSeries(
        const QString&, std::uint32_t, std::uint32_t, std::uint64_t, std::uint64_t)>;

    Mdf4DocumentSession(QString displayName,
                        QString sourcePath,
                        mdf4::File document,
                        QList<DiagnosticMessage> diagnostics = {},
                        DecodeFunction decode = {});
    ~Mdf4DocumentSession() override;

    QUrl centerPanelSource() const override;
    QAbstractListModel* centerPanelModel() override;
    void selectNode(quint64 key) override;
    void moveModelsToThread(QThread* thread) override;

private:
    using ChannelKey = std::pair<std::uint32_t, std::uint32_t>;

    // Decoded samples dominate this session's footprint at 16 bytes per sample,
    // so the cache is bounded by bytes rather than entries.
    static constexpr std::uint64_t kDecodeCacheBudget = 256ull * 1024 * 1024;

    struct CacheEntry {
        PlotSeriesPtr series;
        std::uint64_t bytes = 0;
        std::uint64_t lastUse = 0;
    };

    void buildTree();
    void clearPlot();
    void selectChannel(const Mdf4Path& path);
    PlotSeriesPtr cachedSeries(ChannelKey key);
    void cacheSeries(ChannelKey key, PlotSeriesPtr series);

    mdf4::File _document;
    std::unique_ptr<SignalPlotModel> _plot_model;
    DecodeFunction _decode;
    std::map<ChannelKey, CacheEntry> _decode_cache;
    std::set<ChannelKey> _decodes_in_flight;
    std::optional<ChannelKey> _selected_channel;
    std::uint64_t _cache_bytes = 0;
    std::uint64_t _cache_clock = 0;
};
