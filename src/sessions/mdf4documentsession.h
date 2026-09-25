#pragma once

#include "models/plotseries.h"
#include "sessions/adaptersessionbase.h"

#pragma push_macro("signals")
#undef signals
#include "mdf4/reader.h"
#pragma pop_macro("signals")

#include <QFutureWatcher>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <utility>

class SignalPlotModel;

// One MDF4 document. The metadata and every channel read come from one opened
// source; the session runs at most one read at a time against it.
class Mdf4DocumentSession final : public AdapterSessionBase {
public:
    // One channel read against the session's source. It runs on a worker
    // thread, never concurrently with another read of the same session.
    using ReadFunction = std::function<mdf4::ReadResult(
        std::uint32_t group, std::uint32_t channel, std::uint64_t first, std::uint64_t count)>;

    // `metadata` and `read` describe the same opened source and each keeps it
    // alive; the adapter binds both to one mdf4::Reader.
    Mdf4DocumentSession(QString displayName,
                        QString sourcePath,
                        std::shared_ptr<const mdf4::File> metadata,
                        ReadFunction read,
                        QList<DiagnosticMessage> diagnostics = {});
    // Drops the pending selection and waits for an active read, which cannot
    // be interrupted, on this thread. No completion is delivered afterwards.
    ~Mdf4DocumentSession() override;

    QUrl centerPanelSource() const override;
    QAbstractListModel* centerPanelModel() override;
    void selectNode(quint64 key) override;
    // Also moves the watcher that receives read completions.
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
    void startRead(ChannelKey key);
    void onReadFinished();
    void show(PlotSeriesPtr series);
    bool reading() const;
    PlotSeries seriesHeader(ChannelKey key) const;
    PlotSeriesPtr cachedSeries(ChannelKey key);
    void cacheSeries(ChannelKey key, PlotSeriesPtr series);

    const std::shared_ptr<const mdf4::File> _metadata;
    const ReadFunction _read;
    std::unique_ptr<SignalPlotModel> _plot_model;
    std::map<ChannelKey, CacheEntry> _decode_cache;
    std::optional<ChannelKey> _selected_channel;
    // One read runs at a time; the latest selection needing another waits.
    std::optional<ChannelKey> _active_read;
    std::optional<ChannelKey> _pending_read;
    QFutureWatcher<mdf4::ReadResult> _read_watcher;
    std::uint64_t _cache_bytes = 0;
    std::uint64_t _cache_clock = 0;
};
