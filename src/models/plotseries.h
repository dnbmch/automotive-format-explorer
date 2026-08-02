#pragma once

#include <QString>

#include <vector>

// Format-neutral hand-off between a document session and the plot stack.
// Time and value are parallel arrays; time is monotonic and both arrays have
// the same length when handed to SignalPlotModel.
struct PlotSeries {
    QString name;
    QString unit;
    QString domainName;
    QString domainUnit;
    std::vector<double> time;
    std::vector<double> value;
};
