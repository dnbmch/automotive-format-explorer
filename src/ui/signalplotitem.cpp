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
    const SignalPlotModel::State state =
        _model ? _model->plotState() : SignalPlotModel::NoSignal;
    if (state != SignalPlotModel::Overview && state != SignalPlotModel::Detail) {
        QString message = QStringLiteral("Select a plottable channel");
        if (state != SignalPlotModel::NoSignal) {
            message = _model->message();
            if (state == SignalPlotModel::Pending && _model->progress() >= 0.0) {
                message += QStringLiteral(" %1%").arg(qRound(_model->progress() * 100.0));
            }
        }
        drawCenteredMessage(painter, rect, message);
        return;
    }

    painter->save();
    painter->setClipRect(rect.adjusted(-1.0, -1.0, 1.0, 1.0));
    if (state == SignalPlotModel::Detail) {
        drawSamples(painter, rect);
    } else {
        drawColumns(painter, rect);
    }
    painter->restore();
    drawCursor(painter, rect);
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
        for (auto change : {&SignalPlotModel::contentChanged, &SignalPlotModel::stateChanged,
                            &SignalPlotModel::busyChanged, &SignalPlotModel::viewChanged,
                            &SignalPlotModel::cursorChanged}) {
            connect(_model, change, this, [this] { update(); });
        }
        connect(_model, &QObject::destroyed, this, &SignalPlotItem::onModelReplaced);
    }
    onModelReplaced();
}

// A drag in progress belongs to the previous model and ends as a release would;
// the model's destruction lands here too.
void SignalPlotItem::onModelReplaced() {
    if (_panning) {
        _panning = false;
        setKeepMouseGrab(false);
        ungrabMouse();
        emit panningChanged();
    }
    emit modelChanged();
    update();
}

bool SignalPlotItem::panning() const {
    return _panning;
}

bool SignalPlotItem::interactive() const {
    return _model && _model->hasSamples();
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
    if (!interactive() || !plotRect().contains(event->position())) {
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
    if (event->button() != Qt::LeftButton || !interactive() ||
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

// The cursor reports the pixel column under the pointer: its exact sample, or
// the overview bins that column covers.
void SignalPlotItem::updateCursorAt(const QPointF& position) {
    if (!_model) {
        return;
    }
    const QRectF rect = plotRect();
    if (!_model->hasSamples() || !rect.contains(position)) {
        _model->clearCursor();
        return;
    }
    const double halfPixel = (_model->viewEnd() - _model->viewStart()) / rect.width() * 0.5;
    _model->setCursor(timeAtX(position.x()), halfPixel);
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
        painter->drawText(labelRect, Qt::AlignHCenter | Qt::AlignTop,
                          SignalPlotModel::numberText(value));
    }
    for (int tick = 0; tick <= yTickCount; ++tick) {
        const qreal y = rect.bottom() - rect.height() * tick / yTickCount;
        const double value = yMinimum + (yMaximum - yMinimum) * tick / yTickCount;
        painter->drawText(QRectF(2.0, y - 10.0, kLeftMargin - 8.0, 20.0),
                          Qt::AlignRight | Qt::AlignVCenter, SignalPlotModel::numberText(value));
    }

    painter->setPen(QPen(_axis, 1));
    painter->drawRect(rect);
    const QString domainLabel = _model
        ? labelWithUnit(_model->domainName(), _model->domainUnit())
        : QStringLiteral("Domain");
    painter->drawText(QRectF(rect.left(), rect.bottom() + 28.0, rect.width(), 16.0),
                      Qt::AlignHCenter | Qt::AlignTop, domainLabel);
}

// One vertical line per pixel column, from its minimum to its maximum. Columns
// stand alone: an overview's extrema are not consecutive samples.
void SignalPlotItem::drawColumns(QPainter* painter, const QRectF& rect) const {
    const std::vector<PlotColumn>& columns =
        _model->columns(static_cast<int>(std::ceil(rect.width())));
    std::vector<QLineF> lines;
    lines.reserve(columns.size());
    for (const PlotColumn& column : columns) {
        const qreal x = xForTime(column.domain, rect);
        lines.emplace_back(x, yForValue(column.minimum, rect), x, yForValue(column.maximum, rect));
    }

    QPen pen(_series_color, 1.0);
    pen.setCosmetic(true);
    pen.setCapStyle(Qt::RoundCap);
    painter->setPen(pen);
    if (!lines.empty()) {
        painter->drawLines(lines.data(), static_cast<int>(lines.size()));
    }
}

// Exact samples: a line through them while they are sparser than two per
// pixel, one sample past each edge so it enters and leaves the view. A
// nonfinite value breaks the line; a sample alone between breaks is a dot.
void SignalPlotItem::drawSamples(QPainter* painter, const QRectF& rect) const {
    const auto [first, last] = _model->visibleSampleRange();
    if (last - first > static_cast<std::size_t>(std::max(1.0, rect.width() * 2.0))) {
        drawColumns(painter, rect);
        return;
    }

    const PlotWindow& window = *_model->window();
    const std::size_t from = first > 0 ? first - 1 : 0;
    const std::size_t to = std::min(last + 1, window.time.size());
    QPen pen(_series_color, 1.5);
    pen.setCosmetic(true);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter->setPen(pen);

    QPainterPath path;
    std::size_t run = 0;
    QPointF lone;
    for (std::size_t i = from; i < to; ++i) {
        if (!std::isfinite(window.value[i])) {
            if (run == 1) {
                painter->drawPoint(lone);
            }
            run = 0;
            continue;
        }
        const QPointF point(xForTime(window.time[i], rect), yForValue(window.value[i], rect));
        if (run == 0) {
            path.moveTo(point);
        } else {
            path.lineTo(point);
        }
        lone = point;
        ++run;
    }
    if (run == 1) {
        painter->drawPoint(lone);
    }
    painter->drawPath(path);
}

// An exact sample gets a crosshair on its point. Overview bins get a band over
// the domain range they cover and a bar over their extrema, never a point: no
// single sample is claimed.
void SignalPlotItem::drawCursor(QPainter* painter, const QRectF& rect) const {
    if (!_model || !_model->cursorVisible()) {
        return;
    }
    const PlotCursor& cursor = _model->cursor();
    if (cursor.lastDomain < _model->viewStart() || cursor.firstDomain > _model->viewEnd()) {
        return;
    }

    qreal x = 0.0;
    qreal y = rect.center().y();
    if (cursor.exact) {
        x = xForTime(cursor.firstDomain, rect);
        const bool finite = std::isfinite(cursor.value);
        if (finite) {
            y = yForValue(cursor.value, rect);
        }
        QPen crosshair(_cursor_color, 1.0, Qt::DashLine);
        crosshair.setCosmetic(true);
        painter->setPen(crosshair);
        painter->drawLine(QPointF(x, rect.top()), QPointF(x, rect.bottom()));
        if (finite) {
            painter->drawLine(QPointF(rect.left(), y), QPointF(rect.right(), y));
            painter->setBrush(_cursor_color);
            painter->setPen(Qt::NoPen);
            painter->drawEllipse(QPointF(x, y), 3.5, 3.5);
        }
    } else {
        const qreal left = std::clamp(xForTime(cursor.firstDomain, rect), rect.left(), rect.right());
        const qreal right = std::clamp(xForTime(cursor.lastDomain, rect), rect.left(), rect.right());
        QColor band = _cursor_color;
        band.setAlpha(48);
        painter->fillRect(QRectF(QPointF(left, rect.top()),
                                 QPointF(std::max(right, left + 1.0), rect.bottom())),
                          band);
        x = (left + right) * 0.5;
        if (cursor.finiteCount > 0) {
            const qreal top = yForValue(cursor.maximum, rect);
            QPen extent(_cursor_color, 2.0);
            extent.setCosmetic(true);
            extent.setCapStyle(Qt::RoundCap);
            painter->setPen(extent);
            painter->drawLine(QPointF(x, top), QPointF(x, yForValue(cursor.minimum, rect)));
            y = top;
        }
    }

    const QString text = _model->cursorLines().join(QLatin1Char('\n'));
    QFont font(QStringLiteral("Consolas"));
    font.setPixelSize(10);
    painter->setFont(font);
    const QFontMetrics metrics(font);
    QRectF labelRect = metrics.boundingRect(QRect(), Qt::AlignLeft, text);
    labelRect.setSize(labelRect.size() + QSizeF(14.0, 10.0));

    qreal labelX = x + 10.0;
    if (labelX + labelRect.width() > rect.right()) {
        labelX = x - labelRect.width() - 10.0;
    }
    labelX = std::clamp(labelX, rect.left() + 2.0,
                        std::max(rect.left() + 2.0, rect.right() - labelRect.width() - 2.0));
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
    painter->drawText(labelRect.adjusted(7.0, 5.0, -7.0, -5.0), Qt::AlignLeft | Qt::AlignVCenter,
                      text);
}

void SignalPlotItem::drawCenteredMessage(QPainter* painter, const QRectF& rect,
                                         const QString& message) const {
    QFont font(QStringLiteral("Consolas"));
    font.setPixelSize(11);
    painter->setFont(font);
    painter->setPen(_axis);
    painter->drawText(rect, Qt::AlignCenter | Qt::TextWordWrap, message);
}
