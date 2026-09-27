#pragma once

#include <QColor>
#include <QTimer>
#include <QVariantList>

#include <array>
#include <functional>
#include <vector>

// Shared styling helpers for the QQuickPaintedItem grid renderers
// (MemoryGridItem / SignalGridItem).
//
// Cell color encoding: -1 = unoccupied, otherwise (colorIndex 0-7 | 0x10
// alternate-shade bit) indexing the 32-entry palette.

class GridPalette {
public:
    void setColors(const QVariantList& colors, const QColor& unoccupied) {
        _palette.clear();
        _palette.resize(32);
        for (int i = 0; i < colors.size() && i < 8; ++i) {
            QColor base(colors[i].toString());
            _palette[static_cast<size_t>(i)] = base;                      // normal: 0-7
            _palette[static_cast<size_t>(i | 0x10)] = base.darker(130);  // alternate: 16-23
        }
        _unoccupied = unoccupied;
    }

    // Cell fill for an encoded color-map value; -1 or out-of-range → unoccupied.
    QColor cellColor(int8_t encoded) const {
        if (encoded >= 0 && static_cast<size_t>(encoded) < _palette.size()) {
            return _palette[static_cast<size_t>(encoded)];
        }
        return _unoccupied;
    }

private:
    std::vector<QColor> _palette;
    QColor _unoccupied{0x33, 0x33, 0x33};
};

// Alternating-shade encoder for color-map rebuilds: flips the shade bit each
// time a different object with the same color index appears, so adjacent
// same-color objects stay distinguishable.
class ShadeCycler {
public:
    int8_t encode(int colorIndex, int32_t objectIndex) {
        auto ci = static_cast<size_t>(colorIndex);
        if (_last[ci] != -1 && _last[ci] != objectIndex) {
            _shade[ci] ^= 0x10;
        }
        _last[ci] = objectIndex;
        return static_cast<int8_t>(colorIndex | _shade[ci]);
    }

private:
    std::array<int8_t, 8> _shade{};
    std::array<int32_t, 8> _last{-1, -1, -1, -1, -1, -1, -1, -1};
};

// Fading white flash around one object/signal, driven by a 30 ms timer.
// requestUpdate is invoked every fade tick (the owner's update()).
class HighlightFlash {
public:
    explicit HighlightFlash(std::function<void()> requestUpdate)
        : _request_update(std::move(requestUpdate)) {
        _timer.setInterval(30);
        QObject::connect(&_timer, &QTimer::timeout, &_timer, [this]() {
            _opacity -= 0.04;
            if (_opacity <= 0.0) {
                _opacity = 0.0;
                _index = -1;
                _timer.stop();
            }
            _request_update();
        });
    }

    void start(int index) {
        _index = index;
        _opacity = 1.0;
        _timer.start();
    }

    void stop() {
        _index = -1;
        _opacity = 0.0;
        _timer.stop();
    }

    bool activeFor(int index) const {
        return _index >= 0 && _index == index && _opacity > 0.0;
    }

    QColor penColor() const {
        return QColor(255, 255, 255, static_cast<int>(_opacity * 160));
    }

private:
    std::function<void()> _request_update;
    QTimer _timer;
    int _index = -1;
    qreal _opacity = 0.0;
};
