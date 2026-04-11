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
    if (!model_ || model_->segmentCount() == 0 || colorMap_.empty()) {
        return;
    }

    const int bpr = model_->bytesPerRow();
    const int rh = rowHeight();
    const int cs = cellSize_;
    const int cg = cellGap_;
    const int gw = gutterWidth_;
    const uint64_t segStart = model_->viewStartAddress();
    const uint64_t segSize = model_->viewEndAddress() - segStart;
    const int totalRowCount = model_->totalRows();

    const int firstRow = qMax(0, static_cast<int>(scrollY_ / rh) - 1);
    const int visibleRows = static_cast<int>(height() / rh) + 3;
    const int lastRow = qMin(firstRow + visibleRows, totalRowCount);

    const qreal yOffset = -scrollY_;

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

            if (mapIdx < colorMap_.size()) {
                int8_t ci = colorMap_[mapIdx];
                if (ci >= 0 && static_cast<size_t>(ci) < palette_.size()) {
                    painter->fillRect(QRectF(x, y, cs, cs), palette_[static_cast<size_t>(ci)]);
                } else {
                    painter->fillRect(QRectF(x, y, cs, cs), unoccupiedColor_);
                }
            } else {
                painter->fillRect(QRectF(x, y, cs, cs), unoccupiedColor_);
            }
        }
    }
}

MemoryMapModel* MemoryGridItem::model() const {
    return model_;
}

void MemoryGridItem::setModel(MemoryMapModel* model) {
    if (model_ == model) {
        return;
    }

    if (model_) {
        disconnect(model_, nullptr, this, nullptr);
    }

    model_ = model;

    if (model_) {
        connect(model_, SIGNAL(currentSegmentChanged()), this, SLOT(onModelUpdated()));
        connect(model_, SIGNAL(bytesPerRowChanged()), this, SLOT(onLayoutChanged()));
        connect(model_, SIGNAL(objectsChanged()), this, SLOT(onModelUpdated()));
        rebuildColorMap();
        updateContentHeight();
    }

    emit modelChanged();
    update();
}

qreal MemoryGridItem::scrollY() const {
    return scrollY_;
}

void MemoryGridItem::setScrollY(qreal y) {
    y = qBound(0.0, y, qMax(0.0, contentHeight() - height()));
    if (qFuzzyCompare(scrollY_, y)) {
        return;
    }
    scrollY_ = y;
    emit scrollYChanged();
    update();
}

qreal MemoryGridItem::contentHeight() const {
    if (!model_) {
        return 0;
    }
    return model_->totalRows() * rowHeight();
}

int MemoryGridItem::cellSize() const { return cellSize_; }
void MemoryGridItem::setCellSize(int size) {
    if (cellSize_ == size) return;
    cellSize_ = size;
    emit cellSizeChanged();
    updateContentHeight();
    update();
}

int MemoryGridItem::cellGap() const { return cellGap_; }
void MemoryGridItem::setCellGap(int gap) {
    if (cellGap_ == gap) return;
    cellGap_ = gap;
    emit cellGapChanged();
    updateContentHeight();
    update();
}

int MemoryGridItem::gutterWidth() const { return gutterWidth_; }
void MemoryGridItem::setGutterWidth(int w) {
    if (gutterWidth_ == w) return;
    gutterWidth_ = w;
    emit gutterWidthChanged();
    update();
}

int MemoryGridItem::hoveredObjectIndex() const {
    return hoveredObj_;
}

QString MemoryGridItem::hoveredTooltip() const {
    if (hoveredObj_ < 0 || !model_) {
        return {};
    }

    const auto mi = model_->index(hoveredObj_, 0);
    const QString name = model_->data(mi, MemoryMapModel::NameRole).toString();
    const auto addr = model_->data(mi, MemoryMapModel::AddressRole).toULongLong();
    const auto size = model_->data(mi, MemoryMapModel::SizeRole).toULongLong();
    const QString type = model_->data(mi, MemoryMapModel::TypeNameRole).toString();
    const bool approx = model_->data(mi, MemoryMapModel::SizeApproximateRole).toBool();

    const QString addrHex = QStringLiteral("0x%1").arg(addr, 8, 16, QChar('0')).toUpper();
    const QString sizeStr = approx
        ? QStringLiteral("~%1 bytes (approx)").arg(size)
        : QStringLiteral("%1 bytes").arg(size);

    return QStringLiteral("%1\nType: %2  |  Address: %3  |  Size: %4")
        .arg(name, type, addrHex, sizeStr);
}

qreal MemoryGridItem::mouseX() const { return mousePos_.x(); }
qreal MemoryGridItem::mouseY() const { return mousePos_.y(); }

void MemoryGridItem::setColors(const QVariantList& colors, const QColor& unoccupied) {
    palette_.clear();
    // Build 16 entries: 0-7 = normal, 16-23 (stored as 8-15) = darker shade.
    // Index in colorMap_ uses bits 0-2 for color, bit 4 for shade.
    // We map: value & 0x0F < 8 → normal, value & 0x10 → alternate.
    palette_.resize(32); // indexed by raw encoded value
    for (int i = 0; i < colors.size() && i < 8; ++i) {
        QColor base(colors[i].toString());
        palette_[static_cast<size_t>(i)] = base;                          // normal: 0-7
        palette_[static_cast<size_t>(i | 0x10)] = base.darker(130);      // alternate: 16-23
    }
    unoccupiedColor_ = unoccupied;
    update();
}

int MemoryGridItem::rowForAddress(quint64 address) const {
    if (!model_) {
        return 0;
    }
    return model_->rowForAddress(address);
}

void MemoryGridItem::hoverMoveEvent(QHoverEvent* event) {
    mousePos_ = event->position();
    emit mousePosChanged();

    int idx = objectIndexAtPixel(event->position().x(), event->position().y());
    if (idx != hoveredObj_) {
        hoveredObj_ = idx;
        emit hoveredObjectChanged();
    }
}

void MemoryGridItem::hoverLeaveEvent(QHoverEvent*) {
    if (hoveredObj_ != -1) {
        hoveredObj_ = -1;
        emit hoveredObjectChanged();
    }
}

void MemoryGridItem::wheelEvent(QWheelEvent* event) {
    if (contentHeight() <= height()) {
        return;
    }
    const qreal delta = -event->angleDelta().y() / 120.0 * 3.0 * rowHeight();
    setScrollY(scrollY_ + delta);
    event->accept();
}

void MemoryGridItem::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        return;
    }
    int idx = objectIndexAtPixel(event->position().x(), event->position().y());
    if (idx >= 0) {
        emit objectClicked(idx);
        if (model_) {
            const auto mi = model_->index(idx, 0);
            const auto key = model_->data(mi, MemoryMapModel::NodeKeyRole).toULongLong();
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
    return cellSize_ + cellGap_;
}

void MemoryGridItem::rebuildColorMap() {
    colorMap_.clear();
    objectMap_.clear();

    if (!model_ || model_->segmentCount() == 0) {
        return;
    }

    const uint64_t segStart = model_->viewStartAddress();
    const uint64_t segEnd = model_->viewEndAddress();
    if (segEnd <= segStart) {
        return;
    }

    const auto segSize = static_cast<size_t>(segEnd - segStart);

    // Cap at 16MB to avoid absurd allocations.
    const size_t mapSize = qMin(segSize, size_t(16 * 1024 * 1024));
    colorMap_.assign(mapSize, -1);
    objectMap_.assign(mapSize, -1);

    // Track shade toggle per color index: flips each time a new object
    // of the same color appears, so adjacent same-color objects alternate.
    // shade[colorIdx] = current shade bit (0 or 0x10).
    // lastObj[colorIdx] = last object index seen for that color.
    int8_t shade[8] = {};
    int32_t lastObj[8] = {-1, -1, -1, -1, -1, -1, -1, -1};

    const int objCount = model_->rowCount();
    for (int i = 0; i < objCount; ++i) {
        const auto mi = model_->index(i, 0);
        const auto addr = model_->data(mi, MemoryMapModel::AddressRole).toULongLong();
        const auto size = model_->data(mi, MemoryMapModel::SizeRole).toULongLong();
        const int ci = model_->data(mi, MemoryMapModel::ColorIndexRole).toInt();

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
            colorMap_[b] = encoded;
            objectMap_[b] = static_cast<int32_t>(i);
        }
    }
}

void MemoryGridItem::updateContentHeight() {
    emit contentHeightChanged();
}

void MemoryGridItem::clampScrollY() {
    setScrollY(scrollY_);
}

int MemoryGridItem::objectIndexAtPixel(qreal px, qreal py) const {
    if (!model_ || model_->segmentCount() == 0 || objectMap_.empty()) {
        return -1;
    }

    const int bpr = model_->bytesPerRow();
    const int rh = rowHeight();
    const int cs = cellSize_;
    const int cg = cellGap_;
    const int gw = gutterWidth_;

    if (px < gw) {
        return -1;
    }

    const int col = static_cast<int>((px - gw) / (cs + cg));
    if (col < 0 || col >= bpr) {
        return -1;
    }

    const int row = static_cast<int>((py + scrollY_) / rh);
    if (row < 0 || row >= model_->totalRows()) {
        return -1;
    }

    const auto byteOffset = static_cast<size_t>(
        static_cast<uint64_t>(row) * static_cast<uint64_t>(bpr) + static_cast<uint64_t>(col));

    if (byteOffset >= objectMap_.size()) {
        return -1;
    }

    return objectMap_[byteOffset];
}
