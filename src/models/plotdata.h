#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

// Format-neutral plot data. A producer's worker builds an overview and exact
// windows, each from the ordered sample chunks of one scan; the plot installs
// them immutable and shared, so a producer's cache and the plot hold one copy.
// One overview and the windows requested from it form a result set and share
// its domain.

// An overview partitions a signal's samples into at most this many bins.
constexpr std::size_t kPlotOverviewBins = 4096;
// An exact window holds at most this many samples, its two neighbors included:
// 64 MiB of time and value doubles.
constexpr std::uint64_t kPlotWindowSamples = std::uint64_t{1} << 22;
// A window keeps the finite extrema of every run of this many samples.
constexpr std::size_t kPlotBlockSamples = 256;

// What the domain axis of a result set shows.
enum class PlotDomain : std::uint8_t {
    Time,   // the producer's coordinates: finite and nondecreasing over the whole scan
    Index,  // absolute sample indices, because some coordinate was not
};

// A run of consecutive samples. Bins cover exactly the delivered samples, so
// none is empty; one whose values are all nonfinite has finiteCount 0 and NaN
// extrema.
struct PlotBin {
    std::uint64_t firstSample = 0;  // absolute index of the first sample
    std::uint64_t sampleCount = 0;
    std::uint64_t finiteCount = 0;  // samples with a finite value
    double firstDomain = 0.0;       // coordinate of the first sample
    double lastDomain = 0.0;        // coordinate of the last sample
    // Widest distance between consecutive coordinates inside the bin: where it
    // exceeds a pixel column, the bin leaves columns between its ends empty.
    double widestGap = 0.0;
    double minimum = 0.0;           // finite extrema
    double maximum = 0.0;
};

// The window that holds every sample with a coordinate in [start, end]: the
// index cover a producer scans, and what lies outside it.
struct PlotWindowRequest {
    PlotDomain domain = PlotDomain::Time;
    double start = 0.0;
    double end = 0.0;
    std::uint64_t firstSample = 0;
    std::uint64_t sampleCount = 0;
    bool samplesBefore = false;  // samples precede the cover
    bool samplesAfter = false;   // samples follow the cover
};

// A whole signal at bin resolution.
struct PlotOverview {
    PlotDomain domain = PlotDomain::Time;
    std::uint64_t requestedCount = 0;  // samples the source stated
    std::uint64_t sampleCount = 0;     // samples the scan delivered
    std::uint64_t finiteCount = 0;
    double minimum = std::numeric_limits<double>::quiet_NaN();  // finite extrema
    double maximum = std::numeric_limits<double>::quiet_NaN();
    // Smallest positive distance between consecutive coordinates; +inf if none.
    double minimumSpacing = std::numeric_limits<double>::infinity();
    // Every bin lies within one aligned run of 2^binShift sample indices.
    unsigned binShift = 0;
    std::vector<PlotBin> bins;  // in sample order

    // Fewer samples arrived than the source stated: the missing tail is unknown.
    bool incomplete() const { return sampleCount < requestedCount; }
    // Coordinates of the first and last sample; there must be one.
    double domainStart() const { return bins.front().firstDomain; }
    double domainEnd() const { return bins.back().lastDomain; }
    // The structure and its bin capacity: what the overview retains.
    std::uint64_t bytes() const;

    // The bins overlapping [start, end], as [first, last).
    std::pair<std::size_t, std::size_t> binsOverlapping(double start, double end) const;
    // Samples certainly within [start, end]: those of the bins inside it.
    std::uint64_t samplesWithin(double start, double end) const;
    // The cover of the bins that may hold a sample within [start, end], widened
    // by one sample each side for the neighbors a line needs.
    PlotWindowRequest windowRequest(double start, double end) const;
};

// Consecutive exact samples. The window holds every sample with a coordinate
// in the open range between its first and last one, or up to the edge of the
// data where it reaches it.
struct PlotWindow {
    PlotDomain domain = PlotDomain::Time;
    std::uint64_t firstSample = 0;  // absolute index of element 0
    bool atStart = false;           // no sample precedes the window
    bool atEnd = false;             // no sample follows it
    std::vector<double> time;       // coordinates, nondecreasing; the indices for Index
    std::vector<double> value;
    // Finite extrema of each run of kPlotBlockSamples; +inf and -inf when none.
    std::vector<double> blockMinimum;
    std::vector<double> blockMaximum;

    bool covers(double start, double end) const;
    // The structure and its array capacities: what the window retains.
    std::uint64_t bytes() const;
    // Finite extrema of elements [first, last); minimum > maximum when none.
    std::pair<double, double> extrema(std::size_t first, std::size_t last) const;
};

using PlotOverviewPtr = std::shared_ptr<const PlotOverview>;
using PlotWindowPtr = std::shared_ptr<const PlotWindow>;

// Builds an overview from the chunks of one scan, delivered in sample order.
// The bins are allocated at construction; when the scan outgrows them,
// neighbors merge pairwise into bins twice as wide, so building never
// allocates again and an overstated count costs no resolution.
class PlotOverviewBuilder {
public:
    explicit PlotOverviewBuilder(std::uint64_t statedCount);
    // What the builder and its overview hold at most, before either exists.
    static std::uint64_t reservation(std::uint64_t statedCount);

    void add(std::uint64_t firstSample, const double* time, const double* value,
             std::size_t size);
    // After the scan succeeded; requestedCount is its request after clamping.
    PlotOverviewPtr finish(std::uint64_t requestedCount);

private:
    std::shared_ptr<PlotOverview> _overview;
    std::size_t _bin_limit;  // bins allocated at construction
    std::uint64_t _samples = 0;
    double _last_time = 0.0;
    double _minimum_spacing = std::numeric_limits<double>::infinity();
    bool _time_valid = true;
};

enum class PlotBuild : std::uint8_t {
    Continue,  // keep scanning
    Refuse,    // the window would hold more samples than its limit
    Fail,      // the coordinates contradict the overview the request came from
};

// Builds the window a request describes from the scan of its cover, keeping
// the samples within the range and one neighbor each side. The arrays are
// allocated once, when the first sample is kept, for what the cover can still
// deliver.
class PlotWindowBuilder {
public:
    explicit PlotWindowBuilder(const PlotWindowRequest& request,
                               std::uint64_t maxSamples = kPlotWindowSamples);
    static std::uint64_t reservation(const PlotWindowRequest& request,
                                     std::uint64_t maxSamples = kPlotWindowSamples);

    // Refuse and Fail are final: the scan stops.
    PlotBuild add(std::uint64_t firstSample, const double* time, const double* value,
                  std::size_t size);
    // After the scan succeeded. Null, and state() Fail, when the samples did not
    // cover the request: the source no longer matches its overview.
    PlotWindowPtr finish();
    PlotBuild state() const { return _state; }

private:
    bool keep(std::uint64_t index, double time, double value);

    PlotWindowRequest _request;
    std::uint64_t _max_samples;
    std::shared_ptr<PlotWindow> _window;
    std::size_t _limit = 0;  // samples the arrays hold, once allocated
    std::uint64_t _seen = 0;
    double _last_time = 0.0;
    // The latest sample before the range: the left neighbor, until one in range comes.
    bool _has_before = false;
    std::uint64_t _before_index = 0;
    double _before_time = 0.0;
    double _before_value = 0.0;
    bool _done = false;  // the right neighbor is kept
    PlotBuild _state = PlotBuild::Continue;
};
