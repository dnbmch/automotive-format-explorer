#include "models/plotdata.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();

// At least two bins: with runs of 2^63 indices, every index lies in one of two.
std::size_t binLimit(std::uint64_t statedCount) {
    return static_cast<std::size_t>(
        std::clamp<std::uint64_t>(statedCount, 2, kPlotOverviewBins));
}

std::size_t blockCount(std::size_t samples) {
    return (samples + kPlotBlockSamples - 1) / kPlotBlockSamples;
}

// The aligned run of 2^shift indices that holds index.
std::uint64_t runOf(std::uint64_t index, unsigned shift) {
    return index >> shift;
}

// Extrema still carry the building sentinels, +inf and -inf, when nothing is finite.
void mergeInto(PlotBin& into, const PlotBin& next) {
    into.widestGap = std::max({into.widestGap, next.widestGap, next.firstDomain - into.lastDomain});
    into.sampleCount += next.sampleCount;
    into.finiteCount += next.finiteCount;
    into.lastDomain = next.lastDomain;
    into.minimum = std::min(into.minimum, next.minimum);
    into.maximum = std::max(into.maximum, next.maximum);
}

} // namespace

std::uint64_t PlotOverview::bytes() const {
    return sizeof(PlotOverview) + bins.capacity() * sizeof(PlotBin);
}

std::pair<std::size_t, std::size_t> PlotOverview::binsOverlapping(double start,
                                                                  double end) const {
    const auto first = std::lower_bound(
        bins.begin(), bins.end(), start,
        [](const PlotBin& bin, double coordinate) { return bin.lastDomain < coordinate; });
    const auto last = std::upper_bound(
        first, bins.end(), end,
        [](double coordinate, const PlotBin& bin) { return coordinate < bin.firstDomain; });
    return {static_cast<std::size_t>(first - bins.begin()),
            static_cast<std::size_t>(last - bins.begin())};
}

std::uint64_t PlotOverview::samplesWithin(double start, double end) const {
    const auto first = std::lower_bound(
        bins.begin(), bins.end(), start,
        [](const PlotBin& bin, double coordinate) { return bin.firstDomain < coordinate; });
    const auto last = std::upper_bound(
        first, bins.end(), end,
        [](double coordinate, const PlotBin& bin) { return coordinate < bin.lastDomain; });
    if (first == last) {
        return 0;
    }
    const PlotBin& back = *(last - 1);
    return back.firstSample + back.sampleCount - first->firstSample;
}

PlotWindowRequest PlotOverview::windowRequest(double start, double end) const {
    PlotWindowRequest request;
    request.domain = domain;
    request.start = start;
    request.end = end;
    if (bins.empty()) {
        return request;
    }

    const std::uint64_t dataStart = bins.front().firstSample;
    const std::uint64_t dataEnd = bins.back().firstSample + bins.back().sampleCount;
    const auto [first, last] = binsOverlapping(start, end);
    std::uint64_t coverStart = first < bins.size() ? bins[first].firstSample : dataEnd;
    std::uint64_t coverEnd =
        last > 0 ? bins[last - 1].firstSample + bins[last - 1].sampleCount : dataStart;
    // The bins before and after lie wholly outside the range: their nearest
    // samples are the neighbors.
    if (coverStart > dataStart) {
        --coverStart;
    }
    if (coverEnd < dataEnd) {
        ++coverEnd;
    }
    request.firstSample = coverStart;
    request.sampleCount = coverEnd - coverStart;
    request.samplesBefore = coverStart > dataStart;
    request.samplesAfter = coverEnd < dataEnd;
    return request;
}

bool PlotWindow::covers(double start, double end) const {
    return !time.empty() && (atStart || time.front() < start) && (atEnd || end < time.back());
}

std::uint64_t PlotWindow::bytes() const {
    return sizeof(PlotWindow) + (time.capacity() + value.capacity() + blockMinimum.capacity() +
                                 blockMaximum.capacity()) * sizeof(double);
}

std::pair<double, double> PlotWindow::extrema(std::size_t first, std::size_t last) const {
    double minimum = kInf;
    double maximum = -kInf;
    const auto include = [&](double sample) {
        if (std::isfinite(sample)) {
            minimum = std::min(minimum, sample);
            maximum = std::max(maximum, sample);
        }
    };
    last = std::min(last, value.size());
    first = std::min(first, last);
    while (first < last && first % kPlotBlockSamples != 0) {
        include(value[first++]);
    }
    while (first + kPlotBlockSamples <= last) {
        const std::size_t block = first / kPlotBlockSamples;
        minimum = std::min(minimum, blockMinimum[block]);
        maximum = std::max(maximum, blockMaximum[block]);
        first += kPlotBlockSamples;
    }
    while (first < last) {
        include(value[first++]);
    }
    return {minimum, maximum};
}

PlotOverviewBuilder::PlotOverviewBuilder(std::uint64_t statedCount)
    : _overview(std::make_shared<PlotOverview>()),
      _bin_limit(binLimit(statedCount)) {
    _overview->bins.reserve(_bin_limit);
}

std::uint64_t PlotOverviewBuilder::reservation(std::uint64_t statedCount) {
    return sizeof(PlotOverviewBuilder) + sizeof(PlotOverview) +
           binLimit(statedCount) * sizeof(PlotBin);
}

void PlotOverviewBuilder::add(std::uint64_t firstSample, const double* time,
                              const double* value, std::size_t size) {
    PlotOverview& overview = *_overview;
    std::vector<PlotBin>& bins = overview.bins;
    std::size_t i = 0;
    while (i < size) {
        const std::uint64_t index = firstSample + i;
        const auto joinsLast = [&] {
            return !bins.empty() && runOf(index, overview.binShift) ==
                                        runOf(bins.back().firstSample, overview.binShift);
        };
        if (!joinsLast()) {
            // Every bin in use: widen the runs until neighbors merge or the
            // index joins the last bin.
            while (bins.size() == _bin_limit && !joinsLast()) {
                ++overview.binShift;
                std::size_t kept = 0;
                for (std::size_t b = 1; b < bins.size(); ++b) {
                    if (runOf(bins[b].firstSample, overview.binShift) ==
                        runOf(bins[kept].firstSample, overview.binShift)) {
                        mergeInto(bins[kept], bins[b]);
                    } else {
                        bins[++kept] = bins[b];
                    }
                }
                bins.resize(kept + 1);
            }
            if (!joinsLast()) {
                bins.push_back({index, 0, 0, time[i], time[i], 0.0, kInf, -kInf});
            }
        }

        // The samples up to the end of the last bin's run.
        PlotBin& bin = bins.back();
        const std::uint64_t width = std::uint64_t{1} << overview.binShift;
        const std::uint64_t room = width - (index & (width - 1));
        const std::size_t end =
            i + static_cast<std::size_t>(std::min<std::uint64_t>(room, size - i));
        bool inside = bin.sampleCount > 0;
        bin.sampleCount += end - i;
        for (; i < end; ++i) {
            const double coordinate = time[i];
            const double sample = value[i];
            if (inside) {
                bin.widestGap = std::max(bin.widestGap, coordinate - bin.lastDomain);
            }
            inside = true;
            bin.lastDomain = coordinate;
            if (std::isfinite(sample)) {
                ++bin.finiteCount;
                bin.minimum = std::min(bin.minimum, sample);
                bin.maximum = std::max(bin.maximum, sample);
            }
            if (_time_valid) {
                if (!std::isfinite(coordinate) || (_samples > 0 && coordinate < _last_time)) {
                    _time_valid = false;
                } else if (_samples > 0 && coordinate > _last_time) {
                    _minimum_spacing = std::min(_minimum_spacing, coordinate - _last_time);
                }
            }
            _last_time = coordinate;
            ++_samples;
        }
    }
}

PlotOverviewPtr PlotOverviewBuilder::finish(std::uint64_t requestedCount) {
    PlotOverview& overview = *_overview;
    overview.requestedCount = requestedCount;
    overview.sampleCount = _samples;
    if (_time_valid) {
        overview.minimumSpacing = _minimum_spacing;
    } else {
        // A coordinate was nonfinite or went back: the whole result set plots
        // against sample indices, which the bins already partition.
        overview.domain = PlotDomain::Index;
        overview.minimumSpacing = _samples > 1 ? 1.0 : kInf;
        for (PlotBin& bin : overview.bins) {
            bin.firstDomain = static_cast<double>(bin.firstSample);
            bin.lastDomain = static_cast<double>(bin.firstSample + (bin.sampleCount - 1));
            bin.widestGap = bin.sampleCount > 1 ? 1.0 : 0.0;
        }
    }

    double minimum = kInf;
    double maximum = -kInf;
    for (PlotBin& bin : overview.bins) {
        overview.finiteCount += bin.finiteCount;
        if (bin.finiteCount == 0) {
            bin.minimum = kNaN;
            bin.maximum = kNaN;
        } else {
            minimum = std::min(minimum, bin.minimum);
            maximum = std::max(maximum, bin.maximum);
        }
    }
    if (overview.finiteCount > 0) {
        overview.minimum = minimum;
        overview.maximum = maximum;
    }
    return std::move(_overview);
}

PlotWindowBuilder::PlotWindowBuilder(const PlotWindowRequest& request, std::uint64_t maxSamples)
    : _request(request),
      _max_samples(maxSamples),
      _window(std::make_shared<PlotWindow>()) {
    _window->domain = request.domain;
}

std::uint64_t PlotWindowBuilder::reservation(const PlotWindowRequest& request,
                                             std::uint64_t maxSamples) {
    const std::uint64_t samples = std::min(maxSamples, request.sampleCount);
    const std::uint64_t blocks = (samples + kPlotBlockSamples - 1) / kPlotBlockSamples;
    return sizeof(PlotWindowBuilder) + sizeof(PlotWindow) +
           2 * (samples + blocks) * sizeof(double);
}

PlotBuild PlotWindowBuilder::add(std::uint64_t firstSample, const double* time,
                                 const double* value, std::size_t size) {
    if (_state != PlotBuild::Continue) {
        return _state;
    }
    const bool indexDomain = _request.domain == PlotDomain::Index;
    for (std::size_t i = 0; i < size; ++i) {
        const std::uint64_t index = firstSample + i;
        const double coordinate = indexDomain ? static_cast<double>(index) : time[i];
        // The overview found every coordinate finite and in order.
        if (!std::isfinite(coordinate) || (_seen > 0 && coordinate < _last_time)) {
            _state = PlotBuild::Fail;
            return _state;
        }
        _last_time = coordinate;
        ++_seen;
        if (_done) {
            continue;
        }
        if (coordinate < _request.start) {
            _has_before = true;
            _before_index = index;
            _before_time = coordinate;
            _before_value = value[i];
            continue;
        }
        if (_has_before) {
            _has_before = false;
            if (!keep(_before_index, _before_time, _before_value)) {
                _state = PlotBuild::Refuse;
                return _state;
            }
        }
        if (!keep(index, coordinate, value[i])) {
            _state = PlotBuild::Refuse;
            return _state;
        }
        _done = coordinate > _request.end;
    }
    return PlotBuild::Continue;
}

PlotWindowPtr PlotWindowBuilder::finish() {
    if (_state != PlotBuild::Continue) {
        return {};
    }
    // No sample reached the range: the last one before it stands for it.
    if (_has_before) {
        _has_before = false;
        if (!keep(_before_index, _before_time, _before_value)) {
            _state = PlotBuild::Refuse;
            return {};
        }
    }
    PlotWindow& window = *_window;
    if (!window.time.empty()) {
        const std::uint64_t end = window.firstSample + window.time.size();
        window.atStart = !_request.samplesBefore && window.firstSample == _request.firstSample;
        window.atEnd = !_request.samplesAfter &&
                       end == _request.firstSample + _request.sampleCount;
    }
    if (!window.covers(_request.start, _request.end)) {
        _state = PlotBuild::Fail;
        return {};
    }
    return std::move(_window);
}

bool PlotWindowBuilder::keep(std::uint64_t index, double time, double value) {
    PlotWindow& window = *_window;
    if (_limit == 0) {
        const std::uint64_t coverEnd = _request.firstSample + _request.sampleCount;
        _limit = static_cast<std::size_t>(std::min(_max_samples, coverEnd - index));
        window.firstSample = index;
        window.time.reserve(_limit);
        window.value.reserve(_limit);
        window.blockMinimum.reserve(blockCount(_limit));
        window.blockMaximum.reserve(blockCount(_limit));
    }
    if (window.time.size() == _limit) {
        return false;
    }
    if (window.time.size() % kPlotBlockSamples == 0) {
        window.blockMinimum.push_back(kInf);
        window.blockMaximum.push_back(-kInf);
    }
    window.time.push_back(time);
    window.value.push_back(value);
    if (std::isfinite(value)) {
        window.blockMinimum.back() = std::min(window.blockMinimum.back(), value);
        window.blockMaximum.back() = std::max(window.blockMaximum.back(), value);
    }
    return true;
}
