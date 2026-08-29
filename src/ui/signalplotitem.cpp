#include "ui/signalplotitem.h"

#include <QFontMetrics>
#include <QHoverEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

constexpr qreal kLeftMargin = 72.0;
constexpr qreal kRightMargin = 18.0;
constexpr qreal kTopMargin = 18.0;
constexpr qreal kBottomMargin = 52.0;

QString labelWithUnit(const QString& name, const QString& unit) {
    const QString label = name.isEmpty() ? QStringLiteral("Domain") : name;
    return unit.isEmpty() ? label : QStringLiteral("%1 (%2)").arg(label, unit);
}

} // namespace

SignalPlotItem::SignalPlotItem(QQuickItem* parent)
    : QQuickPaintedItem(parent) {
    setAcceptHoverEvents(true);
    setAcceptedMouseButtons(Qt::LeftButton);
    setRenderTarget(QQuickPaintedItem::FramebufferObject);
}

void SignalPlotItem::paint(QPainter* painter) {
    painter->fillRect(boundingRect(), _background);
    painter->setRenderHint(QPainter::Antialiasing, true);

    const QRectF rect = plotRect();
    if (rect.isEmpty()) {
        return;
    }

    drawAxes(painter, rect);
    if (!_model || !_model->hasSeries()) {
        QString message = QStringLiteral("Select a plottable channel");
        if (_model && _model->busy()) {
            message = QStringLiteral("Decoding samples\u2026");
        } else if (_model && !_model->placeholderText().isEmpty()) {
            message = _model->placeholderText();
        } else if (_model && !_model->name().isEmpty()) {
            message = QStringLiteral("No samples available");
        }
        drawCenteredMessage(painter, rect, message);
        return;
    }

    drawSeries(painter, rect);
    drawCursor(painter, rect);

    if (_model->busy()) {
        QColor veil = _background;
        veil.setAlpha(165);
        painter->fillRect(rect, veil);
        drawCenteredMessage(painter, rect, QStringLiteral("Decoding samples\u2026"));
    }
}

SignalPlotModel* SignalPlotItem::model() const {
    return _model;
}

void SignalPlotItem::setModel(SignalPlotModel* model) {
    if (_model == model) {
        return;
    }
    if (_model) {
        disconnect(_model, nullptr, this, nullptr);
    }

    _model = model;
    if (_model) {
        connect(_model, &SignalPlotModel::seriesChanged, this,
                [this] { update(); });
        connect(_model, &SignalPlotModel::viewChanged, this,
                [this] { update(); });
        connect(_model, &SignalPlotModel::busyChanged, this,
                [this] { update(); });
        connect(_model, &SignalPlotModel::cursorChanged, this,
                [this] { update(); });
    }

    emit modelChanged();
    update();
}

bool SignalPlotItem::panning() const {
    return _panning;
}

void SignalPlotItem::setColors(const QColor& background,
                               const QColor& grid,
                               const QColor& axis,
                               const QColor& series,
                               const QColor& cursor) {
    _background = background;
    _grid = grid;
    _axis = axis;
    _series_color = series;
    _cursor_color = cursor;
    update();
}

void SignalPlotItem::resetZoom() {
    if (_model) {
        _model->resetView();
    }
}

void SignalPlotItem::hoverMoveEvent(QHoverEvent* event) {
    updateCursorAt(event->position());
}

void SignalPlotItem::hoverLeaveEvent(QHoverEvent*) {
    if (_model && !_panning) {
        _model->clearCursor();
    }
}

void SignalPlotItem::wheelEvent(QWheelEvent* event) {
    if (!_model || !_model->hasSeries() || !plotRect().contains(event->position())) {
        event->ignore();
        return;
    }

    qreal steps = event->angleDelta().y() / 120.0;
    if (qFuzzyIsNull(steps)) {
        steps = event->pixelDelta().y() / 120.0;
    }
    if (qFuzzyIsNull(steps)) {
        event->ignore();
        return;
    }

    const double scale = std::pow(0.8, static_cast<double>(steps));
    _model->zoomAt(timeAtX(event->position().x()), scale);
    updateCursorAt(event->position());
    event->accept();
}

void SignalPlotItem::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton || !_model || !_model->hasSeries() ||
        !plotRect().contains(event->position())) {
        event->ignore();
        return;
    }

    _panning = true;
    _last_drag_position = event->position();
    setKeepMouseGrab(true);
    grabMouse();
    emit panningChanged();
    updateCursorAt(event->position());
    event->accept();
}

void SignalPlotItem::mouseMoveEvent(QMouseEvent* event) {
    if (!_panning || !_model) {
        event->ignore();
        return;
    }

    const QRectF rect = plotRect();
    const qreal dx = event->position().x() - _last_drag_position.x();
    const double span = _model->viewEnd() - _model->viewStart();
    if (rect.width() > 0.0 && !qFuzzyIsNull(dx)) {
        _model->panBy(-static_cast<double>(dx / rect.width()) * span);
    }
    _last_drag_position = event->position();
    updateCursorAt(event->position());
    event->accept();
}

void SignalPlotItem::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton || !_panning) {
        event->ignore();
        return;
    }

    _panning = false;
    setKeepMouseGrab(false);
    ungrabMouse();
    emit panningChanged();
    updateCursorAt(event->position());
    event->accept();
}

void SignalPlotItem::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && plotRect().contains(event->position())) {
        resetZoom();
        updateCursorAt(event->position());
        event->accept();
        return;
    }
    event->ignore();
}

void SignalPlotItem::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) {
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        update();
    }
}

QRectF SignalPlotItem::plotRect() const {
    const qreal plotWidth = width() - kLeftMargin - kRightMargin;
    const qreal plotHeight = height() - kTopMargin - kBottomMargin;
    if (plotWidth <= 1.0 || plotHeight <= 1.0) {
        return {};
    }
    return {kLeftMargin, kTopMargin, plotWidth, plotHeight};
}

double SignalPlotItem::timeAtX(qreal x) const {
    if (!_model) {
        return 0.0;
    }
    const QRectF rect = plotRect();
    const qreal clamped = std::clamp(x, rect.left(), rect.right());
    const double ratio = static_cast<double>((clamped - rect.left()) / rect.width());
    return _model->viewStart() + ratio * (_model->viewEnd() - _model->viewStart());
}

qreal SignalPlotItem::xForTime(double time, const QRectF& rect) const {
    const double span = _model->viewEnd() - _model->viewStart();
    if (!(span > 0.0)) {
        return rect.center().x();
    }
    return rect.left() + (time - _model->viewStart()) / span * rect.width();
}

qreal SignalPlotItem::yForValue(double value, const QRectF& rect) const {
    const double span = _model->viewMaximum() - _model->viewMinimum();
    if (!(span > 0.0)) {
        return rect.center().y();
    }
    return rect.bottom() - (value - _model->viewMinimum()) / span * rect.height();
}

void SignalPlotItem::updateCursorAt(const QPointF& position) {
    if (!_model) {
        return;
    }
    if (!_model->hasSeries() || !plotRect().contains(position)) {
        _model->clearCursor();
        return;
    }
    _model->setCursorTime(timeAtX(position.x()));
}

void SignalPlotItem::drawAxes(QPainter* painter, const QRectF& rect) const {
    QFont font(QStringLiteral("Consolas"));
    font.setPixelSize(10);
    painter->setFont(font);

    const double xMinimum = _model ? _model->viewStart() : 0.0;
    const double xMaximum = _model ? _model->viewEnd() : 1.0;
    const double yMinimum = _model ? _model->viewMinimum() : -1.0;
    const double yMaximum = _model ? _model->viewMaximum() : 1.0;

    const int xTickCount = std::clamp(static_cast<int>(rect.width() / 110.0), 1, 8);
    const int yTickCount = std::clamp(static_cast<int>(rect.height() / 70.0), 2, 7);

    painter->setPen(QPen(_grid, 1));
    for (int tick = 0; tick <= xTickCount; ++tick) {
        const qreal x = rect.left() + rect.width() * tick / xTickCount;
        painter->drawLine(QPointF(x, rect.top()), QPointF(x, rect.bottom()));
    }
    for (int tick = 0; tick <= yTickCount; ++tick) {
        const qreal y = rect.top() + rect.height() * tick / yTickCount;
        painter->drawLine(QPointF(rect.left(), y), QPointF(rect.right(), y));
    }

    painter->setPen(_axis);
    for (int tick = 0; tick <= xTickCount; ++tick) {
        const qreal x = rect.left() + rect.width() * tick / xTickCount;
        const double value = xMinimum + (xMaximum - xMinimum) * tick / xTickCount;
        QRectF labelRect(x - 55.0, rect.bottom() + 5.0, 110.0, 20.0);
        if (labelRect.left() < 2.0) {
            labelRect.moveLeft(2.0);
        } else if (labelRect.right() > width() - 2.0) {
            labelRect.moveRight(width() - 2.0);
        }
        painter->drawText(labelRect,
                          Qt::AlignHCenter | Qt::AlignTop, numberLabel(value));
    }
    for (int tick = 0; tick <= yTickCount; ++tick) {
        const qreal y = rect.bottom() - rect.height() * tick / yTickCount;
        const double value = yMinimum + (yMaximum - yMinimum) * tick / yTickCount;
        painter->drawText(QRectF(2.0, y - 10.0, kLeftMargin - 8.0, 20.0),
                          Qt::AlignRight | Qt::AlignVCenter, numberLabel(value));
    }

    painter->setPen(QPen(_axis, 1));
    painter->drawRect(rect);
    const QString domainLabel = _model
        ? labelWithUnit(_model->domainName(), _model->domainUnit())
        : QStringLiteral("Domain");
    painter->drawText(QRectF(rect.left(), rect.bottom() + 28.0, rect.width(), 16.0),
                      Qt::AlignHCenter | Qt::AlignTop, domainLabel);
}

void SignalPlotItem::drawSeries(QPainter* painter, const QRectF& rect) const {
    const auto visible = _model->visibleSampleRange();
    if (visible.first == visible.second) {
        return;
    }

    painter->save();
    painter->setClipRect(rect.adjusted(-1.0, -1.0, 1.0, 1.0));

    const std::size_t visibleCount = visible.second - visible.first;
    if (visibleCount <= static_cast<std::size_t>(std::max(1.0, rect.width() * 2.0))) {
        const PlotSeries& series = _model->series();
        std::size_t first = visible.first;
        std::size_t last = visible.second;
        if (first > 0) {
            --first;
        }
        if (last < series.time.size()) {
            ++last;
        }

        QPainterPath path;
        bool pathStarted = false;
        for (std::size_t i = first; i < last; ++i) {
            if (!std::isfinite(series.time[i]) || !std::isfinite(series.value[i])) {
                pathStarted = false;
                continue;
            }
            const QPointF point(xForTime(series.time[i], rect),
                                yForValue(series.value[i], rect));
            if (pathStarted) {
                path.lineTo(point);
            } else {
                path.moveTo(point);
                pathStarted = true;
            }
        }
        QPen pen(_series_color, 1.5);
        pen.setCosmetic(true);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        painter->setPen(pen);
        painter->drawPath(path);
    } else {
        const auto& columns = _model->buckets(static_cast<int>(std::ceil(rect.width())));
        std::vector<QLineF> lines;
        lines.reserve(columns.size());
        for (const PlotBucket& bucket : columns) {
            lines.emplace_back(xForTime(bucket.time, rect), yForValue(bucket.minimum, rect),
                               xForTime(bucket.time, rect), yForValue(bucket.maximum, rect));
        }

        QPen pen(_series_color, 1.0);
        pen.setCosmetic(true);
        pen.setCapStyle(Qt::RoundCap);
        painter->setPen(pen);
        if (!lines.empty()) {
            painter->drawLines(lines.data(), static_cast<int>(lines.size()));
        }
    }

    painter->restore();
}

void SignalPlotItem::drawCursor(QPainter* painter, const QRectF& rect) const {
    if (!_model || !_model->cursorVisible() ||
        _model->cursorTime() < _model->viewStart() ||
        _model->cursorTime() > _model->viewEnd()) {
        return;
    }

    const qreal x = xForTime(_model->cursorTime(), rect);
    const bool finiteValue = std::isfinite(_model->cursorValue());
    const qreal y = finiteValue
        ? yForValue(_model->cursorValue(), rect) : rect.center().y();

    QPen crosshair(_cursor_color, 1.0, Qt::DashLine);
    crosshair.setCosmetic(true);
    painter->setPen(crosshair);
    painter->drawLine(QPointF(x, rect.top()), QPointF(x, rect.bottom()));
    if (finiteValue) {
        painter->drawLine(QPointF(rect.left(), y), QPointF(rect.right(), y));
        painter->setBrush(_cursor_color);
        painter->setPen(Qt::NoPen);
        painter->drawEllipse(QPointF(x, y), 3.5, 3.5);
    }

    QString valueText = numberLabel(_model->cursorValue());
    if (!_model->unit().isEmpty()) {
        valueText += QStringLiteral(" %1").arg(_model->unit());
    }
    QString domainText = QStringLiteral("%1  %2")
        .arg(_model->domainName().isEmpty() ? QStringLiteral("Domain")
                                           : _model->domainName(),
             numberLabel(_model->cursorTime()));
    if (!_model->domainUnit().isEmpty()) {
        domainText += QStringLiteral(" %1").arg(_model->domainUnit());
    }
    const QString text = QStringLiteral("%1\n%2").arg(domainText, valueText);

    QFont font(QStringLiteral("Consolas"));
    font.setPixelSize(10);
    painter->setFont(font);
    const QFontMetrics metrics(font);
    QRectF labelRect(QPointF(0.0, 0.0), metrics.size(Qt::TextSingleLine, valueText));
    labelRect.setWidth(std::max(labelRect.width(),
                                static_cast<qreal>(metrics.horizontalAdvance(domainText))));
    labelRect.setHeight(metrics.height() * 2 + 10.0);
    labelRect.setWidth(labelRect.width() + 14.0);

    qreal labelX = x + 10.0;
    if (labelX + labelRect.width() > rect.right()) {
        labelX = x - labelRect.width() - 10.0;
    }
    labelX = std::clamp(labelX, rect.left() + 2.0,
                        std::max(rect.left() + 2.0,
                                 rect.right() - labelRect.width() - 2.0));
    qreal labelY = y - labelRect.height() - 10.0;
    if (labelY < rect.top()) {
        labelY = y + 10.0;
    }
    labelY = std::clamp(labelY, rect.top() + 2.0,
                        std::max(rect.top() + 2.0, rect.bottom() - labelRect.height() - 2.0));
    labelRect.moveTo(labelX, labelY);

    QColor tooltip = _background.lighter(135);
    tooltip.setAlpha(235);
    painter->setBrush(tooltip);
    painter->setPen(QPen(_grid, 1));
    painter->drawRoundedRect(labelRect, 3.0, 3.0);
    painter->setPen(_axis.lighter(130));
    painter->drawText(labelRect.adjusted(7.0, 4.0, -7.0, -4.0),
                      Qt::AlignLeft | Qt::AlignVCenter, text);
}

void SignalPlotItem::drawCenteredMessage(QPainter* painter, const QRectF& rect,
                                         const QString& message) const {
    QFont font(QStringLiteral("Consolas"));
    font.setPixelSize(11);
    painter->setFont(font);
    painter->setPen(_axis);
    painter->drawText(rect, Qt::AlignCenter, message);
}

QString SignalPlotItem::numberLabel(double value) {
    if (!std::isfinite(value)) {
        return QStringLiteral("\u2014");
    }
    const double magnitude = std::abs(value);
    if ((magnitude >= 1.0e6) || (magnitude > 0.0 && magnitude < 1.0e-4)) {
        return QString::number(value, 'e', 3);
    }
    return QString::number(value, 'g', 6);
}
