#pragma once

#include "models/plotdata.h"

#include <QAbstractListModel>
#include <QString>
#include <QStringList>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

// What a producer says about its signal, whatever its samples.
struct PlotHeader {
    QString name;
    QString unit;
    QString domainName;  // of the Time domain; an Index result set shows sample indices
    QString domainUnit;
};

// Why the plot shows fewer samples than asked for, or none.
enum class PlotNote : std::uint8_t {
    None,
    Empty,    // nothing to plot: no samples, or a channel that is not a signal
    Failed,   // the samples could not be read
    Refused,  // a limit refused them
};

// One pixel column of the painted plot: the finite extrema it covers.
struct PlotColumn {
    double domain = 0.0;  // the column's center
    double minimum = 0.0;
    double maximum = 0.0;
};

// What the pointer is over: one exact sample, or the overview bins under it.
struct PlotCursor {
    bool visible = false;
    bool exact = false;
    std::uint64_t firstSample = 0;  // the sample's absolute index, or the first bin's
    std::uint64_t sampleCount = 0;
    std::uint64_t finiteCount = 0;
    double firstDomain = 0.0;  // the sample's coordinate, or the range of the bins
    double lastDomain = 0.0;
    double value = std::numeric_limits<double>::quiet_NaN();    // exact only
    double minimum = std::numeric_limits<double>::quiet_NaN();  // overview only
    double maximum = std::numeric_limits<double>::quiet_NaN();
};

// Format-neutral model of the painted plot. A producer installs a signal with
// its overview, then the exact windows the view asks for; bounds, zoom, cursor
// and paint columns belong here. Every GUI-thread operation is sized by the
// viewport or the overview's bins, never by the recording. The list interface
// carries no rows: views read the properties.
class SignalPlotModel : public QAbstractListModel {
    Q_OBJECT

    Q_PROPERTY(QString name READ name NOTIFY contentChanged)
    Q_PROPERTY(QString unit READ unit NOTIFY contentChanged)
    Q_PROPERTY(QString domainName READ domainName NOTIFY contentChanged)
    Q_PROPERTY(QString domainUnit READ domainUnit NOTIFY contentChanged)
    Q_PROPERTY(bool hasSamples READ hasSamples NOTIFY contentChanged)
    Q_PROPERTY(bool incomplete READ incomplete NOTIFY contentChanged)
    Q_PROPERTY(QString countText READ countText NOTIFY contentChanged)
    Q_PROPERTY(double fullStart READ fullStart NOTIFY contentChanged)
    Q_PROPERTY(double fullEnd READ fullEnd NOTIFY contentChanged)
    Q_PROPERTY(State plotState READ plotState NOTIFY stateChanged)
    Q_PROPERTY(QString message READ message NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(double progress READ progress NOTIFY busyChanged)
    Q_PROPERTY(double viewStart READ viewStart NOTIFY viewChanged)
    Q_PROPERTY(double viewEnd READ viewEnd NOTIFY viewChanged)
    Q_PROPERTY(double viewMinimum READ viewMinimum NOTIFY viewChanged)
    Q_PROPERTY(double viewMaximum READ viewMaximum NOTIFY viewChanged)
    Q_PROPERTY(bool cursorVisible READ cursorVisible NOTIFY cursorChanged)
    Q_PROPERTY(QString cursorText READ cursorText NOTIFY cursorChanged)

public:
    enum State {
        NoSignal,  // nothing selected
        Pending,   // a signal whose overview is being read
        Empty,     // a signal with nothing to plot; message() says why
        Failed,    // its samples could not be read
        Refused,   // a limit refused them
        Overview,  // each column spans the extrema of the samples it covers
        Detail,    // exact samples cover the whole view
    };
    Q_ENUM(State)

    explicit SignalPlotModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;

    QString name() const;
    QString unit() const;
    QString domainName() const;
    QString domainUnit() const;
    bool hasSamples() const;
    bool incomplete() const;
    QString countText() const;
    State plotState() const;
    QString message() const;
    bool busy() const;
    double progress() const;

    double fullStart() const;
    double fullEnd() const;
    double viewStart() const;
    double viewEnd() const;
    double viewMinimum() const;
    double viewMaximum() const;

    bool cursorVisible() const;
    const PlotCursor& cursor() const;
    // The cursor's report, one fact per line; cursorText joins them.
    QStringList cursorLines() const;
    QString cursorText() const;

    // Producer contract. Each call settles the model, then notifies.
    //
    // No signal: the empty plot.
    void clear();
    // A signal and its overview: null while it is read, or when there is none,
    // and then the note says why. Starts a new result set: drops the window and
    // the cursor and shows the whole overview.
    void setSignal(PlotHeader header, PlotOverviewPtr overview = {},
                   PlotNote note = PlotNote::None, QString text = {});
    // An exact window of the current overview that covers the view, or a note on
    // why the view has none. Bounds and zoom stay. The model drops the window
    // once the view leaves it, so it holds only what it shows.
    void setWindow(PlotWindowPtr window, PlotNote note = PlotNote::None, QString text = {});
    void setBusy(bool busy);
    // Of the work in progress, from 0 to 1; negative when unknown.
    void setProgress(double fraction);

    const PlotOverviewPtr& overview() const;
    const PlotWindowPtr& window() const;
    // The window the view wants: none when the installed one covers the view or
    // the view holds too many samples for one window.
    std::optional<PlotWindowRequest> detailRequest() const;

    // View and cursor operations used by SignalPlotItem.
    Q_INVOKABLE void resetView();
    void setVisibleRange(double start, double end);
    void zoomAt(double anchor, double scale);
    void panBy(double delta);
    // The pointer at `domain`, a column spanning halfWidth either side.
    void setCursor(double domain, double halfWidth);
    void clearCursor();

    // Detail: the window's elements in view. Both states: one extrema column per
    // pixel, cached until the view or content changes.
    std::pair<std::size_t, std::size_t> visibleSampleRange() const;
    const std::vector<PlotColumn>& columns(int pixelWidth) const;

    static QString numberText(double value);

signals:
    void contentChanged();
    void stateChanged();
    void busyChanged();
    void viewChanged();
    void cursorChanged();
    // A new signal or view range shows more than the installed window covers;
    // detailRequest() names the window it wants. Installing a window or a note
    // never emits it.
    void detailWanted();

private:
    // Range: the visible range moved, or the content under it changed; only
    // then may the view want another window.
    enum Change : unsigned { Content = 1, Busy = 2, View = 4, Cursor = 8, Range = 16 };

    void install(bool hasSignal, PlotHeader header, PlotOverviewPtr overview, PlotNote note,
                 QString text);
    // Emits the named changes, then the state if it moved since last announced,
    // then detailWanted after a Range change when the view still wants detail;
    // stops once an observer destroys the model.
    void announce(unsigned changes);
    void resetBounds();
    void updateValueRange();
    void updateCursor();
    PlotCursor cursorAt(double domain, double halfWidth) const;
    bool windowCoversView() const;
    void invalidateColumns();

    PlotHeader _header;
    bool _has_signal = false;
    PlotOverviewPtr _overview;
    PlotWindowPtr _window;
    PlotNote _signal_note = PlotNote::None;
    QString _signal_text;
    PlotNote _detail_note = PlotNote::None;
    QString _detail_text;
    bool _busy = false;
    double _progress = -1.0;

    double _full_start = 0.0;
    double _full_end = 0.0;
    double _view_start = 0.0;
    double _view_end = 0.0;
    double _view_minimum = -1.0;
    double _view_maximum = 1.0;
    double _minimum_view_span = 0.0;

    PlotCursor _cursor;
    double _cursor_domain = 0.0;
    double _cursor_half_width = 0.0;

    State _announced_state = NoSignal;
    QString _announced_message;

    mutable int _columns_width = -1;
    mutable std::vector<PlotColumn> _columns;
};
