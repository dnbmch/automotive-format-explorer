#pragma once

#include "models/signalmapmodel.h"

#include <QQuickPaintedItem>
#include <QColor>
#include <QPointF>
#include <QTimer>

#include <vector>

class SignalGridItem : public QQuickPaintedItem {
    Q_OBJECT

    Q_PROPERTY(SignalMapModel* model READ model WRITE setModel NOTIFY modelChanged)
    Q_PROPERTY(int hoveredSignalIndex READ hoveredSignalIndex NOTIFY hoveredSignalChanged)
    Q_PROPERTY(QString hoveredTooltip READ hoveredTooltip NOTIFY hoveredSignalChanged)
    Q_PROPERTY(qreal mouseX READ mouseX NOTIFY mousePosChanged)
    Q_PROPERTY(qreal mouseY READ mouseY NOTIFY mousePosChanged)
    Q_PROPERTY(int selectedSignalIndex READ selectedSignalIndex WRITE setSelectedSignalIndex
               NOTIFY selectedSignalChanged)

public:
    explicit SignalGridItem(QQuickItem* parent = nullptr);

    void paint(QPainter* painter) override;

    SignalMapModel* model() const;
    void setModel(SignalMapModel* model);

    int hoveredSignalIndex() const;
    QString hoveredTooltip() const;

    qreal mouseX() const;
    qreal mouseY() const;

    int selectedSignalIndex() const;
    void setSelectedSignalIndex(int index);

    Q_INVOKABLE void setColors(const QVariantList& colors, const QColor& unoccupied);
    Q_INVOKABLE void highlightSignal(int signalIndex);

signals:
    void modelChanged();
    void hoveredSignalChanged();
    void mousePosChanged();
    void selectedSignalChanged();
    void signalClicked(int signalIndex);
    void nodeKeyClicked(qulonglong nodeKey);

protected:
    void hoverMoveEvent(QHoverEvent* event) override;
    void hoverLeaveEvent(QHoverEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private slots:
    void onModelUpdated();

private:
    int signalIndexAtPixel(qreal px, qreal py) const;
    void rebuildColorMap();
    void selectNextSignal(int direction);

    // Layout constants.
    static constexpr int kCellSize = 28;
    static constexpr int kCellGap = 1;
    static constexpr int kGutterWidth = 40;
    static constexpr int kHeaderHeight = 20;
    static constexpr int kBitsPerRow = 8;

    int rowHeight() const { return kCellSize + kCellGap; }
    int totalHeight() const;

    SignalMapModel* _model = nullptr;
    int _hovered_sig = -1;
    int _selected_sig = -1;
    QPointF _mouse_pos;

    // _color_map[displayBit]: encoded color index or -1.
    std::vector<int8_t> _color_map;

    std::vector<QColor> _palette;
    QColor _unoccupied_color{0x33, 0x33, 0x33};

    // Highlight flash state.
    int _highlight_sig = -1;
    qreal _highlight_opacity = 0.0;
    QTimer _highlight_timer;
};
