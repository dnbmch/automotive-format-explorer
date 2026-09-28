// The painted signal grid against a model that goes away first, as a replaced
// center view does. Paints into an image; runs on the offscreen platform.

#include "models/signalmapmodel.h"
#include "ui/signalgriditem.h"

#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QSignalSpy>
#include <QTest>

#include <memory>

namespace {

const QVariantList kColors = {
    QStringLiteral("#4a90d9"), QStringLiteral("#2bb3a0"), QStringLiteral("#9b59b6"),
    QStringLiteral("#5c6bc0"), QStringLiteral("#e67e22"), QStringLiteral("#26c6da"),
    QStringLiteral("#66bb6a"), QStringLiteral("#d4ac0d"),
};

// Test access to the item's input handler.
class GridItem : public SignalGridItem {
public:
    using SignalGridItem::mousePressEvent;
};

// One eight-byte message whose single signal covers every bit.
std::unique_ptr<SignalMapModel> oneSignalMessage() {
    auto model = std::make_unique<SignalMapModel>();
    SignalEntry signal;
    signal.name = QStringLiteral("Payload");
    signal.bitLength = 64;
    signal.nodeKey = 2;
    MessageEntry message;
    message.name = QStringLiteral("Frame");
    message.id = 0x100;
    message.dlc = 8;
    message.nodeKey = 1;
    message.signalEntries.push_back(signal);
    model->addMessage(std::move(message));
    model->finalize();
    return model;
}

QImage render(SignalGridItem& item) {
    QImage image(static_cast<int>(item.width()), static_cast<int>(item.height()),
                 QImage::Format_ARGB32);
    image.fill(Qt::black);
    QPainter painter(&image);
    item.paint(&painter);
    return image;
}

void click(GridItem& item, QPoint at) {
    QMouseEvent press(QEvent::MouseButtonPress, at, at, Qt::LeftButton, Qt::LeftButton,
                      Qt::NoModifier);
    item.mousePressEvent(&press);
}

} // namespace

class TestSignalGridItem : public QObject {
    Q_OBJECT

private slots:
    void modelDestroyedBeforeItem();
};

// The item then holds no model and no signal of it, paints nothing and ignores
// input.
void TestSignalGridItem::modelDestroyedBeforeItem() {
    auto model = oneSignalMessage();
    GridItem item;
    item.setModel(model.get());
    item.setColors(kColors, QColor(0x33, 0x33, 0x33));
    item.setSize(QSizeF(400, 300));
    const QPoint cell(60, 40);   // past the gutter and header, in the first byte row
    click(item, cell);
    QCOMPARE(item.selectedSignalIndex(), 0);

    QSignalSpy changed(&item, &SignalGridItem::modelChanged);
    QSignalSpy clicked(&item, &SignalGridItem::nodeKeyClicked);
    model.reset();

    QVERIFY(!item.model());
    QCOMPARE(changed.count(), 1);
    QCOMPARE(item.selectedSignalIndex(), -1);
    QVERIFY(item.hoveredTooltip().isEmpty());
    QCOMPARE(render(item).pixel(cell), QColor(Qt::black).rgb());
    click(item, cell);
    QCOMPARE(clicked.count(), 0);
    QCOMPARE(item.selectedSignalIndex(), -1);
}

QTEST_MAIN(TestSignalGridItem)
#include "tst_signalgriditem.moc"
