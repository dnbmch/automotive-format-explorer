#include "ui/signalgriditem.h"

#include <QPainter>
#include <QHoverEvent>
#include <QMouseEvent>
#include <QKeyEvent>

SignalGridItem::SignalGridItem(QQuickItem* parent)
    : QQuickPaintedItem(parent) {
    setAcceptHoverEvents(true);
    setAcceptedMouseButtons(Qt::LeftButton);
    setRenderTarget(QQuickPaintedItem::FramebufferObject);
    setFlag(QQuickItem::ItemAcceptsInputMethod, true);
    setActiveFocusOnTab(true);

    _highlight_timer.setInterval(30);
    connect(&_highlight_timer, &QTimer::timeout, this, [this]() {
        _highlight_opacity -= 0.04;
        if (_highlight_opacity <= 0.0) {
            _highlight_opacity = 0.0;
            _highlight_sig = -1;
            _highlight_timer.stop();
        }
        update();
    });
}

int SignalGridItem::totalHeight() const {
    if (!_model) return 0;
    return kHeaderHeight + _model->dlcBytes() * rowHeight();
}

void SignalGridItem::paint(QPainter* painter) {
    if (!_model) return;

    if (_model->dlcBytes() == 0) {
        QFont monoFont(QStringLiteral("Consolas"), 0);
        monoFont.setPixelSize(11);
        painter->setFont(monoFont);
        painter->setPen(QColor(0x88, 0x88, 0x88));
        painter->drawText(QRectF(0, 0, width(), height()),
                          Qt::AlignCenter,
                          QStringLiteral("DLC = 0 — no payload"));
        return;
    }

    const int dlcBytes = _model->dlcBytes();
    const int rh = rowHeight();
    const int cs = kCellSize;
    const int gw = kGutterWidth;

    QFont monoFont(QStringLiteral("Consolas"), 0);
    monoFont.setPixelSize(10);
    painter->setFont(monoFont);

    // Column headers: bit 7 (MSB left) to bit 0 (LSB right).
    painter->setPen(QColor(0x88, 0x88, 0x88));
    for (int bit = 0; bit < kBitsPerRow; ++bit) {
        const qreal x = gw + bit * (cs + kCellGap);
        const int bitLabel = 7 - bit;
        painter->drawText(QRectF(x, 0, cs, kHeaderHeight),
                          Qt::AlignCenter,
                          QString::number(bitLabel));
    }

    // Rows: one per byte.
    for (int byteIdx = 0; byteIdx < dlcBytes; ++byteIdx) {
        const qreal y = kHeaderHeight + byteIdx * rh;

        // Byte index gutter.
        painter->setPen(QColor(0x88, 0x88, 0x88));
        painter->drawText(QRectF(0, y, gw - 4, cs),
                          Qt::AlignRight | Qt::AlignVCenter,
                          QStringLiteral("B%1").arg(byteIdx));

        // Bit cells.
        for (int col = 0; col < kBitsPerRow; ++col) {
            const qreal x = gw + col * (cs + kCellGap);
            const int displayBit = byteIdx * 8 + col;

            // Fill with color.
            if (displayBit < static_cast<int>(_color_map.size())) {
                int8_t ci = _color_map[displayBit];
                if (ci >= 0 && static_cast<size_t>(ci) < _palette.size()) {
                    painter->fillRect(QRectF(x, y, cs, cs), _palette[static_cast<size_t>(ci)]);
                } else {
                    painter->fillRect(QRectF(x, y, cs, cs), _unoccupied_color);
                }
            } else {
                painter->fillRect(QRectF(x, y, cs, cs), _unoccupied_color);
            }

            // Overlap stripe.
            if (_model->isOverlap(displayBit)) {
                painter->save();
                painter->setClipRect(QRectF(x, y, cs, cs));
                painter->setPen(QPen(QColor(255, 60, 60, 180), 1));
                for (int s = -cs; s < cs * 2; s += 6) {
                    painter->drawLine(QPointF(x + s, y),
                                      QPointF(x + s + cs, y + cs));
                }
                painter->restore();
            }

            // Selection border.
            if (_selected_sig >= 0 && displayBit < static_cast<int>(_color_map.size())) {
                int sigIdx = _model->signalAtBit(displayBit);
                if (sigIdx == _selected_sig) {
                    painter->setPen(QPen(QColor(255, 255, 255), 2));
                    painter->drawRect(QRectF(x, y, cs, cs));
                    painter->setPen(Qt::NoPen);
                }
            }

            // Highlight flash overlay.
            if (_highlight_sig >= 0 && _highlight_opacity > 0.0 &&
                displayBit < static_cast<int>(_color_map.size())) {
                int sigIdx = _model->signalAtBit(displayBit);
                if (sigIdx == _highlight_sig) {
                    QColor hl(255, 255, 255, static_cast<int>(_highlight_opacity * 160));
                    painter->setPen(QPen(hl, 2));
                    painter->drawRect(QRectF(x, y, cs, cs));
                    painter->setPen(Qt::NoPen);
                }
            }
        }

        // Byte separator line (thin line below each byte row except last).
        if (byteIdx < dlcBytes - 1) {
            const qreal lineY = y + cs + kCellGap * 0.5;
            painter->setPen(QPen(QColor(0x55, 0x55, 0x55), 1));
            painter->drawLine(QPointF(gw, lineY),
                              QPointF(gw + kBitsPerRow * (cs + kCellGap) - kCellGap, lineY));
        }
    }

    // Draw signal name labels inside the colored regions.
    // For each signal, find its bounding rectangle in the grid and draw the
    // name centered within its first byte row span.
    if (_model->signalCount() > 0) {
        QFont labelFont(QStringLiteral("Consolas"), 0);
        labelFont.setPixelSize(9);
        painter->setFont(labelFont);

        const auto& sigs = _model->data(_model->index(0, 0), SignalMapModel::NameRole);
        Q_UNUSED(sigs);

        for (int si = 0; si < _model->signalCount(); ++si) {
            const auto mi = _model->index(si, 0);
            const QString name = _model->data(mi, SignalMapModel::NameRole).toString();
            const bool isBE = _model->data(mi, SignalMapModel::BigEndianRole).toBool();

            // Find the first and last display bit for this signal.
            int firstBit = -1;
            int lastBit = -1;
            for (int b = 0; b < static_cast<int>(_color_map.size()); ++b) {
                if (_model->signalAtBit(b) == si) {
                    if (firstBit < 0) firstBit = b;
                    lastBit = b;
                }
            }
            if (firstBit < 0) continue;

            // Find first row span for label placement.
            int firstRow = firstBit / 8;
            int spanStart = firstBit;
            int spanEnd = firstBit;
            for (int b = firstBit; b <= lastBit && b / 8 == firstRow; ++b) {
                if (_model->signalAtBit(b) == si) {
                    spanEnd = b;
                }
            }

            int colStart = spanStart % 8;
            int colEnd = spanEnd % 8;

            const qreal lx = gw + colStart * (cs + kCellGap);
            const qreal ly = kHeaderHeight + firstRow * rh;
            const qreal lw = (colEnd - colStart + 1) * (cs + kCellGap) - kCellGap;

            // Truncate label to fit.
            QFontMetrics fm(painter->font());
            QString label = name;
            if (isBE) label += QStringLiteral(" \u2190"); // left arrow for BE
            QString elidedLabel = fm.elidedText(label, Qt::ElideRight, static_cast<int>(lw) - 2);

            painter->setPen(QColor(0xFF, 0xFF, 0xFF, 0xDD));
            painter->drawText(QRectF(lx + 1, ly, lw - 2, cs),
                              Qt::AlignCenter,
                              elidedLabel);
        }
    }
}

SignalMapModel* SignalGridItem::model() const {
    return _model;
}

void SignalGridItem::setModel(SignalMapModel* model) {
    if (_model == model) return;

    if (_model) {
        disconnect(_model, nullptr, this, nullptr);
    }

    _model = model;

    if (_model) {
        connect(_model, SIGNAL(currentMessageChanged()), this, SLOT(onModelUpdated()));
        connect(_model, SIGNAL(messagesChanged()), this, SLOT(onModelUpdated()));
        connect(_model, SIGNAL(muxGroupChanged()), this, SLOT(onModelUpdated()));
        rebuildColorMap();
    }

    emit modelChanged();
    update();
}

int SignalGridItem::hoveredSignalIndex() const {
    return _hovered_sig;
}

QString SignalGridItem::hoveredTooltip() const {
    if (_hovered_sig < 0 || !_model) return {};
    return _model->signalTooltip(_hovered_sig);
}

qreal SignalGridItem::mouseX() const { return _mouse_pos.x(); }
qreal SignalGridItem::mouseY() const { return _mouse_pos.y(); }

int SignalGridItem::selectedSignalIndex() const { return _selected_sig; }

void SignalGridItem::setSelectedSignalIndex(int index) {
    if (_selected_sig == index) return;
    _selected_sig = index;
    emit selectedSignalChanged();
    update();
}

void SignalGridItem::setColors(const QVariantList& colors, const QColor& unoccupied) {
    _palette.clear();
    _palette.resize(32);
    for (int i = 0; i < colors.size() && i < 8; ++i) {
        QColor base(colors[i].toString());
        _palette[static_cast<size_t>(i)] = base;
        _palette[static_cast<size_t>(i | 0x10)] = base.darker(130);
    }
    _unoccupied_color = unoccupied;
    update();
}

void SignalGridItem::highlightSignal(int signalIndex) {
    _highlight_sig = signalIndex;
    _highlight_opacity = 1.0;
    _highlight_timer.start();
    update();
}

void SignalGridItem::hoverMoveEvent(QHoverEvent* event) {
    _mouse_pos = event->position();
    emit mousePosChanged();

    int idx = signalIndexAtPixel(event->position().x(), event->position().y());
    if (idx != _hovered_sig) {
        _hovered_sig = idx;
        emit hoveredSignalChanged();
    }
}

void SignalGridItem::hoverLeaveEvent(QHoverEvent*) {
    if (_hovered_sig != -1) {
        _hovered_sig = -1;
        emit hoveredSignalChanged();
    }
}

void SignalGridItem::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;
    forceActiveFocus();

    int idx = signalIndexAtPixel(event->position().x(), event->position().y());
    if (idx >= 0) {
        setSelectedSignalIndex(idx);
        emit signalClicked(idx);
        if (_model) {
            quint64 key = _model->signalNodeKey(idx);
            if (key != 0) {
                emit nodeKeyClicked(key);
            }
        }
    }
    event->accept();
}

void SignalGridItem::onModelUpdated() {
    _selected_sig = -1;
    _hovered_sig = -1;
    rebuildColorMap();
    setImplicitHeight(totalHeight());
    update();
}

void SignalGridItem::rebuildColorMap() {
    _color_map.clear();
    if (!_model || _model->dlcBytes() == 0) return;

    const int totalBits = _model->dlcBits();
    _color_map.assign(totalBits, -1);

    const int sigCount = _model->signalCount();

    // Track shade alternation per color index (same logic as MemoryGridItem).
    int8_t shade[8] = {};
    int32_t lastSig[8] = {-1, -1, -1, -1, -1, -1, -1, -1};

    for (int si = 0; si < sigCount; ++si) {
        if (!_model->isSignalVisible(si)) continue;
        const auto mi = _model->index(si, 0);
        const int ci = _model->data(mi, SignalMapModel::ColorIndexRole).toInt();
        if (ci < 0 || ci >= 8) continue;

        if (lastSig[ci] != -1 && lastSig[ci] != si) {
            shade[ci] ^= 0x10;
        }
        lastSig[ci] = si;

        const auto encoded = static_cast<int8_t>(ci | shade[ci]);

        for (int b = 0; b < totalBits; ++b) {
            if (_model->signalAtBit(b) == si && _color_map[b] < 0) {
                _color_map[b] = encoded;
            }
        }
    }

    setImplicitHeight(totalHeight());
}

int SignalGridItem::signalIndexAtPixel(qreal px, qreal py) const {
    if (!_model || _model->dlcBytes() == 0) return -1;

    if (px < kGutterWidth || py < kHeaderHeight) return -1;

    const int col = static_cast<int>((px - kGutterWidth) / (kCellSize + kCellGap));
    const int row = static_cast<int>((py - kHeaderHeight) / rowHeight());

    if (col < 0 || col >= kBitsPerRow || row < 0 || row >= _model->dlcBytes()) {
        return -1;
    }

    const int displayBit = row * 8 + col;
    return _model->signalAtBit(displayBit);
}

void SignalGridItem::keyPressEvent(QKeyEvent* event) {
    if (!_model) {
        event->ignore();
        return;
    }

    switch (event->key()) {
    case Qt::Key_Tab:
    case Qt::Key_Right:
    case Qt::Key_Down:
        selectNextSignal(1);
        event->accept();
        break;
    case Qt::Key_Backtab:
    case Qt::Key_Left:
    case Qt::Key_Up:
        selectNextSignal(-1);
        event->accept();
        break;
    case Qt::Key_Home:
        // First message.
        if (_model->messageCount() > 0) {
            _model->setCurrentMessage(0);
        }
        event->accept();
        break;
    case Qt::Key_End:
        // Last message.
        if (_model->messageCount() > 0) {
            _model->setCurrentMessage(_model->messageCount() - 1);
        }
        event->accept();
        break;
    case Qt::Key_PageDown:
        // Next message.
        if (_model->currentMessage() + 1 < _model->messageCount()) {
            _model->setCurrentMessage(_model->currentMessage() + 1);
        }
        event->accept();
        break;
    case Qt::Key_PageUp:
        // Previous message.
        if (_model->currentMessage() > 0) {
            _model->setCurrentMessage(_model->currentMessage() - 1);
        }
        event->accept();
        break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Space:
        // Select/click the current signal.
        if (_selected_sig >= 0) {
            emit signalClicked(_selected_sig);
            quint64 key = _model->signalNodeKey(_selected_sig);
            if (key != 0) emit nodeKeyClicked(key);
        }
        event->accept();
        break;
    default:
        event->ignore();
        break;
    }
}

void SignalGridItem::selectNextSignal(int direction) {
    if (!_model || _model->signalCount() == 0) return;

    // Find next visible signal.
    int count = _model->signalCount();
    int start = _selected_sig;
    if (start < 0) start = (direction > 0) ? -1 : count;

    for (int i = 0; i < count; ++i) {
        start += direction;
        if (start < 0) start = count - 1;
        if (start >= count) start = 0;
        if (_model->isSignalVisible(start)) {
            setSelectedSignalIndex(start);
            highlightSignal(start);
            emit signalClicked(start);
            quint64 key = _model->signalNodeKey(start);
            if (key != 0) emit nodeKeyClicked(key);
            return;
        }
    }
}
