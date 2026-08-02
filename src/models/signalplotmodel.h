#pragma once

#include "core/explorercoreexport.h"
#include "models/plotseries.h"

#include <QAbstractListModel>
#include <QString>

#include <cstddef>
#include <limits>
#include <utility>
#include <vector>

struct PlotBucket {
    double time = 0.0;
    double minimum = 0.0;
    double maximum = 0.0;
    std::size_t firstSample = 0;
    std::size_t lastSample = 0; // exclusive
};

// Format-neutral time-series model shared by document-session providers and
// the painted plot. Providers only call setSeries() and setBusy(); zoom,
// cursor, and bucket state belong here so repainting stays bounded by the
// viewport rather than by the complete recording.
class EXPLORER_CORE_EXPORT SignalPlotModel : public QAbstractListModel {
    Q_OBJECT

    Q_PROPERTY(QString name READ name NOTIFY seriesChanged)
    Q_PROPERTY(QString unit READ unit NOTIFY seriesChanged)
    Q_PROPERTY(QString domainName READ domainName NOTIFY seriesChanged)
    Q_PROPERTY(QString domainUnit READ domainUnit NOTIFY seriesChanged)
    Q_PROPERTY(qulonglong sampleCount READ sampleCount NOTIFY seriesChanged)
    Q_PROPERTY(bool hasSeries READ hasSeries NOTIFY seriesChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(double fullStart READ fullStart NOTIFY seriesChanged)
    Q_PROPERTY(double fullEnd READ fullEnd NOTIFY seriesChanged)
    Q_PROPERTY(double viewStart READ viewStart NOTIFY viewChanged)
    Q_PROPERTY(double viewEnd READ viewEnd NOTIFY viewChanged)
    Q_PROPERTY(double viewMinimum READ viewMinimum NOTIFY viewChanged)
    Q_PROPERTY(double viewMaximum READ viewMaximum NOTIFY viewChanged)
    Q_PROPERTY(bool cursorVisible READ cursorVisible NOTIFY cursorChanged)
    Q_PROPERTY(qint64 cursorIndex READ cursorIndex NOTIFY cursorChanged)
    Q_PROPERTY(double cursorTime READ cursorTime NOTIFY cursorChanged)
    Q_PROPERTY(double cursorValue READ cursorValue NOTIFY cursorChanged)

public:
    enum Roles {
        TimeRole = Qt::UserRole + 1,
        ValueRole,
    };

    explicit SignalPlotModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    const PlotSeries& series() const;
    QString name() const;
    QString unit() const;
    QString domainName() const;
    QString domainUnit() const;
    quint64 sampleCount() const;
    bool hasSeries() const;
    bool busy() const;

    double fullStart() const;
    double fullEnd() const;
    double viewStart() const;
    double viewEnd() const;
    double viewMinimum() const;
    double viewMaximum() const;

    bool cursorVisible() const;
    qint64 cursorIndex() const;
    double cursorTime() const;
    double cursorValue() const;

    // Document-session provider contract.
    void setSeries(PlotSeries series);
    void setBusy(bool busy);

    // View and cursor operations used by SignalPlotItem.
    Q_INVOKABLE void resetView();
    void setVisibleRange(double start, double end);
    void zoomAt(double anchor, double scale);
    void panBy(double delta);
    void setCursorTime(double time);
    void clearCursor();

    // Exact visible index range and min/max columns for the current viewport.
    // buckets() is cached until either the view or series changes.
    std::pair<std::size_t, std::size_t> visibleSampleRange() const;
    const std::vector<PlotBucket>& buckets(int pixelWidth) const;

signals:
    void seriesChanged();
    void busyChanged();
    void viewChanged();
    void cursorChanged();

private:
    struct Extrema {
        double minimum = std::numeric_limits<double>::infinity();
        double maximum = -std::numeric_limits<double>::infinity();

        bool valid() const;
        void include(double value);
        void include(double minimumValue, double maximumValue);
    };

    static constexpr std::size_t kSummaryBlockSize = 256;

    void rebuildSummaries();
    void rebuildTimeSpacing();
    void updateValueRange();
    void invalidateBuckets();
    double minimumViewSpan() const;
    Extrema extremaForRange(std::size_t first, std::size_t last) const;

    PlotSeries _series;
    bool _busy = false;

    double _full_start = 0.0;
    double _full_end = 0.0;
    double _view_start = 0.0;
    double _view_end = 0.0;
    double _view_minimum = -1.0;
    double _view_maximum = 1.0;
    double _minimum_view_span = 0.0;

    qint64 _cursor_index = -1;

    std::vector<double> _block_minimum;
    std::vector<double> _block_maximum;

    mutable int _bucket_width = -1;
    mutable std::vector<PlotBucket> _bucket_cache;
};
