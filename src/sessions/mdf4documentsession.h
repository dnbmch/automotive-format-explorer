#pragma once

#include "models/plotdata.h"
#include "sessions/adaptersessionbase.h"
#include "sessions/mdf4detailpresenter.h"

#pragma push_macro("signals")
#undef signals
#include "mdf4/reader.h"
#pragma pop_macro("signals")

#include <QFutureWatcher>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

class SignalPlotModel;
struct PlotHeader;

// Finite allowances of one MDF4 session, fixed at construction. Tests pass
// small values to reach a refusal without large inputs.
struct Mdf4SessionLimits {
    // The navigation tree with its key and path tables, as
    // Mdf4DocumentSession::treeBytes() estimates it before any row is built.
    std::uint64_t treeBytes = std::uint64_t{384} << 20;
    // Plot results: cached, shown and being built, together.
    std::uint64_t resultBytes = std::uint64_t{256} << 20;
};

// One MDF4 document. The metadata and every scan come from one opened source;
// the session runs at most one scan at a time against it.
class Mdf4DocumentSession final : public AdapterSessionBase {
public:
    // One scan of a channel against the session's source, on a worker thread,
    // never concurrently with another scan of the same session.
    using ScanFunction = std::function<mdf4::ScanResult(
        std::uint32_t group, std::uint32_t channel, std::uint64_t first, std::uint64_t count,
        const mdf4::Control& control, const mdf4::Visitor& visitor)>;
    // A group's time axis, as the source resolved it at opening.
    using AxisFunction = std::function<mdf4::Axis(std::uint32_t group)>;

    // What the session holds for plots: finished results, whether cached or
    // shown, and the reservation of the scan in flight. Their sum stays within
    // Mdf4SessionLimits::resultBytes.
    struct ResultBytes {
        std::uint64_t retained = 0;
        std::uint64_t reserved = 0;
        std::size_t results = 0;
        std::size_t held = 0;  // results someone besides the cache still holds
    };

    // `metadata`, `scan` and `axis` describe the same opened source and each
    // keeps it alive; the adapter binds all three to one mdf4::Reader.
    Mdf4DocumentSession(QString displayName,
                        QString sourcePath,
                        std::shared_ptr<const mdf4::File> metadata,
                        ScanFunction scan,
                        AxisFunction axis,
                        QList<DiagnosticMessage> diagnostics = {},
                        const Mdf4SessionLimits& limits = {});
    // Drops the pending scan, cancels the one in flight and waits for it on this
    // thread. No completion or progress is delivered afterwards.
    ~Mdf4DocumentSession() override;

    QUrl centerPanelSource() const override;
    QAbstractListModel* centerPanelModel() override;
    void selectNode(quint64 key) override;
    // Also moves the watcher that receives scan completions and progress.
    void moveModelsToThread(QThread* thread) override;

    ResultBytes resultBytes() const;

    // Upper bound of the tree's footprint for `document`: per row the measured
    // cost of its item, child slot, key and path table entries, plus its title and
    // subtitle text at two bytes per UTF-8 byte and allocator rounding.
    static std::uint64_t treeBytes(const mdf4::File& document);

private:
    using ChannelKey = std::pair<std::uint32_t, std::uint32_t>;

    // A scan the session wants: a channel's overview, or an exact window of the
    // result set that overview began. A window request holds its overview.
    struct Request {
        ChannelKey channel;
        PlotOverviewPtr set;       // null for an overview
        PlotWindowRequest window;  // window only
    };

    // The scan in flight: what it builds, the cancellation flag it shares with
    // its task and the bytes admitted for it before launch.
    struct Active {
        Request request;
        std::shared_ptr<std::atomic<bool>> cancel;
        std::uint64_t reserved = 0;
        double progress = -1.0;
    };

    // What a scan returns to the GUI thread.
    struct Completion {
        mdf4::ScanResult scan;
        PlotOverviewPtr overview;
        PlotWindowPtr window;
        PlotBuild build = PlotBuild::Continue;  // the window builder's final state
    };

    // A finished result. An overview entry holds its overview; a window entry
    // its window and the overview of its result set. Keyed by kind, channel,
    // result set (and with it the domain) and the window's sample range.
    struct Retained {
        ChannelKey channel;
        PlotOverviewPtr overview;
        PlotWindowPtr window;
        std::uint64_t bytes = 0;
        std::uint64_t lastUse = 0;
    };

    void buildTree();
    // Appends a row that shows the entity at `path`.
    TreeItem* appendEntity(TreeItem* parent, const QString& title, const QString& subtitle,
                           const QString& iconKey, SemanticKind semanticKind, Mdf4Path path);
    PlotHeader header(ChannelKey key) const;
    std::uint64_t statedCount(ChannelKey key) const;

    // Continues the selection flow `selection` after its detail notification.
    void selectChannel(const Mdf4Path& path, std::uint64_t selection);
    void onDetailWanted();
    void onScanFinished();
    void onProgress(int permille);

    // Scheduling. The active scan is cancelled when a new selection or view
    // makes it obsolete; its completion is consumed before the pending one
    // starts.
    void cancelActive();
    // Starts the pending scan when none runs and its bytes fit; otherwise
    // presents the refusal. Notifies.
    void startPending();
    void launch(Request request, std::uint64_t reserved);
    void updateBusy();

    // Storage. admit() evicts least recently used results no one else holds
    // until `bytes` more fit the allowance.
    bool admit(std::uint64_t bytes);
    std::uint64_t heldBytes() const;
    Retained* retainedOverview(ChannelKey key);
    Retained* retainedWindow(ChannelKey key, const PlotOverview* set, double start, double end);
    void retain(Retained entry);

    const std::shared_ptr<const mdf4::File> _metadata;
    const Mdf4SessionLimits _limits;
    const Mdf4DetailPresenter _presenter;
    // The entity each row shows, by the row's key.
    std::unordered_map<quint64, Mdf4Path> _paths;
    const ScanFunction _scan;
    const AxisFunction _axis;
    std::unique_ptr<SignalPlotModel> _plot_model;
    // Counts selectNode() calls; a selection flow that finds it moved after a
    // notification was superseded by a selection made from inside it.
    std::uint64_t _selections = 0;
    std::optional<ChannelKey> _selected;
    std::optional<Active> _active;
    std::optional<Request> _pending;
    std::vector<Retained> _retained;
    std::uint64_t _retained_bytes = 0;
    std::uint64_t _clock = 0;
    QFutureWatcher<Completion> _watcher;
};
