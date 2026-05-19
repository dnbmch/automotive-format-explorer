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

    _highlight_timer.setInterval(30);
    connect(&_highlight_timer, &QTimer::timeout, this, [this]() {
        _highlight_opacity -= 0.04;
        if (_highlight_opacity <= 0.0) {
            _highlight_opacity = 0.0;
            _highlight_obj = -1;
            _highlight_timer.stop();
        }
        update();
    });
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

            if (mapIdx < _color_map.size()) {
                int8_t ci = _color_map[mapIdx];
                if (ci >= 0 && static_cast<size_t>(ci) < _palette.size()) {
                    painter->fillRect(QRectF(x, y, cs, cs), _palette[static_cast<size_t>(ci)]);
                } else {
                    painter->fillRect(QRectF(x, y, cs, cs), _unoccupied_color);
                }
            } else {
                painter->fillRect(QRectF(x, y, cs, cs), _unoccupied_color);
            }

            // Persistent selection border.
            if (_selected_obj >= 0 && mapIdx < _object_map.size() &&
                _object_map[mapIdx] == _selected_obj) {
                painter->setPen(QPen(QColor(255, 255, 255), 2));
                painter->drawRect(QRectF(x, y, cs, cs));
                painter->setPen(Qt::NoPen);
            }

            // Highlight flash overlay.
            if (_highlight_obj >= 0 && _highlight_opacity > 0.0 &&
                mapIdx < _object_map.size() && _object_map[mapIdx] == _highlight_obj) {
                QColor hl(255, 255, 255, static_cast<int>(_highlight_opacity * 160));
                painter->setPen(QPen(hl, 2));
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

    return QStringLiteral("%1\nType: %2  |  Address: %3  |  Size: %4")
        .arg(name, type, addrHex, sizeStr);
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

void MemoryGridItem::setColors(const QVariantList& colors, const QColor& unoccupied) {
    _palette.clear();
    // Build 16 entries: 0-7 = normal, 16-23 (stored as 8-15) = darker shade.
    // Index in _color_map uses bits 0-2 for color, bit 4 for shade.
    // We map: value & 0x0F < 8 → normal, value & 0x10 → alternate.
    _palette.resize(32); // indexed by raw encoded value
    for (int i = 0; i < colors.size() && i < 8; ++i) {
        QColor base(colors[i].toString());
        _palette[static_cast<size_t>(i)] = base;                          // normal: 0-7
        _palette[static_cast<size_t>(i | 0x10)] = base.darker(130);      // alternate: 16-23
    }
    _unoccupied_color = unoccupied;
    update();
}

int MemoryGridItem::rowForAddress(quint64 address) const {
    if (!_model) {
        return 0;
    }
    return _model->rowForAddress(address);
}

void MemoryGridItem::highlightObject(int objectIndex) {
    setSelectedObjectIndex(objectIndex);
    _highlight_obj = objectIndex;
    _highlight_opacity = 1.0;
    _highlight_timer.start();
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
    int idx = objectIndexAtPixel(event->position().x(), event->position().y());
    setSelectedObjectIndex(idx);
    if (idx >= 0) {
        emit objectClicked(idx);
        if (_model) {
            const auto mi = _model->index(idx, 0);
            const auto key = _model->data(mi, MemoryMapModel::NodeKeyRole).toULongLong();
            if (key != 0) {
                emit nodeKeyClicked(key);
            }
        }
    }
    event->accept();
}

void MemoryGridItem::onModelUpdated() {
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

    // Track shade toggle per color index: flips each time a new object
    // of the same color appears, so adjacent same-color objects alternate.
    // shade[colorIdx] = current shade bit (0 or 0x10).
    // lastObj[colorIdx] = last object index seen for that color.
    int8_t shade[8] = {};
    int32_t lastObj[8] = {-1, -1, -1, -1, -1, -1, -1, -1};

    const int objCount = _model->rowCount();
    for (int i = 0; i < objCount; ++i) {
        const auto mi = _model->index(i, 0);
        const auto addr = _model->data(mi, MemoryMapModel::AddressRole).toULongLong();
        const auto size = _model->data(mi, MemoryMapModel::SizeRole).toULongLong();
        const int ci = _model->data(mi, MemoryMapModel::ColorIndexRole).toInt();

        if (addr < segStart || size == 0 || ci < 0 || ci >= 8) {
            continue;
        }

        // Toggle shade when a different object of the same color appears.
        if (lastObj[ci] != -1 && lastObj[ci] != i) {
            shade[ci] ^= 0x10;
        }
        lastObj[ci] = i;

        const auto encoded = static_cast<int8_t>(ci | shade[ci]);

        const auto startOff = static_cast<size_t>(addr - segStart);
        const auto endOff = qMin(startOff + static_cast<size_t>(size), mapSize);

        for (size_t b = startOff; b < endOff; ++b) {
            _color_map[b] = encoded;
            _object_map[b] = static_cast<int32_t>(i);
        }
    }
}

void MemoryGridItem::updateContentHeight() {
    emit contentHeightChanged();
}

void MemoryGridItem::clampScrollY() {
    setScrollY(_scroll_y);
}

int MemoryGridItem::objectIndexAtPixel(qreal px, qreal py) const {
    if (!_model || _model->segmentCount() == 0 || _object_map.empty()) {
        return -1;
    }

    const int bpr = _model->bytesPerRow();
    const int rh = rowHeight();
    const int cs = _cell_size;
    const int cg = _cell_gap;
    const int gw = _gutter_width;

    if (px < gw) {
        return -1;
    }

    const int col = static_cast<int>((px - gw) / (cs + cg));
    if (col < 0 || col >= bpr) {
        return -1;
    }

    const int row = static_cast<int>((py + _scroll_y) / rh);
    if (row < 0 || row >= _model->totalRows()) {
        return -1;
    }

    const auto byteOffset = static_cast<size_t>(
        static_cast<uint64_t>(row) * static_cast<uint64_t>(bpr) + static_cast<uint64_t>(col));

    if (byteOffset >= _object_map.size()) {
        return -1;
    }

    return _object_map[byteOffset];
}
