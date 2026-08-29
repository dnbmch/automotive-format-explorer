#pragma once

#include <QString>

#include <memory>
#include <vector>

// Format-neutral hand-off between a document session and the plot stack.
// Domain and value are parallel arrays (the domain vector keeps the API name
// `time`). The producer normalizes at its own parse boundary: equal lengths and
// a non-decreasing domain, which the plot's binary searches depend on.
struct PlotSeries {
    QString name;
    QString unit;
    QString domainName;
    QString domainUnit;
    // Set by the producer when it knows why there are no samples (axis channel,
    // unsupported type, empty recording); plot surfaces show it instead of the
    // generic empty-state message.
    QString placeholderText;
    std::vector<double> time;
    std::vector<double> value;
};

// Decoded samples dominate a session's footprint, so a producer's cache and the
// plot model share one immutable buffer instead of each holding a copy.
using PlotSeriesPtr = std::shared_ptr<const PlotSeries>;
