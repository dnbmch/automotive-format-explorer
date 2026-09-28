// The painted memory grid over the model's sparse tiles: an object gigabytes
// into a segment is reachable by scrolling, painted in its own color and
// selected by a click on its cell, and scrolling never recolors an object.
// Paints into an image; runs on the offscreen platform.

#include "models/memorymapmodel.h"
#include "ui/memorygriditem.h"

#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QSignalSpy>
#include <QTest>

#include <limits>
#include <memory>

namespace {

const QVariantList kColors = {
    QStringLiteral("#4a90d9"), QStringLiteral("#2bb3a0"), QStringLiteral("#9b59b6"),
    QStringLiteral("#5c6bc0"), QStringLiteral("#e67e22"), QStringLiteral("#26c6da"),
    QStringLiteral("#66bb6a"), QStringLiteral("#d4ac0d"),
};
const QColor kUnoccupied(0x33, 0x33, 0x33);

MemoryObject makeObject(const QString& name, uint64_t address, uint64_t size,
                        int colorIndex, quint64 nodeKey = 0) {
    MemoryObject obj;
    obj.name = name;
    obj.address = address;
    obj.size = size;
    obj.colorIndex = colorIndex;
    obj.nodeKey = nodeKey;
    return obj;
}

// Test access to the item's input handler.
class GridItem : public MemoryGridItem {
public:
    using MemoryGridItem::mouseMoveEvent;
    using MemoryGridItem::mousePressEvent;
};

void prepare(MemoryGridItem& item, MemoryMapModel& model) {
    item.setModel(&model);
    item.setColors(kColors, kUnoccupied);
    item.setSize(QSizeF(640, 400));
}

QImage render(MemoryGridItem& item) {
    QImage image(static_cast<int>(item.width()), static_cast<int>(item.height()),
                 QImage::Format_ARGB32);
    image.fill(Qt::black);
    QPainter painter(&image);
    item.paint(&painter);
    return image;
}

int rowStep(const MemoryGridItem& item) {
    return item.cellSize() + item.cellGap();
}

// Center of the cell showing the byte `offset` bytes into the segment.
QPoint cellCenter(const MemoryGridItem& item, const MemoryMapModel& model, uint64_t offset) {
    const auto bpr = static_cast<uint64_t>(model.bytesPerRow());
    const qreal y = static_cast<qreal>(offset / bpr) * rowStep(item) - item.scrollY();
    const int x = item.gutterWidth() + static_cast<int>(offset % bpr) * rowStep(item);
    return {x + item.cellSize() / 2, static_cast<int>(y) + item.cellSize() / 2};
}

void click(GridItem& item, QPoint at) {
    QMouseEvent press(QEvent::MouseButtonPress, at, at, Qt::LeftButton, Qt::LeftButton,
                      Qt::NoModifier);
    item.mousePressEvent(&press);
}

void drag(GridItem& item, QPoint from, QPoint to) {
    click(item, from);
    QMouseEvent move(QEvent::MouseMove, to, to, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    item.mouseMoveEvent(&move);
}

} // namespace

class TestMemoryGridItem : public QObject {
    Q_OBJECT

private slots:
    void objectBeyondSixteenMiBPainted();
    void farObjectPaintedAndSelectable();
    void colorsStableAcrossScrolling();
    void modelDestroyedBeforeItem();
};

void TestMemoryGridItem::objectBeyondSixteenMiBPainted() {
    // A 64 MiB segment, short enough for any row arithmetic: an object 48 MiB
    // in is painted and hit like one at the segment start.
    MemoryMapModel model;
    MemorySegmentInfo seg;
    seg.name = QStringLiteral("FLASH");
    seg.address = 0x80000000;
    seg.size = uint64_t(64) << 20;
    model.addSegment(seg);
    model.addObject(makeObject(QStringLiteral("Far"), 0x83000000, 16, 1, 44));
    model.finalize();

    GridItem item;
    prepare(item, model);
    item.setScrollY(static_cast<qreal>(model.rowForAddress(0x83000000)) * rowStep(item));
    const uint64_t far = 0x83000000 - seg.address;
    const QImage image = render(item);
    QCOMPARE(image.pixel(cellCenter(item, model, far)), QColor(kColors[1].toString()).rgb());
    QCOMPARE(image.pixel(cellCenter(item, model, far + 16)), kUnoccupied.rgb());

    QSignalSpy clicked(&item, &MemoryGridItem::nodeKeyClicked);
    click(item, cellCenter(item, model, far + 15));
    QCOMPARE(item.selectedObjectIndex(), 0);
    QCOMPARE(clicked.count(), 1);
    QCOMPARE(clicked.at(0).at(0).toULongLong(), qulonglong(44));
}

void TestMemoryGridItem::farObjectPaintedAndSelectable() {
    // A derived segment 3 GiB tall: its height exceeds the int range, and an
    // object and an alias overlapping it at the far end are shown and hit.
    MemoryMapModel model;
    model.addObject(makeObject(QStringLiteral("Low"), 0x00001000, 8, 0, 11));
    model.addObject(makeObject(QStringLiteral("High"), 0xC0001000, 16, 2, 22));
    model.addObject(makeObject(QStringLiteral("HighAlias"), 0xC0001008, 4, 6, 33));
    model.finalize();

    GridItem item;
    prepare(item, model);
    QCOMPARE(item.contentHeight(), static_cast<qreal>(model.totalRows()) * rowStep(item));
    QVERIFY(item.contentHeight() > std::numeric_limits<int>::max());

    const qreal target =
        static_cast<qreal>(model.rowForAddress(0xC0001000)) * rowStep(item) - 100;
    item.setScrollY(target);
    QCOMPARE(item.scrollY(), target);

    // Painted pixels compare as the image stores them, 8-bit RGB; a QColor
    // keeps finer components that the image quantizes away.
    const uint64_t high = 0xC0001000 - model.viewStartAddress();
    const QImage image = render(item);
    QCOMPARE(image.pixel(cellCenter(item, model, high)), QColor(kColors[2].toString()).rgb());
    QCOMPARE(image.pixel(cellCenter(item, model, high + 4)), QColor(kColors[2].toString()).rgb());
    QCOMPARE(image.pixel(cellCenter(item, model, high + 12)), QColor(kColors[2].toString()).rgb());
    QCOMPARE(image.pixel(cellCenter(item, model, high + 16)), kUnoccupied.rgb());

    QSignalSpy clicked(&item, &MemoryGridItem::nodeKeyClicked);
    click(item, cellCenter(item, model, high + 2));
    QCOMPARE(item.selectedObjectIndex(), 1);
    click(item, cellCenter(item, model, high + 9));
    QCOMPARE(item.selectedObjectIndex(), 2); // the alias owns the shared bytes
    click(item, cellCenter(item, model, high + 16));
    QCOMPARE(item.selectedObjectIndex(), -1);
    QCOMPARE(clicked.count(), 2);
    QCOMPARE(clicked.at(0).at(0).toULongLong(), qulonglong(22));
    QCOMPARE(clicked.at(1).at(0).toULongLong(), qulonglong(33));
}

void TestMemoryGridItem::colorsStableAcrossScrolling() {
    // Three same-colored objects alternate shades in address order: A plain,
    // B darker, C plain. Scrolled so that A is outside the painted rows, B and
    // C keep their shades; a shade assigned per painted range would restart at B.
    MemoryMapModel model;
    model.addObject(makeObject(QStringLiteral("A"), 0x1000, 16, 0));  // row 0
    model.addObject(makeObject(QStringLiteral("B"), 0x1010, 64, 0));  // rows 1-4
    model.addObject(makeObject(QStringLiteral("C"), 0x1050, 16, 0));  // row 5
    model.addObject(makeObject(QStringLiteral("Tail"), 0x5000, 4, 1)); // makes room to scroll
    model.finalize();

    GridItem item;
    prepare(item, model);
    const QColor plain(kColors[0].toString());
    const QColor darker = plain.darker(130);

    const QImage top = render(item);
    QCOMPARE(top.pixel(cellCenter(item, model, 0x00)), plain.rgb());
    QCOMPARE(top.pixel(cellCenter(item, model, 0x30)), darker.rgb());
    QCOMPARE(top.pixel(cellCenter(item, model, 0x50)), plain.rgb());

    item.setScrollY(3 * rowStep(item)); // painting starts at row 2, past A
    const QImage scrolled = render(item);
    QCOMPARE(scrolled.pixel(cellCenter(item, model, 0x30)), darker.rgb());
    QCOMPARE(scrolled.pixel(cellCenter(item, model, 0x50)), plain.rgb());
}

// A view can outlive its model: the item then holds no model, keeps no state
// naming the model's rows, paints nothing and ignores input.
void TestMemoryGridItem::modelDestroyedBeforeItem() {
    auto model = std::make_unique<MemoryMapModel>();
    model->addObject(makeObject(QStringLiteral("A"), 0x1000, 32, 0, 7));
    model->finalize();
    GridItem item;
    prepare(item, *model);
    const QPoint first = cellCenter(item, *model, 2);
    drag(item, first, cellCenter(item, *model, 20));
    QCOMPARE(item.selectedObjectIndex(), 0);
    QVERIFY(item.hasSelection());

    QSignalSpy changed(&item, &MemoryGridItem::modelChanged);
    QSignalSpy clicked(&item, &MemoryGridItem::nodeKeyClicked);
    model.reset();

    QVERIFY(!item.model());
    QCOMPARE(changed.count(), 1);
    QCOMPARE(item.selectedObjectIndex(), -1);
    QVERIFY(!item.hasSelection());
    QCOMPARE(item.selectionStart(), quint64(0));
    QCOMPARE(item.contentHeight(), 0.0);
    QVERIFY(item.hoveredTooltip().isEmpty());
    QCOMPARE(render(item).pixel(first), QColor(Qt::black).rgb());
    click(item, first);
    QCOMPARE(clicked.count(), 0);
    QCOMPARE(item.selectedObjectIndex(), -1);
}

QTEST_MAIN(TestMemoryGridItem)
#include "tst_memorygriditem.moc"
