#pragma once

#include "models/signalplotmodel.h"

#include <QColor>
#include <QPointF>
#include <QQuickPaintedItem>
#include <QRectF>

class QHoverEvent;
class QMouseEvent;
class QPainter;
class QWheelEvent;

class SignalPlotItem : public QQuickPaintedItem {
    Q_OBJECT

    Q_PROPERTY(SignalPlotModel* model READ model WRITE setModel NOTIFY modelChanged)
    Q_PROPERTY(bool panning READ panning NOTIFY panningChanged)

public:
    explicit SignalPlotItem(QQuickItem* parent = nullptr);

    void paint(QPainter* painter) override;

    SignalPlotModel* model() const;
    void setModel(SignalPlotModel* model);
    bool panning() const;

    Q_INVOKABLE void setColors(const QColor& background,
                               const QColor& grid,
                               const QColor& axis,
                               const QColor& series,
                               const QColor& cursor);
    Q_INVOKABLE void resetZoom();

signals:
    void modelChanged();
    void panningChanged();

protected:
    void hoverMoveEvent(QHoverEvent* event) override;
    void hoverLeaveEvent(QHoverEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;

private:
    QRectF plotRect() const;
    double timeAtX(qreal x) const;
    qreal xForTime(double time, const QRectF& rect) const;
    qreal yForValue(double value, const QRectF& rect) const;
    void updateCursorAt(const QPointF& position);
    void drawAxes(QPainter* painter, const QRectF& rect) const;
    void drawSeries(QPainter* painter, const QRectF& rect) const;
    void drawCursor(QPainter* painter, const QRectF& rect) const;
    void drawCenteredMessage(QPainter* painter, const QRectF& rect,
                             const QString& message) const;

    static QString numberLabel(double value);

    SignalPlotModel* _model = nullptr;
    QColor _background{0x1e, 0x1e, 0x1e};
    QColor _grid{0x44, 0x44, 0x44};
    QColor _axis{0xaa, 0xaa, 0xaa};
    QColor _series_color{0x5b, 0x9b, 0xd5};
    QColor _cursor_color{0xe0, 0xc0, 0x60};

    bool _panning = false;
    QPointF _last_drag_position;
};
