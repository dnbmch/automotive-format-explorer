#include "models/signalplotmodel.h"

#include <QVariant>

#include <algorithm>
#include <cmath>

SignalPlotModel::SignalPlotModel(QObject* parent)
    : QAbstractListModel(parent) {
}

int SignalPlotModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(_series.time.size());
}

QVariant SignalPlotModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 ||
        index.row() >= static_cast<int>(_series.time.size())) {
        return {};
    }

    const auto sample = static_cast<std::size_t>(index.row());
    switch (role) {
    case TimeRole: return _series.time[sample];
    case ValueRole: return _series.value[sample];
    }
    return {};
}

QHash<int, QByteArray> SignalPlotModel::roleNames() const {
    return {
        {TimeRole, "time"},
        {ValueRole, "value"},
    };
}

const PlotSeries& SignalPlotModel::series() const { return _series; }
QString SignalPlotModel::name() const { return _series.name; }
QString SignalPlotModel::unit() const { return _series.unit; }
QString SignalPlotModel::domainName() const { return _series.domainName; }
QString SignalPlotModel::domainUnit() const { return _series.domainUnit; }
quint64 SignalPlotModel::sampleCount() const {
    return static_cast<quint64>(_series.time.size());
}
bool SignalPlotModel::hasSeries() const { return !_series.time.empty(); }
bool SignalPlotModel::busy() const { return _busy; }

double SignalPlotModel::fullStart() const { return _full_start; }
double SignalPlotModel::fullEnd() const { return _full_end; }
double SignalPlotModel::viewStart() const { return _view_start; }
double SignalPlotModel::viewEnd() const { return _view_end; }
double SignalPlotModel::viewMinimum() const { return _view_minimum; }
double SignalPlotModel::viewMaximum() const { return _view_maximum; }

bool SignalPlotModel::cursorVisible() const { return _cursor_index >= 0; }
qint64 SignalPlotModel::cursorIndex() const { return _cursor_index; }
double SignalPlotModel::cursorTime() const {
    return _cursor_index >= 0
        ? _series.time[static_cast<std::size_t>(_cursor_index)] : 0.0;
}
double SignalPlotModel::cursorValue() const {
    return _cursor_index >= 0
        ? _series.value[static_cast<std::size_t>(_cursor_index)] : 0.0;
}

void SignalPlotModel::setSeries(PlotSeries series) {
    const std::size_t count = std::min(series.time.size(), series.value.size());
    series.time.resize(count);
    series.value.resize(count);

    beginResetModel();
    _series = std::move(series);
    _cursor_index = -1;
    rebuildSummaries();

    if (_series.time.empty()) {
        _full_start = 0.0;
        _full_end = 0.0;
        _view_start = 0.0;
        _view_end = 0.0;
        _view_minimum = -1.0;
        _view_maximum = 1.0;
        _minimum_view_span = 0.0;
    } else {
        _full_start = _series.time.front();
        _full_end = _series.time.back();
        rebuildTimeSpacing();
        if (_full_end > _full_start) {
            _view_start = _full_start;
            _view_end = _full_end;
        } else {
            _view_start = _full_start - 0.5;
            _view_end = _full_start + 0.5;
        }
        updateValueRange();
    }
    invalidateBuckets();
    endResetModel();

    emit seriesChanged();
    emit viewChanged();
    emit cursorChanged();
}

void SignalPlotModel::setBusy(bool busy) {
    if (_busy == busy) {
        return;
    }
    _busy = busy;
    emit busyChanged();
}

void SignalPlotModel::resetView() {
    if (_series.time.empty()) {
        return;
    }

    if (_full_end > _full_start) {
        setVisibleRange(_full_start, _full_end);
        return;
    }

    const double start = _full_start - 0.5;
    const double end = _full_start + 0.5;
    if (_view_start == start && _view_end == end) {
        return;
    }
    _view_start = start;
    _view_end = end;
    updateValueRange();
    invalidateBuckets();
    emit viewChanged();
}

void SignalPlotModel::setVisibleRange(double start, double end) {
    if (_series.time.empty() || !(end > start) || !std::isfinite(start) || !std::isfinite(end)) {
        return;
    }

    const double fullSpan = _full_end - _full_start;
    if (!(fullSpan > 0.0)) {
        resetView();
        return;
    }

    double span = std::clamp(end - start, minimumViewSpan(), fullSpan);
    double center = start + (end - start) * 0.5;
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
    updateValueRange();
    invalidateBuckets();
    emit viewChanged();
}

void SignalPlotModel::zoomAt(double anchor, double scale) {
    if (_series.time.empty() || !(scale > 0.0) || !std::isfinite(scale)) {
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

void SignalPlotModel::setCursorTime(double time) {
    if (_series.time.empty() || !std::isfinite(time)) {
        clearCursor();
        return;
    }

    const auto it = std::lower_bound(_series.time.begin(), _series.time.end(), time);
    std::size_t index = 0;
    if (it == _series.time.end()) {
        index = _series.time.size() - 1;
    } else {
        index = static_cast<std::size_t>(std::distance(_series.time.begin(), it));
        if (index > 0 &&
            time - _series.time[index - 1] <= _series.time[index] - time) {
            --index;
        }
    }

    const qint64 next = static_cast<qint64>(index);
    if (_cursor_index == next) {
        return;
    }
    _cursor_index = next;
    emit cursorChanged();
}

void SignalPlotModel::clearCursor() {
    if (_cursor_index < 0) {
        return;
    }
    _cursor_index = -1;
    emit cursorChanged();
}

std::pair<std::size_t, std::size_t> SignalPlotModel::visibleSampleRange() const {
    if (_series.time.empty()) {
        return {0, 0};
    }

    const auto first = std::lower_bound(_series.time.begin(), _series.time.end(), _view_start);
    const auto last = std::upper_bound(first, _series.time.end(), _view_end);
    return {
        static_cast<std::size_t>(std::distance(_series.time.begin(), first)),
        static_cast<std::size_t>(std::distance(_series.time.begin(), last)),
    };
}

const std::vector<PlotBucket>& SignalPlotModel::buckets(int pixelWidth) const {
    pixelWidth = std::max(1, pixelWidth);
    if (_bucket_width == pixelWidth) {
        return _bucket_cache;
    }

    _bucket_width = pixelWidth;
    _bucket_cache.clear();
    if (_series.time.empty() || !(_view_end > _view_start)) {
        return _bucket_cache;
    }

    const auto visible = visibleSampleRange();
    if (visible.first == visible.second) {
        return _bucket_cache;
    }

    _bucket_cache.reserve(static_cast<std::size_t>(pixelWidth));
    const double span = _view_end - _view_start;
    std::size_t first = visible.first;

    for (int column = 0; column < pixelWidth && first < visible.second; ++column) {
        const double bucketStart = _view_start + span * column / pixelWidth;
        const double bucketEnd = _view_start + span * (column + 1) / pixelWidth;

        std::size_t last;
        if (column == pixelWidth - 1) {
            last = visible.second;
        } else {
            const auto it = std::lower_bound(_series.time.begin() + static_cast<std::ptrdiff_t>(first),
                                             _series.time.begin() + static_cast<std::ptrdiff_t>(visible.second),
                                             bucketEnd);
            last = static_cast<std::size_t>(std::distance(_series.time.begin(), it));
        }

        if (last > first) {
            const Extrema extrema = extremaForRange(first, last);
            if (extrema.valid()) {
                _bucket_cache.push_back({
                    bucketStart + (bucketEnd - bucketStart) * 0.5,
                    extrema.minimum,
                    extrema.maximum,
                    first,
                    last,
                });
            }
        }
        first = last;
    }

    return _bucket_cache;
}

bool SignalPlotModel::Extrema::valid() const {
    return minimum <= maximum;
}

void SignalPlotModel::Extrema::include(double value) {
    if (!std::isfinite(value)) {
        return;
    }
    minimum = std::min(minimum, value);
    maximum = std::max(maximum, value);
}

void SignalPlotModel::Extrema::include(double minimumValue, double maximumValue) {
    if (!std::isfinite(minimumValue) || !std::isfinite(maximumValue)) {
        return;
    }
    minimum = std::min(minimum, minimumValue);
    maximum = std::max(maximum, maximumValue);
}

void SignalPlotModel::rebuildSummaries() {
    const std::size_t blockCount =
        (_series.value.size() + kSummaryBlockSize - 1) / kSummaryBlockSize;
    _block_minimum.assign(blockCount, std::numeric_limits<double>::infinity());
    _block_maximum.assign(blockCount, -std::numeric_limits<double>::infinity());

    for (std::size_t i = 0; i < _series.value.size(); ++i) {
        const double value = _series.value[i];
        if (!std::isfinite(value)) {
            continue;
        }
        const std::size_t block = i / kSummaryBlockSize;
        _block_minimum[block] = std::min(_block_minimum[block], value);
        _block_maximum[block] = std::max(_block_maximum[block], value);
    }
}

void SignalPlotModel::rebuildTimeSpacing() {
    const double fullSpan = _full_end - _full_start;
    if (!(fullSpan > 0.0) || _series.time.size() < 2) {
        _minimum_view_span = 0.0;
        return;
    }

    double minimumSpacing = std::numeric_limits<double>::infinity();
    for (std::size_t i = 1; i < _series.time.size(); ++i) {
        const double spacing = _series.time[i] - _series.time[i - 1];
        if (spacing > 0.0 && std::isfinite(spacing)) {
            minimumSpacing = std::min(minimumSpacing, spacing);
        }
    }

    const double scale = std::max({std::abs(_full_start), std::abs(_full_end), fullSpan});
    double precisionFloor = std::numeric_limits<double>::epsilon() * scale * 16.0;
    if (!(precisionFloor > 0.0) || !std::isfinite(precisionFloor)) {
        precisionFloor = std::numeric_limits<double>::denorm_min() * 16.0;
    }

    const double spacingFloor = std::isfinite(minimumSpacing)
        ? std::max(minimumSpacing, precisionFloor)
        : precisionFloor;
    _minimum_view_span = std::min(spacingFloor, fullSpan);
}

void SignalPlotModel::updateValueRange() {
    auto range = visibleSampleRange();
    if (range.first == range.second && !_series.time.empty()) {
        const auto it = std::lower_bound(_series.time.begin(), _series.time.end(),
                                         _view_start + (_view_end - _view_start) * 0.5);
        const std::size_t nearest = it == _series.time.end()
            ? _series.time.size() - 1
            : static_cast<std::size_t>(std::distance(_series.time.begin(), it));
        range = {nearest, nearest + 1};
    }

    Extrema extrema = extremaForRange(range.first, range.second);
    if (!extrema.valid()) {
        _view_minimum = -1.0;
        _view_maximum = 1.0;
        return;
    }

    if (extrema.maximum > extrema.minimum) {
        const double padding = (extrema.maximum - extrema.minimum) * 0.05;
        _view_minimum = extrema.minimum - padding;
        _view_maximum = extrema.maximum + padding;
    } else {
        const double padding = std::max(1.0, std::abs(extrema.minimum) * 0.05);
        _view_minimum = extrema.minimum - padding;
        _view_maximum = extrema.maximum + padding;
    }
}

void SignalPlotModel::invalidateBuckets() {
    _bucket_width = -1;
    _bucket_cache.clear();
}

double SignalPlotModel::minimumViewSpan() const {
    return _minimum_view_span;
}

SignalPlotModel::Extrema SignalPlotModel::extremaForRange(std::size_t first,
                                                          std::size_t last) const {
    Extrema result;
    last = std::min(last, _series.value.size());
    first = std::min(first, last);

    while (first < last && first % kSummaryBlockSize != 0) {
        result.include(_series.value[first++]);
    }
    while (first + kSummaryBlockSize <= last) {
        const std::size_t block = first / kSummaryBlockSize;
        result.include(_block_minimum[block], _block_maximum[block]);
        first += kSummaryBlockSize;
    }
    while (first < last) {
        result.include(_series.value[first++]);
    }
    return result;
}
