#include "ui/memorygriditem.h"
#include "models/memorymapmodel.h"

#include <QPainter>
#include <QHoverEvent>
#include <QWheelEvent>
#include <QMouseEvent>

#include <cmath>

MemoryGridItem::MemoryGridItem(QQuickItem* parent)
    : QQuickPaintedItem(parent) {
    setAcceptHoverEvents(true);
    setAcceptedMouseButtons(Qt::LeftButton);
    setRenderTarget(QQuickPaintedItem::FramebufferObject);
}

void MemoryGridItem::paint(QPainter* painter) {
    if (!_model || _model->segmentCount() == 0) {
        return;
    }

    const int bpr = _model->bytesPerRow();
    const int rh = rowHeight();
    const int cs = _cell_size;
    const int cg = _cell_gap;
    const int gw = _gutter_width;
    const uint64_t segStart = _model->viewStartAddress();
    const uint64_t segSize = _model->viewEndAddress() - segStart;
    const uint64_t totalRowCount = _model->totalRows();

    const auto firstRow = static_cast<uint64_t>(qMax(0.0, std::floor(_scroll_y / rh) - 1.0));
    const uint64_t lastRow =
        qMin<uint64_t>(firstRow + static_cast<uint64_t>(height() / rh) + 3, totalRowCount);
    if (firstRow >= lastRow) {
        return;
    }

    // One tile holds every visible byte; the segment is never mapped whole.
    const uint64_t firstByte = firstRow * static_cast<uint64_t>(bpr);
    const uint64_t endByte =
        lastRow == totalRowCount ? segSize : lastRow * static_cast<uint64_t>(bpr);
    const MemoryTile tile = _model->queryBytes(segStart + firstByte, endByte - firstByte);

    QFont monoFont(QStringLiteral("Consolas"), 0);
    monoFont.setPixelSize(11);
    painter->setFont(monoFont);

    for (uint64_t row = firstRow; row < lastRow; ++row) {
        const qreal y = static_cast<qreal>(row) * rh - _scroll_y;

        if (y + cs < 0 || y > height()) {
            continue;
        }

        const uint64_t rowAddr = row * static_cast<uint64_t>(bpr);

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
            const auto k = static_cast<size_t>(byteOffset - firstByte);
            const int32_t owner = tile.owner[k];

            painter->fillRect(QRectF(x, y, cs, cs),
                              _palette.cellColor(owner >= 0
                                                     ? _object_colors[static_cast<size_t>(owner)]
                                                     : int8_t(-1)));

            // Overlap hatching: diagonal stripes on bytes claimed by more
            // than one object (matches the signal grid's overlap marker).
            if (tile.overlap[k] != 0) {
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
            if (_selected_obj >= 0 && owner == _selected_obj) {
                painter->setPen(QPen(QColor(255, 255, 255), 2));
                painter->drawRect(QRectF(x, y, cs, cs));
                painter->setPen(Qt::NoPen);
            }

            // Highlight flash overlay.
            if (_flash.activeFor(owner)) {
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
        rebuildObjectColors();
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
    return static_cast<qreal>(_model->totalRows()) * rowHeight();
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
    rebuildObjectColors();
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

void MemoryGridItem::rebuildObjectColors() {
    _object_colors.clear();
    if (!_model) {
        return;
    }

    // Same-colored objects alternate shades in row (address) order; a row of
    // unknown size claims no bytes and takes no shade.
    ShadeCycler shader;
    const int objCount = _model->rowCount();
    _object_colors.reserve(static_cast<size_t>(objCount));
    for (int i = 0; i < objCount; ++i) {
        const auto mi = _model->index(i, 0);
        const auto size = _model->data(mi, MemoryMapModel::SizeRole).toULongLong();
        const int ci = _model->data(mi, MemoryMapModel::ColorIndexRole).toInt();
        _object_colors.push_back(size == 0 ? int8_t(-1) : shader.encode(ci, i));
    }
}

void MemoryGridItem::updateContentHeight() {
    emit contentHeightChanged();
}

int MemoryGridItem::objectIndexAtPixel(qreal px, qreal py) const {
    const qint64 off = byteOffsetAtPixel(px, py, false);
    if (off < 0) {
        return -1;
    }
    return _model->objectAtAddress(_model->viewStartAddress() + static_cast<quint64>(off));
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
    const auto rows = static_cast<qint64>(_model->totalRows());

    auto col = static_cast<qint64>(std::floor((px - gw) / (cs + cg)));
    auto row = static_cast<qint64>(std::floor((py + _scroll_y) / rh));

    if (clamp) {
        col = qBound(qint64(0), col, qint64(bpr) - 1);
        row = qBound(qint64(0), row, rows - 1);
    } else if (px < gw || col < 0 || col >= bpr || row < 0 || row >= rows) {
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
