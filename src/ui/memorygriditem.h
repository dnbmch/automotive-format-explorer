#pragma once

#include "models/memorymapmodel.h"
#include "ui/gridpalette.h"

#include <QQuickPaintedItem>
#include <QColor>
#include <QPointF>
#include <QPointer>

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
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionRangeChanged)
    Q_PROPERTY(quint64 selectionStart READ selectionStart NOTIFY selectionRangeChanged)
    Q_PROPERTY(quint64 selectionEnd READ selectionEnd NOTIFY selectionRangeChanged)

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

    // Click-drag byte-range selection. Addresses are absolute and inclusive.
    bool hasSelection() const;
    quint64 selectionStart() const;
    quint64 selectionEnd() const;
    Q_INVOKABLE void clearSelection();

    Q_INVOKABLE void setColors(const QVariantList& colors, const QColor& unoccupied);
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
    void selectionRangeChanged();
    void nodeKeyClicked(qulonglong nodeKey);

protected:
    void hoverMoveEvent(QHoverEvent* event) override;
    void hoverLeaveEvent(QHoverEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private slots:
    void onModelUpdated();
    void onLayoutChanged();

private:
    void onModelReplaced();
    int rowHeight() const;
    void rebuildObjectColors();
    void updateContentHeight();
    int objectIndexAtPixel(qreal px, qreal py) const;
    // Byte offset (from segment start) under a pixel, or -1 outside the grid.
    // When clamp is true, out-of-grid pixels snap to the nearest valid byte
    // (used while dragging a range selection).
    qint64 byteOffsetAtPixel(qreal px, qreal py, bool clamp) const;

    // Reads null once the model is destroyed, which counts as a replacement.
    QPointer<MemoryMapModel> _model;
    qreal _scroll_y = 0;
    int _cell_size = 18;
    int _cell_gap = 1;
    int _gutter_width = 90;
    int _hovered_obj = -1;
    int _selected_obj = -1;
    QPointF _mouse_pos;

    // Drag byte-range selection, as offsets from the segment start.
    // _sel_anchor is the pressed byte; -1 while no drag can start.
    // _sel_start/_sel_end (inclusive) hold the active selection; -1 = none.
    qint64 _sel_anchor = -1;
    qint64 _sel_start = -1;
    qint64 _sel_end = -1;

    // Encoded palette color per model row: (colorIndex | 0x10 alternate-shade
    // bit), or -1 for a row that claims no bytes. Assigned once per segment in
    // row order, so scrolling never changes an object's color. Which object a
    // byte shows comes from the model's queryBytes().
    std::vector<int8_t> _object_colors;

    GridPalette _palette;
    HighlightFlash _flash{[this] { update(); }};
};
