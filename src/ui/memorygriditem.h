#pragma once

#include "models/memorymapmodel.h"

#include <QQuickPaintedItem>
#include <QColor>
#include <QPointF>
#include <QTimer>

#include <vector>

class MemoryGridItem : public QQuickPaintedItem {
    Q_OBJECT

    Q_PROPERTY(MemoryMapModel* model READ model WRITE setModel NOTIFY modelChanged)
    Q_PROPERTY(qreal scrollY READ scrollY WRITE setScrollY NOTIFY scrollYChanged)
    Q_PROPERTY(qreal contentHeight READ contentHeight NOTIFY contentHeightChanged)
    Q_PROPERTY(int cellSize READ cellSize WRITE setCellSize NOTIFY cellSizeChanged)
    Q_PROPERTY(int cellGap READ cellGap WRITE setCellGap NOTIFY cellGapChanged)
    Q_PROPERTY(int gutterWidth READ gutterWidth WRITE setGutterWidth NOTIFY gutterWidthChanged)
    Q_PROPERTY(int hoveredObjectIndex READ hoveredObjectIndex NOTIFY hoveredObjectChanged)
    Q_PROPERTY(QString hoveredTooltip READ hoveredTooltip NOTIFY hoveredObjectChanged)
    Q_PROPERTY(qreal mouseX READ mouseX NOTIFY mousePosChanged)
    Q_PROPERTY(qreal mouseY READ mouseY NOTIFY mousePosChanged)
    Q_PROPERTY(int selectedObjectIndex READ selectedObjectIndex WRITE setSelectedObjectIndex
               NOTIFY selectedObjectChanged)

public:
    explicit MemoryGridItem(QQuickItem* parent = nullptr);

    void paint(QPainter* painter) override;

    MemoryMapModel* model() const;
    void setModel(MemoryMapModel* model);

    qreal scrollY() const;
    void setScrollY(qreal y);

    qreal contentHeight() const;

    int cellSize() const;
    void setCellSize(int size);
    int cellGap() const;
    void setCellGap(int gap);
    int gutterWidth() const;
    void setGutterWidth(int w);

    int hoveredObjectIndex() const;
    QString hoveredTooltip() const;

    qreal mouseX() const;
    qreal mouseY() const;

    int selectedObjectIndex() const;
    void setSelectedObjectIndex(int index);

    Q_INVOKABLE void setColors(const QVariantList& colors, const QColor& unoccupied);
    Q_INVOKABLE int rowForAddress(quint64 address) const;
    Q_INVOKABLE void highlightObject(int objectIndex);

signals:
    void modelChanged();
    void scrollYChanged();
    void contentHeightChanged();
    void cellSizeChanged();
    void cellGapChanged();
    void gutterWidthChanged();
    void hoveredObjectChanged();
    void mousePosChanged();
    void selectedObjectChanged();
    void objectClicked(int objectIndex);
    void nodeKeyClicked(qulonglong nodeKey);

protected:
    void hoverMoveEvent(QHoverEvent* event) override;
    void hoverLeaveEvent(QHoverEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private slots:
    void onModelUpdated();
    void onLayoutChanged();

private:
    int rowHeight() const;
    void rebuildColorMap();
    void updateContentHeight();
    int objectIndexAtPixel(qreal px, qreal py) const;
    void clampScrollY();

    MemoryMapModel* model_ = nullptr;
    qreal scrollY_ = 0;
    int cellSize_ = 18;
    int cellGap_ = 1;
    int gutterWidth_ = 90;
    int hoveredObj_ = -1;
    int selectedObj_ = -1;
    QPointF mousePos_;

    // Pre-computed flat color map: one int8 per byte in the segment.
    // -1 = unoccupied, 0-7 = color index.
    std::vector<int8_t> colorMap_;
    // Parallel map: object index per byte (-1 = none).
    std::vector<int32_t> objectMap_;

    std::vector<QColor> palette_;
    QColor unoccupiedColor_{0x33, 0x33, 0x33};

    // Highlight flash state.
    int highlightObj_ = -1;
    qreal highlightOpacity_ = 0.0;
    QTimer highlightTimer_;
};
