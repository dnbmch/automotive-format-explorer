#include "ui/memorygriditem.h"
#include "models/memorymapmodel.h"

#include <QPainter>
#include <QHoverEvent>
#include <QWheelEvent>
#include <QMouseEvent>

MemoryGridItem::MemoryGridItem(QQuickItem* parent)
    : QQuickPaintedItem(parent) {
    setAcceptHoverEvents(true);
    setAcceptedMouseButtons(Qt::LeftButton);
    setRenderTarget(QQuickPaintedItem::FramebufferObject);
}

void MemoryGridItem::paint(QPainter* painter) {
    if (!_model || _model->segmentCount() == 0 || _color_map.empty()) {
        return;
    }

    const int bpr = _model->bytesPerRow();
    const int rh = rowHeight();
    const int cs = _cell_size;
    const int cg = _cell_gap;
    const int gw = _gutter_width;
    const uint64_t segStart = _model->viewStartAddress();
    const uint64_t segSize = _model->viewEndAddress() - segStart;
    const int totalRowCount = _model->totalRows();

    const int firstRow = qMax(0, static_cast<int>(_scroll_y / rh) - 1);
    const int visibleRows = static_cast<int>(height() / rh) + 3;
    const int lastRow = qMin(firstRow + visibleRows, totalRowCount);

    const qreal yOffset = -_scroll_y;

    QFont monoFont(QStringLiteral("Consolas"), 0);
    monoFont.setPixelSize(11);
    painter->setFont(monoFont);

    for (int row = firstRow; row < lastRow; ++row) {
        const qreal y = row * rh + yOffset;

        if (y + cs < 0 || y > height()) {
            continue;
        }

        const uint64_t rowAddr = static_cast<uint64_t>(row) * static_cast<uint64_t>(bpr);

        // Address gutter.
        painter->setPen(QColor(0x88, 0x88, 0x88));
        const uint64_t absAddr = segStart + rowAddr;
        const QString addrText = QStringLiteral("0x%1").arg(absAddr, 8, 16, QChar('0')).toUpper();
        painter->drawText(QRectF(4, y, gw - 8, cs), Qt::AlignLeft | Qt::AlignVCenter, addrText);

        // Byte cells.
        for (int col = 0; col < bpr; ++col) {
            const uint64_t byteOffset = rowAddr + static_cast<uint64_t>(col);
            if (byteOffset >= segSize) {
                break;
            }

            const qreal x = gw + col * (cs + cg);
            const auto mapIdx = static_cast<size_t>(byteOffset);

            const int8_t encoded = mapIdx < _color_map.size() ? _color_map[mapIdx] : int8_t(-1);
            painter->fillRect(QRectF(x, y, cs, cs), _palette.cellColor(encoded));

            // Overlap hatching: diagonal stripes on bytes claimed by more
            // than one object (matches the signal grid's overlap marker).
            if (_model->isOverlap(segStart + byteOffset)) {
                painter->save();
                painter->setClipRect(QRectF(x, y, cs, cs));
                painter->setPen(QPen(QColor(255, 60, 60, 180), 1));
                for (int s = -cs; s < cs * 2; s += 6) {
                    painter->drawLine(QPointF(x + s, y),
                                      QPointF(x + s + cs, y + cs));
                }
                painter->restore();
            }

            // Byte-range selection overlay.
            if (_sel_start >= 0 && static_cast<qint64>(byteOffset) >= _sel_start &&
                static_cast<qint64>(byteOffset) <= _sel_end) {
                painter->fillRect(QRectF(x, y, cs, cs), QColor(255, 255, 255, 70));
            }

            // Persistent selection border.
            if (_selected_obj >= 0 && mapIdx < _object_map.size() &&
                _object_map[mapIdx] == _selected_obj) {
                painter->setPen(QPen(QColor(255, 255, 255), 2));
                painter->drawRect(QRectF(x, y, cs, cs));
                painter->setPen(Qt::NoPen);
            }

            // Highlight flash overlay.
            if (mapIdx < _object_map.size() && _flash.activeFor(_object_map[mapIdx])) {
                painter->setPen(QPen(_flash.penColor(), 2));
                painter->drawRect(QRectF(x, y, cs, cs));
                painter->setPen(Qt::NoPen);
            }
        }
    }
}

MemoryMapModel* MemoryGridItem::model() const {
    return _model;
}

void MemoryGridItem::setModel(MemoryMapModel* model) {
    if (_model == model) {
        return;
    }

    if (_model) {
        disconnect(_model, nullptr, this, nullptr);
    }

    _model = model;

    if (_model) {
        connect(_model, SIGNAL(currentSegmentChanged()), this, SLOT(onModelUpdated()));
        connect(_model, SIGNAL(bytesPerRowChanged()), this, SLOT(onLayoutChanged()));
        connect(_model, SIGNAL(objectsChanged()), this, SLOT(onModelUpdated()));
        rebuildColorMap();
        updateContentHeight();
    }

    emit modelChanged();
    update();
}

qreal MemoryGridItem::scrollY() const {
    return _scroll_y;
}

void MemoryGridItem::setScrollY(qreal y) {
    y = qBound(0.0, y, qMax(0.0, contentHeight() - height()));
    if (qFuzzyCompare(_scroll_y, y)) {
        return;
    }
    _scroll_y = y;
    emit scrollYChanged();
    update();
}

qreal MemoryGridItem::contentHeight() const {
    if (!_model) {
        return 0;
    }
    return _model->totalRows() * rowHeight();
}

int MemoryGridItem::cellSize() const { return _cell_size; }
void MemoryGridItem::setCellSize(int size) {
    if (_cell_size == size) return;
    _cell_size = size;
    emit cellSizeChanged();
    updateContentHeight();
    update();
}

int MemoryGridItem::cellGap() const { return _cell_gap; }
void MemoryGridItem::setCellGap(int gap) {
    if (_cell_gap == gap) return;
    _cell_gap = gap;
    emit cellGapChanged();
    updateContentHeight();
    update();
}

int MemoryGridItem::gutterWidth() const { return _gutter_width; }
void MemoryGridItem::setGutterWidth(int w) {
    if (_gutter_width == w) return;
    _gutter_width = w;
    emit gutterWidthChanged();
    update();
}

int MemoryGridItem::hoveredObjectIndex() const {
    return _hovered_obj;
}

QString MemoryGridItem::hoveredTooltip() const {
    if (_hovered_obj < 0 || !_model) {
        return {};
    }

    const auto mi = _model->index(_hovered_obj, 0);
    const QString name = _model->data(mi, MemoryMapModel::NameRole).toString();
    const auto addr = _model->data(mi, MemoryMapModel::AddressRole).toULongLong();
    const auto size = _model->data(mi, MemoryMapModel::SizeRole).toULongLong();
    const QString type = _model->data(mi, MemoryMapModel::TypeNameRole).toString();
    const bool approx = _model->data(mi, MemoryMapModel::SizeApproximateRole).toBool();

    const QString addrHex = QStringLiteral("0x%1").arg(addr, 8, 16, QChar('0')).toUpper();
    const QString sizeStr = approx
        ? QStringLiteral("~%1 bytes (approx)").arg(size)
        : QStringLiteral("%1 bytes").arg(size);

    QString tip = QStringLiteral("%1\nType: %2  |  Address: %3  |  Size: %4")
        .arg(name, type, addrHex, sizeStr);

    const QString layout = _model->data(mi, MemoryMapModel::RecordLayoutRole).toString();
    if (!layout.isEmpty()) {
        tip += QStringLiteral("\nRecord Layout: %1").arg(layout);
    }
    const QString conversion = _model->data(mi, MemoryMapModel::ConversionRole).toString();
    if (!conversion.isEmpty()) {
        tip += QStringLiteral("\nConversion: %1").arg(conversion);
    }
    return tip;
}

qreal MemoryGridItem::mouseX() const { return _mouse_pos.x(); }
qreal MemoryGridItem::mouseY() const { return _mouse_pos.y(); }

int MemoryGridItem::selectedObjectIndex() const { return _selected_obj; }
void MemoryGridItem::setSelectedObjectIndex(int index) {
    if (_selected_obj == index) return;
    _selected_obj = index;
    emit selectedObjectChanged();
    update();
}

bool MemoryGridItem::hasSelection() const { return _sel_start >= 0; }

quint64 MemoryGridItem::selectionStart() const {
    return _sel_start >= 0 ? _model->viewStartAddress() + static_cast<quint64>(_sel_start) : 0;
}

quint64 MemoryGridItem::selectionEnd() const {
    return _sel_end >= 0 ? _model->viewStartAddress() + static_cast<quint64>(_sel_end) : 0;
}

void MemoryGridItem::clearSelection() {
    _sel_anchor = -1;
    if (_sel_start < 0) {
        return;
    }
    _sel_start = -1;
    _sel_end = -1;
    emit selectionRangeChanged();
    update();
}

void MemoryGridItem::setColors(const QVariantList& colors, const QColor& unoccupied) {
    _palette.setColors(colors, unoccupied);
    update();
}

void MemoryGridItem::highlightObject(int objectIndex) {
    setSelectedObjectIndex(objectIndex);
    _flash.start(objectIndex);
    update();
}

void MemoryGridItem::hoverMoveEvent(QHoverEvent* event) {
    _mouse_pos = event->position();
    emit mousePosChanged();

    int idx = objectIndexAtPixel(event->position().x(), event->position().y());
    if (idx != _hovered_obj) {
        _hovered_obj = idx;
        emit hoveredObjectChanged();
    }
}

void MemoryGridItem::hoverLeaveEvent(QHoverEvent*) {
    if (_hovered_obj != -1) {
        _hovered_obj = -1;
        emit hoveredObjectChanged();
    }
}

void MemoryGridItem::wheelEvent(QWheelEvent* event) {
    if (contentHeight() <= height()) {
        return;
    }
    const qreal delta = -event->angleDelta().y() / 120.0 * 3.0 * rowHeight();
    setScrollY(_scroll_y + delta);
    event->accept();
}

void MemoryGridItem::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        return;
    }
    clearSelection();
    _sel_anchor = byteOffsetAtPixel(event->position().x(), event->position().y(), false);

    int idx = objectIndexAtPixel(event->position().x(), event->position().y());
    setSelectedObjectIndex(idx);
    if (idx >= 0) {
        const auto mi = _model->index(idx, 0);
        const auto key = _model->data(mi, MemoryMapModel::NodeKeyRole).toULongLong();
        if (key != 0) {
            emit nodeKeyClicked(key);
        }
    }
    event->accept();
}

void MemoryGridItem::mouseMoveEvent(QMouseEvent* event) {
    if (_sel_anchor < 0) {
        return;
    }
    const qint64 off = byteOffsetAtPixel(event->position().x(), event->position().y(), true);
    // A drag starts once the pointer leaves the anchor byte; from then on the
    // range tracks the pointer (down to a single byte again).
    if (_sel_start < 0 && off == _sel_anchor) {
        return;
    }
    const qint64 start = qMin(_sel_anchor, off);
    const qint64 end = qMax(_sel_anchor, off);
    if (start == _sel_start && end == _sel_end) {
        return;
    }
    _sel_start = start;
    _sel_end = end;
    emit selectionRangeChanged();
    update();
    event->accept();
}

void MemoryGridItem::onModelUpdated() {
    clearSelection();
    rebuildColorMap();
    updateContentHeight();
    update();
}

void MemoryGridItem::onLayoutChanged() {
    updateContentHeight();
    update();
}

int MemoryGridItem::rowHeight() const {
    return _cell_size + _cell_gap;
}

void MemoryGridItem::rebuildColorMap() {
    _color_map.clear();
    _object_map.clear();

    if (!_model || _model->segmentCount() == 0) {
        return;
    }

    const uint64_t segStart = _model->viewStartAddress();
    const uint64_t segEnd = _model->viewEndAddress();
    if (segEnd <= segStart) {
        return;
    }

    const auto segSize = static_cast<size_t>(segEnd - segStart);

    // Cap at 16MB to avoid absurd allocations.
    const size_t mapSize = qMin(segSize, size_t(16 * 1024 * 1024));
    _color_map.assign(mapSize, -1);
    _object_map.assign(mapSize, -1);

    ShadeCycler shader;

    const int objCount = _model->rowCount();
    for (int i = 0; i < objCount; ++i) {
        const auto mi = _model->index(i, 0);
        const auto addr = _model->data(mi, MemoryMapModel::AddressRole).toULongLong();
        const auto size = _model->data(mi, MemoryMapModel::SizeRole).toULongLong();
        const int ci = _model->data(mi, MemoryMapModel::ColorIndexRole).toInt();

        if (size == 0 || ci < 0 || ci >= 8) {
            continue;
        }

        const int8_t encoded = shader.encode(ci, i);

        // The model's segment filter includes objects straddling the segment
        // start (end reaches into the segment); paint them clipped, matching
        // the overlap map.
        const auto span = clampedByteSpan(addr, size, segStart, mapSize);
        for (size_t b = static_cast<size_t>(span.first); b < static_cast<size_t>(span.last); ++b) {
            _color_map[b] = encoded;
            _object_map[b] = static_cast<int32_t>(i);
        }
    }
}

void MemoryGridItem::updateContentHeight() {
    emit contentHeightChanged();
}

int MemoryGridItem::objectIndexAtPixel(qreal px, qreal py) const {
    const qint64 off = byteOffsetAtPixel(px, py, false);
    if (off < 0 || static_cast<size_t>(off) >= _object_map.size()) {
        return -1;
    }
    return _object_map[static_cast<size_t>(off)];
}

qint64 MemoryGridItem::byteOffsetAtPixel(qreal px, qreal py, bool clamp) const {
    if (!_model || _model->segmentCount() == 0) {
        return -1;
    }

    const int bpr = _model->bytesPerRow();
    const int rh = rowHeight();
    const int cs = _cell_size;
    const int cg = _cell_gap;
    const int gw = _gutter_width;

    int col = static_cast<int>((px - gw) / (cs + cg));
    int row = static_cast<int>((py + _scroll_y) / rh);

    if (clamp) {
        col = qBound(0, col, bpr - 1);
        row = qBound(0, row, _model->totalRows() - 1);
    } else if (px < gw || col < 0 || col >= bpr || row < 0 || row >= _model->totalRows()) {
        return -1;
    }

    const uint64_t byteOffset =
        static_cast<uint64_t>(row) * static_cast<uint64_t>(bpr) + static_cast<uint64_t>(col);
    const uint64_t segSize = _model->viewEndAddress() - _model->viewStartAddress();

    if (byteOffset >= segSize) {
        return clamp ? static_cast<qint64>(segSize) - 1 : -1;
    }
    return static_cast<qint64>(byteOffset);
}
