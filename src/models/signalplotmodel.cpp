#include "models/signalplotmodel.h"

#include <QLocale>
#include <QPointer>
#include <QVariant>

#include <algorithm>
#include <cmath>
#include <tuple>

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();

QString countOf(std::uint64_t count) {
    return QLocale().toString(static_cast<qulonglong>(count));
}

QString withUnit(const QString& text, const QString& unit) {
    return unit.isEmpty() ? text : QStringLiteral("%1 %2").arg(text, unit);
}

// Why one window cannot show the view: the samples in it exceed its limit.
QString tooDenseText() {
    return QStringLiteral("More than %1 samples in view; zoom in for exact samples")
        .arg(countOf(kPlotWindowSamples));
}

} // namespace

SignalPlotModel::SignalPlotModel(QObject* parent)
    : QAbstractListModel(parent) {
}

int SignalPlotModel::rowCount(const QModelIndex&) const {
    return 0;
}

QVariant SignalPlotModel::data(const QModelIndex&, int) const {
    return {};
}

QString SignalPlotModel::name() const { return _header.name; }
QString SignalPlotModel::unit() const { return _header.unit; }

QString SignalPlotModel::domainName() const {
    return _overview && _overview->domain == PlotDomain::Index ? QStringLiteral("Sample index")
                                                               : _header.domainName;
}

QString SignalPlotModel::domainUnit() const {
    return _overview && _overview->domain == PlotDomain::Index ? QString() : _header.domainUnit;
}

bool SignalPlotModel::hasSamples() const { return _overview && _overview->sampleCount > 0; }
bool SignalPlotModel::incomplete() const { return _overview && _overview->incomplete(); }

QString SignalPlotModel::countText() const {
    if (!_overview) {
        return {};
    }
    if (_overview->incomplete()) {
        return QStringLiteral("%1 of %2 samples")
            .arg(countOf(_overview->sampleCount), countOf(_overview->requestedCount));
    }
    return QStringLiteral("%1 %2").arg(countOf(_overview->sampleCount),
                                       _overview->sampleCount == 1 ? QStringLiteral("sample")
                                                                   : QStringLiteral("samples"));
}

SignalPlotModel::State SignalPlotModel::plotState() const {
    if (_overview) {
        if (_overview->sampleCount == 0) {
            return Empty;
        }
        return windowCoversView() ? Detail : Overview;
    }
    switch (_signal_note) {
    case PlotNote::Empty: return Empty;
    case PlotNote::Failed: return Failed;
    case PlotNote::Refused: return Refused;
    case PlotNote::None: break;
    }
    return _has_signal ? Pending : NoSignal;
}

QString SignalPlotModel::message() const {
    switch (plotState()) {
    case NoSignal:
    case Detail:
        return {};
    case Pending:
        return QStringLiteral("Reading samples…");
    case Empty:
        if (!_signal_text.isEmpty()) {
            return _signal_text;
        }
        return incomplete() ? QStringLiteral("No samples: the recording ends before its first one")
                            : QStringLiteral("No samples recorded");
    case Failed:
        return _signal_text.isEmpty() ? QStringLiteral("Samples could not be read") : _signal_text;
    case Refused:
        return _signal_text.isEmpty() ? QStringLiteral("Samples exceed the viewer's limits")
                                      : _signal_text;
    case Overview:
        break;
    }
    switch (_detail_note) {
    case PlotNote::Failed:
        return _detail_text.isEmpty() ? QStringLiteral("Exact samples could not be read")
                                      : _detail_text;
    case PlotNote::Refused:
        return _detail_text.isEmpty() ? tooDenseText() : _detail_text;
    case PlotNote::Empty:
    case PlotNote::None:
        break;
    }
    if (!detailRequest()) {
        return tooDenseText();
    }
    return _busy ? QStringLiteral("Reading exact samples…") : QString();
}

bool SignalPlotModel::busy() const { return _busy; }
double SignalPlotModel::progress() const { return _progress; }

double SignalPlotModel::fullStart() const { return _full_start; }
double SignalPlotModel::fullEnd() const { return _full_end; }
double SignalPlotModel::viewStart() const { return _view_start; }
double SignalPlotModel::viewEnd() const { return _view_end; }
double SignalPlotModel::viewMinimum() const { return _view_minimum; }
double SignalPlotModel::viewMaximum() const { return _view_maximum; }

bool SignalPlotModel::cursorVisible() const { return _cursor.visible; }
const PlotCursor& SignalPlotModel::cursor() const { return _cursor; }

QStringList SignalPlotModel::cursorLines() const {
    if (!_cursor.visible) {
        return {};
    }
    const bool indices = _overview && _overview->domain == PlotDomain::Index;
    const QString domain = domainName().isEmpty() ? QStringLiteral("Domain") : domainName();
    // In the Index domain a coordinate is its sample index, shown exactly.
    const auto coordinate = [&](double value, std::uint64_t index) {
        return indices ? countOf(index) : withUnit(numberText(value), domainUnit());
    };
    if (_cursor.exact) {
        return {
            QStringLiteral("%1 = %2").arg(domain, coordinate(_cursor.firstDomain, _cursor.firstSample)),
            withUnit(numberText(_cursor.value), _header.unit),
            QStringLiteral("index %1").arg(countOf(_cursor.firstSample)),
        };
    }

    const std::uint64_t last = _cursor.firstSample + (_cursor.sampleCount - 1);
    QStringList lines;
    lines << (_cursor.sampleCount == 1
                  ? QStringLiteral("%1 %2").arg(domain,
                                                coordinate(_cursor.firstDomain, _cursor.firstSample))
                  : QStringLiteral("%1 %2 – %3")
                        .arg(domain, coordinate(_cursor.firstDomain, _cursor.firstSample),
                             coordinate(_cursor.lastDomain, last)));
    lines << (_cursor.finiteCount == 0
                  ? QStringLiteral("no finite value")
                  : QStringLiteral("min %1, max %2")
                        .arg(numberText(_cursor.minimum),
                             withUnit(numberText(_cursor.maximum), _header.unit)));
    QString count = _cursor.sampleCount == 1
        ? QStringLiteral("1 sample, index %1").arg(countOf(_cursor.firstSample))
        : QStringLiteral("%1 samples, index %2 – %3")
              .arg(countOf(_cursor.sampleCount), countOf(_cursor.firstSample), countOf(last));
    if (_cursor.finiteCount > 0 && _cursor.finiteCount < _cursor.sampleCount) {
        count += QStringLiteral(", %1 not finite")
                     .arg(countOf(_cursor.sampleCount - _cursor.finiteCount));
    }
    lines << count;
    return lines;
}

QString SignalPlotModel::cursorText() const {
    return cursorLines().join(QStringLiteral("  ·  "));
}

void SignalPlotModel::clear() {
    install(false, {}, {}, PlotNote::None, {});
}

void SignalPlotModel::setSignal(PlotHeader header, PlotOverviewPtr overview, PlotNote note,
                                QString text) {
    install(true, std::move(header), std::move(overview), note, std::move(text));
}

void SignalPlotModel::install(bool hasSignal, PlotHeader header, PlotOverviewPtr overview,
                              PlotNote note, QString text) {
    _has_signal = hasSignal;
    _header = std::move(header);
    _overview = std::move(overview);
    _window.reset();
    _signal_note = note;
    _signal_text = std::move(text);
    _detail_note = PlotNote::None;
    _detail_text.clear();
    _cursor = {};
    resetBounds();
    announce(Content | View | Cursor | Range);
}

void SignalPlotModel::setWindow(PlotWindowPtr window, PlotNote note, QString text) {
    _window = std::move(window);
    _detail_note = note;
    _detail_text = std::move(text);
    invalidateColumns();
    updateValueRange();
    updateCursor();
    announce(View | Cursor);
}

void SignalPlotModel::setBusy(bool busy) {
    if (_busy == busy) {
        return;
    }
    _busy = busy;
    announce(Busy);
}

void SignalPlotModel::setProgress(double fraction) {
    if (_progress == fraction) {
        return;
    }
    _progress = fraction;
    announce(Busy);
}

const PlotOverviewPtr& SignalPlotModel::overview() const { return _overview; }
const PlotWindowPtr& SignalPlotModel::window() const { return _window; }

std::optional<PlotWindowRequest> SignalPlotModel::detailRequest() const {
    if (!hasSamples() || windowCoversView()) {
        return std::nullopt;
    }
    // Room to pan: the view and as much again each side, when that fits.
    const double span = _view_end - _view_start;
    const PlotWindowRequest wide = _overview->windowRequest(
        std::max(_full_start, _view_start - span), std::min(_full_end, _view_end + span));
    if (wide.sampleCount <= kPlotWindowSamples) {
        return wide;
    }
    // The view alone, when it fits or may fit: the cover can overstate it by
    // the partial bins at its edges.
    const PlotWindowRequest view = _overview->windowRequest(_view_start, _view_end);
    if (view.sampleCount <= kPlotWindowSamples ||
        _overview->samplesWithin(_view_start, _view_end) < kPlotWindowSamples - 1) {
        return view;
    }
    return std::nullopt;
}

void SignalPlotModel::resetView() {
    if (!hasSamples()) {
        return;
    }
    if (_full_end > _full_start) {
        setVisibleRange(_full_start, _full_end);
    }
}

void SignalPlotModel::setVisibleRange(double start, double end) {
    if (!hasSamples() || !(end > start) || !std::isfinite(start) || !std::isfinite(end)) {
        return;
    }
    const double fullSpan = _full_end - _full_start;
    if (!(fullSpan > 0.0)) {
        return;
    }

    const double span = std::clamp(end - start, _minimum_view_span, fullSpan);
    const double center = start + (end - start) * 0.5;
    start = center - span * 0.5;
    end = start + span;
    if (start < _full_start) {
        start = _full_start;
        end = start + span;
    }
    if (end > _full_end) {
        end = _full_end;
        start = end - span;
    }
    if (_view_start == start && _view_end == end) {
        return;
    }

    _view_start = start;
    _view_end = end;
    // The plot holds only a window it shows; a note on the missing detail
    // belonged to the previous view.
    if (_window && !_window->covers(_view_start, _view_end)) {
        _window.reset();
    }
    _detail_note = PlotNote::None;
    _detail_text.clear();
    invalidateColumns();
    updateValueRange();
    updateCursor();
    announce(View | Cursor | Range);
}

void SignalPlotModel::zoomAt(double anchor, double scale) {
    if (!hasSamples() || !(scale > 0.0) || !std::isfinite(scale)) {
        return;
    }
    const double oldSpan = _view_end - _view_start;
    if (!(oldSpan > 0.0)) {
        return;
    }
    anchor = std::clamp(anchor, _view_start, _view_end);
    const double ratio = (anchor - _view_start) / oldSpan;
    const double newSpan = oldSpan * std::clamp(scale, 0.01, 100.0);
    const double start = anchor - ratio * newSpan;
    setVisibleRange(start, start + newSpan);
}

void SignalPlotModel::panBy(double delta) {
    if (!std::isfinite(delta)) {
        return;
    }
    setVisibleRange(_view_start + delta, _view_end + delta);
}

void SignalPlotModel::setCursor(double domain, double halfWidth) {
    if (!hasSamples() || !std::isfinite(domain)) {
        clearCursor();
        return;
    }
    _cursor_domain = domain;
    _cursor_half_width = std::isfinite(halfWidth) ? std::max(0.0, halfWidth) : 0.0;
    const PlotCursor next = cursorAt(_cursor_domain, _cursor_half_width);
    if (_cursor.visible && next.exact == _cursor.exact &&
        next.firstSample == _cursor.firstSample && next.sampleCount == _cursor.sampleCount) {
        return;
    }
    _cursor = next;
    announce(Cursor);
}

void SignalPlotModel::clearCursor() {
    if (!_cursor.visible) {
        return;
    }
    _cursor = {};
    announce(Cursor);
}

std::pair<std::size_t, std::size_t> SignalPlotModel::visibleSampleRange() const {
    if (!windowCoversView()) {
        return {0, 0};
    }
    const std::vector<double>& time = _window->time;
    const auto first = std::lower_bound(time.begin(), time.end(), _view_start);
    const auto last = std::upper_bound(first, time.end(), _view_end);
    return {static_cast<std::size_t>(first - time.begin()),
            static_cast<std::size_t>(last - time.begin())};
}

const std::vector<PlotColumn>& SignalPlotModel::columns(int pixelWidth) const {
    pixelWidth = std::max(1, pixelWidth);
    if (_columns_width == pixelWidth) {
        return _columns;
    }
    _columns_width = pixelWidth;
    _columns.clear();
    if (!hasSamples() || !(_view_end > _view_start)) {
        return _columns;
    }

    const auto width = static_cast<std::size_t>(pixelWidth);
    const double span = _view_end - _view_start;
    const auto center = [&](std::size_t column) {
        return _view_start + span * (static_cast<double>(column) + 0.5) / static_cast<double>(width);
    };
    if (windowCoversView()) {
        // Exact samples: the extrema of those falling in each column.
        const auto [visibleFirst, visibleLast] = visibleSampleRange();
        const std::vector<double>& time = _window->time;
        _columns.reserve(width);
        std::size_t first = visibleFirst;
        for (std::size_t column = 0; column < width && first < visibleLast; ++column) {
            std::size_t last = visibleLast;
            if (column + 1 < width) {
                const double columnEnd = _view_start + span * static_cast<double>(column + 1) /
                                                           static_cast<double>(width);
                last = static_cast<std::size_t>(
                    std::lower_bound(time.begin() + static_cast<std::ptrdiff_t>(first),
                                     time.begin() + static_cast<std::ptrdiff_t>(visibleLast),
                                     columnEnd) -
                    time.begin());
            }
            if (last > first) {
                const auto [minimum, maximum] = _window->extrema(first, last);
                if (minimum <= maximum) {
                    _columns.push_back({center(column), minimum, maximum});
                }
            }
            first = last;
        }
        return _columns;
    }

    // Overview: a bin spans its extrema over the columns from its first sample
    // to its last, when no gap inside it is wider than a column; otherwise only
    // the columns of its first and last sample, so no empty stretch gains a
    // band. Bins are never joined to one another: nothing reads as consecutive
    // samples.
    _columns.assign(width, PlotColumn{0.0, kInf, -kInf});
    const double scale = static_cast<double>(width) / span;
    const double columnWidth = span / static_cast<double>(width);
    const double lastColumn = static_cast<double>(width - 1);
    const auto columnOf = [&](double coordinate) {
        return static_cast<std::size_t>(
            std::clamp(std::floor((coordinate - _view_start) * scale), 0.0, lastColumn));
    };
    const auto [firstBin, lastBin] = _overview->binsOverlapping(_view_start, _view_end);
    for (std::size_t b = firstBin; b < lastBin; ++b) {
        const PlotBin& bin = _overview->bins[b];
        if (bin.finiteCount == 0) {
            continue;
        }
        const std::size_t first = columnOf(bin.firstDomain);
        const std::size_t last = columnOf(bin.lastDomain);
        const bool solid = bin.widestGap <= columnWidth;
        for (std::size_t column = first; column <= last;
             column = solid || column == last ? column + 1 : last) {
            _columns[column].minimum = std::min(_columns[column].minimum, bin.minimum);
            _columns[column].maximum = std::max(_columns[column].maximum, bin.maximum);
        }
    }
    std::size_t kept = 0;
    for (std::size_t column = 0; column < width; ++column) {
        if (_columns[column].minimum <= _columns[column].maximum) {
            _columns[kept++] = {center(column), _columns[column].minimum, _columns[column].maximum};
        }
    }
    _columns.resize(kept);
    return _columns;
}

QString SignalPlotModel::numberText(double value) {
    if (!std::isfinite(value)) {
        return QStringLiteral("—");
    }
    const double magnitude = std::abs(value);
    if (magnitude >= 1.0e6 || (magnitude > 0.0 && magnitude < 1.0e-4)) {
        return QString::number(value, 'e', 3);
    }
    return QString::number(value, 'g', 6);
}

void SignalPlotModel::announce(unsigned changes) {
    QPointer<SignalPlotModel> self(this);
    if (changes & Content) {
        emit contentChanged();
        if (!self) {
            return;
        }
    }
    if (changes & Busy) {
        emit busyChanged();
        if (!self) {
            return;
        }
    }
    if (changes & View) {
        emit viewChanged();
        if (!self) {
            return;
        }
    }
    if (changes & Cursor) {
        emit cursorChanged();
        if (!self) {
            return;
        }
    }
    // State and wanted detail as they stand now: an observer of the signals
    // above may have changed them, and announced that change itself.
    const State state = plotState();
    const QString text = message();
    if (state != _announced_state || text != _announced_message) {
        _announced_state = state;
        _announced_message = text;
        emit stateChanged();
        if (!self) {
            return;
        }
    }
    if ((changes & Range) != 0 && hasSamples() && !windowCoversView()) {
        emit detailWanted();
    }
}

void SignalPlotModel::resetBounds() {
    invalidateColumns();
    if (!hasSamples()) {
        _full_start = 0.0;
        _full_end = 0.0;
        _view_start = 0.0;
        _view_end = 0.0;
        _view_minimum = -1.0;
        _view_maximum = 1.0;
        _minimum_view_span = 0.0;
        return;
    }

    _full_start = _overview->domainStart();
    _full_end = _overview->domainEnd();
    const double fullSpan = _full_end - _full_start;
    if (fullSpan > 0.0) {
        _view_start = _full_start;
        _view_end = _full_end;
        // Zoom reaches the densest samples, down to what doubles resolve here.
        const double scale = std::max({std::abs(_full_start), std::abs(_full_end), fullSpan});
        double precisionFloor = std::numeric_limits<double>::epsilon() * scale * 16.0;
        if (!(precisionFloor > 0.0) || !std::isfinite(precisionFloor)) {
            precisionFloor = std::numeric_limits<double>::denorm_min() * 16.0;
        }
        const double spacing = _overview->minimumSpacing;
        _minimum_view_span = std::min(
            std::isfinite(spacing) ? std::max(spacing, precisionFloor) : precisionFloor, fullSpan);
    } else {
        _view_start = _full_start - 0.5;
        _view_end = _full_start + 0.5;
        _minimum_view_span = 0.0;
    }
    updateValueRange();
}

// The value axis follows the finite extrema in view: of the exact samples in
// Detail, else of the overview bins the view touches.
void SignalPlotModel::updateValueRange() {
    double minimum = kInf;
    double maximum = -kInf;
    if (windowCoversView()) {
        auto [first, last] = visibleSampleRange();
        if (first == last) {
            // Between two samples: the line joining them crosses the view.
            first = first > 0 ? first - 1 : first;
            last = std::min(last + 1, _window->value.size());
        }
        std::tie(minimum, maximum) = _window->extrema(first, last);
    } else if (hasSamples()) {
        auto [first, last] = _overview->binsOverlapping(_view_start, _view_end);
        if (first == last) {
            first = first > 0 ? first - 1 : first;
            last = std::min(last + 1, _overview->bins.size());
        }
        for (std::size_t b = first; b < last; ++b) {
            const PlotBin& bin = _overview->bins[b];
            if (bin.finiteCount > 0) {
                minimum = std::min(minimum, bin.minimum);
                maximum = std::max(maximum, bin.maximum);
            }
        }
    }

    if (!(minimum <= maximum)) {
        _view_minimum = -1.0;
        _view_maximum = 1.0;
        return;
    }
    const double padding = maximum > minimum ? (maximum - minimum) * 0.05
                                             : std::max(1.0, std::abs(minimum) * 0.05);
    _view_minimum = minimum - padding;
    _view_maximum = maximum + padding;
}

void SignalPlotModel::updateCursor() {
    if (_cursor.visible) {
        _cursor = cursorAt(_cursor_domain, _cursor_half_width);
    }
}

PlotCursor SignalPlotModel::cursorAt(double domain, double halfWidth) const {
    PlotCursor cursor;
    cursor.visible = true;
    if (windowCoversView()) {
        // The nearest exact sample.
        const std::vector<double>& time = _window->time;
        const auto it = std::lower_bound(time.begin(), time.end(), domain);
        std::size_t index = it == time.end() ? time.size() - 1
                                             : static_cast<std::size_t>(it - time.begin());
        if (index > 0 && domain - time[index - 1] <= time[index] - domain) {
            --index;
        }
        const double value = _window->value[index];
        cursor.exact = true;
        cursor.firstSample = _window->firstSample + index;
        cursor.sampleCount = 1;
        cursor.finiteCount = std::isfinite(value) ? 1 : 0;
        cursor.firstDomain = time[index];
        cursor.lastDomain = time[index];
        cursor.value = value;
        return cursor;
    }

    // The overview bins under the pointer's column, or across a gap the nearer one.
    const std::vector<PlotBin>& bins = _overview->bins;
    auto [first, last] = _overview->binsOverlapping(domain - halfWidth, domain + halfWidth);
    if (first == last) {
        if (first == bins.size() ||
            (first > 0 && domain - bins[first - 1].lastDomain <= bins[first].firstDomain - domain)) {
            --first;
        }
        last = first + 1;
    }
    const PlotBin& front = bins[first];
    const PlotBin& back = bins[last - 1];
    cursor.firstSample = front.firstSample;
    cursor.sampleCount = back.firstSample + back.sampleCount - front.firstSample;
    cursor.firstDomain = front.firstDomain;
    cursor.lastDomain = back.lastDomain;
    double minimum = kInf;
    double maximum = -kInf;
    for (std::size_t b = first; b < last; ++b) {
        cursor.finiteCount += bins[b].finiteCount;
        if (bins[b].finiteCount > 0) {
            minimum = std::min(minimum, bins[b].minimum);
            maximum = std::max(maximum, bins[b].maximum);
        }
    }
    if (cursor.finiteCount > 0) {
        cursor.minimum = minimum;
        cursor.maximum = maximum;
    }
    return cursor;
}

bool SignalPlotModel::windowCoversView() const {
    return _window && _window->covers(_view_start, _view_end);
}

void SignalPlotModel::invalidateColumns() {
    _columns_width = -1;
    _columns.clear();
}
