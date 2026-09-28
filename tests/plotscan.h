#pragma once

// Test feeds for the plot data builders: ordered chunks of absolute sample
// indices with their coordinates and values, as a producer's scan delivers them.

#include "models/plotdata.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <tuple>
#include <utility>
#include <vector>

namespace plotscan {

// The reader's default chunk.
constexpr std::size_t kChunk = std::size_t{1} << 16;

// Coordinate and value of an absolute sample index.
using Samples = std::function<std::pair<double, double>(std::uint64_t)>;
using Add = std::function<bool(std::uint64_t, const double*, const double*, std::size_t)>;

// Delivers [first, first + count) in chunks until add() declines.
inline void scan(std::uint64_t first, std::uint64_t count, const Samples& samples, const Add& add,
                 std::size_t chunk = kChunk) {
    std::vector<double> time;
    std::vector<double> value;
    for (std::uint64_t done = 0; done < count;) {
        const auto size = static_cast<std::size_t>(std::min<std::uint64_t>(chunk, count - done));
        time.resize(size);
        value.resize(size);
        for (std::size_t i = 0; i < size; ++i) {
            std::tie(time[i], value[i]) = samples(first + done + i);
        }
        if (!add(first + done, time.data(), value.data(), size)) {
            return;
        }
        done += size;
    }
}

// The overview of a scan from sample 0 that delivered `delivered` of `stated`.
inline PlotOverviewPtr overviewOf(std::uint64_t delivered, const Samples& samples,
                                  std::uint64_t stated) {
    PlotOverviewBuilder builder(stated);
    scan(0, delivered, samples,
         [&](std::uint64_t first, const double* time, const double* value, std::size_t size) {
             builder.add(first, time, value, size);
             return true;
         });
    return builder.finish(stated);
}

inline PlotOverviewPtr overviewOf(std::uint64_t count, const Samples& samples) {
    return overviewOf(count, samples, count);
}

// The window a request describes; null when the builder refused or failed,
// with its state in `state`.
inline PlotWindowPtr windowOf(const PlotWindowRequest& request, const Samples& samples,
                              std::uint64_t maxSamples = kPlotWindowSamples,
                              PlotBuild* state = nullptr) {
    PlotWindowBuilder builder(request, maxSamples);
    scan(request.firstSample, request.sampleCount, samples,
         [&](std::uint64_t first, const double* time, const double* value, std::size_t size) {
             return builder.add(first, time, value, size) == PlotBuild::Continue;
         });
    PlotWindowPtr window = builder.state() == PlotBuild::Continue ? builder.finish() : nullptr;
    if (state) {
        *state = builder.state();
    }
    return window;
}

// Coordinates index * step, values their sine.
inline Samples regular(double step) {
    return [step](std::uint64_t index) {
        const double time = static_cast<double>(index) * step;
        return std::make_pair(time, std::sin(time));
    };
}

} // namespace plotscan
