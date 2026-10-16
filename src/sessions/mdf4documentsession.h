#pragma once

#include "models/plotdata.h"
#include "models/tabledata.h"
#include "sessions/adaptersessionbase.h"
#include "sessions/mdf4arrayread.h"
#include "sessions/mdf4detailpresenter.h"
#include "sessions/mdf4pageread.h"

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
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

class SignalPlotModel;
class ValueTableModel;
struct PlotHeader;
struct TableHeader;

// Finite allowances of one MDF4 session, fixed at construction. Tests pass
// small values to reach a refusal without large inputs.
struct Mdf4SessionLimits {
    // The navigation tree with its key and path tables, as
    // Mdf4DocumentSession::treeBytes() estimates it before any row is built.
    std::uint64_t treeBytes = std::uint64_t{384} << 20;
    // Plot and table results: cached, shown and being built, together.
    std::uint64_t resultBytes = std::uint64_t{256} << 20;
};

// The operations of one opened source, in the reader's shapes. Each read runs
// on a worker thread, never concurrently with another read of the same
// session; the axis is the one the source resolved at opening. Every function
// keeps the source alive; Mdf4Adapter::load() binds all five to one
// mdf4::Reader.
struct Mdf4Source {
    using ScanFunction = std::function<mdf4::ScanResult(
        std::uint32_t group, std::uint32_t channel, std::uint64_t first, std::uint64_t count,
        const mdf4::Control& control, const mdf4::Visitor& visitor)>;
    using ScanTextFunction = std::function<mdf4::ScanResult(
        std::uint32_t group, std::uint32_t channel, std::uint64_t first, std::uint64_t count,
        const mdf4::Control& control, const mdf4::TextVisitor& visitor)>;
    using ReadArrayFunction = std::function<mdf4::ReadArrayResult(
        std::uint32_t group, std::uint32_t channel, std::uint64_t first, std::uint64_t count,
        const mdf4::ElementWindow& elements, const mdf4::Control& control)>;
    using AxisFunction = std::function<mdf4::Axis(std::uint32_t group)>;
    using ScanBytesFunction = std::function<mdf4::ScanResult(
        std::uint32_t group, std::uint32_t channel, std::uint64_t first, std::uint64_t count,
        const mdf4::Control& control, const mdf4::BytesVisitor& visitor)>;

    ScanFunction scan;
    ScanTextFunction scanText;
    ReadArrayFunction readArray;
    AxisFunction axis;
    ScanBytesFunction scanBytes;
};

// One MDF4 document. The metadata and every read come from one opened source;
// the session runs at most one read at a time against it. Its center view is
// the signal plot, or the value table while a decodable text, Byte or array
// channel is selected.
class Mdf4DocumentSession final : public AdapterSessionBase {
public:
    // What the session holds for the plot and the table: finished results,
    // whether cached or shown, and the reservation of the read in flight. Their
    // sum stays within Mdf4SessionLimits::resultBytes.
    struct ResultBytes {
        std::uint64_t retained = 0;
        std::uint64_t reserved = 0;
        std::size_t results = 0;
        std::size_t held = 0;  // results someone besides the cache still holds
    };

    // `metadata` and `source` describe the same opened source and each keeps
    // it alive; the adapter binds both to one mdf4::Reader.
    Mdf4DocumentSession(QString displayName,
                        QString sourcePath,
                        std::shared_ptr<const mdf4::File> metadata,
                        Mdf4Source source,
                        QList<DiagnosticMessage> diagnostics = {},
                        const Mdf4SessionLimits& limits = {});
    // Drops the pending read, cancels the one in flight and waits for it on this
    // thread. No completion or progress is delivered afterwards.
    ~Mdf4DocumentSession() override;

    // The view and model of the center: SignalPlotView.qml with the plot model,
    // or ValueTableView.qml with the table model.
    QUrl centerPanelSource() const override;
    QAbstractItemModel* centerPanelModel() override;
    void selectNode(quint64 key) override;
    // Also moves the watcher that receives read completions and progress.
    void moveModelsToThread(QThread* thread) override;

    ResultBytes resultBytes() const;

    // Upper bound of the tree's footprint for `document`: per row the measured
    // cost of its item, child slot, key and path table entries, plus its title and
    // subtitle text at two bytes per UTF-8 byte and allocator rounding.
    static std::uint64_t treeBytes(const mdf4::File& document);

private:
    using ChannelKey = std::pair<std::uint32_t, std::uint32_t>;

    enum class Center : std::uint8_t { Plot, Table };

    // A read the session wants: a channel's overview, an exact window of the
    // result set that overview began, which it holds, a page of a text or Byte
    // channel, or one sample of an array channel.
    struct Request {
        enum class Kind : std::uint8_t { Overview, Window, Page, ArraySample };
        Kind kind = Kind::Overview;
        ChannelKey channel;
        PlotOverviewPtr set;        // Window: the overview of its result set
        PlotWindowRequest window;   // Window
        std::uint64_t first = 0;    // Page: the page's first sample; ArraySample: the sample
        mdf4array::ArrayPlan plan;  // ArraySample: its slice, element window and axes
        mdf4page::PagePlan page;    // Page: its rows and lanes

        // Of a table read: the same page, or the same sample and element
        // window, of the same channel.
        bool sameTableRead(const Request& other) const {
            return kind == other.kind && channel == other.channel && first == other.first &&
                   plan.window.first == other.plan.window.first &&
                   plan.window.count == other.plan.window.count;
        }
    };

    // The read in flight: what it builds, the cancellation flag it shares with
    // its task and the bytes admitted for it before launch.
    struct Active {
        Request request;
        std::shared_ptr<std::atomic<bool>> cancel;
        std::uint64_t reserved = 0;
        double progress = -1.0;
    };

    // What a read returns to the GUI thread: the plot's result or the table's.
    // An array sample's outcome and samples read come in `scan`, a page's note
    // on its member columns in `note`.
    struct Completion {
        mdf4::ScanResult scan;
        PlotOverviewPtr overview;
        PlotWindowPtr window;
        PlotBuild build = PlotBuild::Continue;  // the window builder's final state
        TablePagePtr page;
        ArraySlicePtr slice;
        std::string note;
    };

    // A finished result, keyed by its request: an overview by its channel; a
    // window by its channel, result set (and with it the domain) and sample
    // range, holding its window and the overview of its result set; a page by
    // its channel and first sample; an array slice by its channel, sample and
    // element window.
    struct Retained {
        Request::Kind kind = Request::Kind::Overview;
        ChannelKey channel;
        PlotOverviewPtr overview;
        PlotWindowPtr window;
        TablePagePtr page;
        ArraySlicePtr slice;
        std::uint64_t first = 0;       // Page: the first sample; ArraySample: the sample
        mdf4::ElementWindow elements;  // ArraySample
        std::uint64_t bytes = 0;
        std::uint64_t lastUse = 0;

        // Held by someone besides the cache: shown by the plot or the table,
        // pending or in flight as the result set of a window, or the result set
        // of a cached window.
        bool held() const;
    };

    void buildTree();
    // Appends a row that shows the entity at `path`.
    TreeItem* appendEntity(TreeItem* parent, const QString& title, const QString& subtitle,
                           const QString& iconKey, SemanticKind semanticKind, Mdf4Path path);
    PlotHeader header(ChannelKey key) const;
    TableHeader tableHeader(ChannelKey key) const;
    std::uint64_t statedCount(ChannelKey key) const;
    // The table read of `key` showing `sample`: the page holding it (text or
    // Byte channel), or that sample's `slice` (array), planned from the
    // metadata.
    Request tableRequest(ChannelKey key, std::uint64_t sample,
                         const std::vector<std::uint64_t>& slice) const;

    // Continue the selection flow `selection` after its detail notification.
    void selectChannel(const Mdf4Path& path, std::uint64_t selection);
    void selectTable(ChannelKey key, std::uint64_t selection);
    // Turns the center to the plot; the table lets go of what it showed. False
    // when an observer of that destroyed the session or selected anew.
    bool showPlot(std::uint64_t selection);
    void onDetailWanted();
    void onTableWanted();
    void onScanFinished();
    void onProgress(int permille);

    // Scheduling. The active read is cancelled when a new selection, view,
    // page or sample makes it obsolete; its completion is consumed before the
    // pending one starts.
    void cancelActive();
    // Starts the pending read when none runs and its bytes fit; otherwise
    // presents the refusal. Notifies.
    void startPending();
    void launch(Request request, std::uint64_t reserved);
    void updateBusy();

    // Storage. admit() refuses more than the whole allowance, else evicts least
    // recently used results no one else holds until `bytes` more fit it.
    bool admit(std::uint64_t bytes);
    std::uint64_t heldBytes() const;
    Retained* retainedOverview(ChannelKey key);
    Retained* retainedWindow(ChannelKey key, const PlotOverview* set, double start, double end);
    Retained* retainedTable(const Request& wanted);
    void retain(Retained entry);

    const std::shared_ptr<const mdf4::File> _metadata;
    const Mdf4SessionLimits _limits;
    const Mdf4DetailPresenter _presenter;
    // The entity each row shows, by the row's key.
    std::unordered_map<quint64, Mdf4Path> _paths;
    const Mdf4Source _source;
    std::unique_ptr<SignalPlotModel> _plot_model;
    std::unique_ptr<ValueTableModel> _table_model;
    Center _center = Center::Plot;
    // Counts selectNode() calls; a selection flow that finds it moved after a
    // notification was superseded by a selection made from inside it.
    std::uint64_t _selections = 0;
    // The channel the plot or the table shows.
    std::optional<ChannelKey> _selected;
    std::optional<Active> _active;
    std::optional<Request> _pending;
    std::vector<Retained> _retained;
    std::uint64_t _retained_bytes = 0;
    std::uint64_t _clock = 0;
    QFutureWatcher<Completion> _watcher;
};
