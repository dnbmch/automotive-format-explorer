// The painted signal plot against a model that goes away first, as a replaced
// center view does. Paints into an image; runs on the offscreen platform.

#include "models/signalplotmodel.h"
#include "ui/signalplotitem.h"

#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QSignalSpy>
#include <QTest>

#include <memory>

namespace {

// Test access to the item's input handlers.
class PlotItem : public SignalPlotItem {
public:
    using SignalPlotItem::mouseMoveEvent;
    using SignalPlotItem::mousePressEvent;
};

std::unique_ptr<SignalPlotModel> rampModel() {
    auto model = std::make_unique<SignalPlotModel>();
    PlotSeries series;
    series.name = QStringLiteral("ramp");
    for (int i = 0; i < 100; ++i) {
        series.time.push_back(i);
        series.value.push_back(i);
    }
    model->setSeries(std::make_shared<const PlotSeries>(std::move(series)));
    return model;
}

QImage render(SignalPlotItem& item) {
    QImage image(static_cast<int>(item.width()), static_cast<int>(item.height()),
                 QImage::Format_ARGB32);
    image.fill(Qt::black);
    QPainter painter(&image);
    item.paint(&painter);
    return image;
}

} // namespace

class TestSignalPlotItem : public QObject {
    Q_OBJECT

private slots:
    void modelDestroyedBeforeItem();
};

// The item then holds no model, ends a drag in progress, paints its empty
// state and ignores input.
void TestSignalPlotItem::modelDestroyedBeforeItem() {
    auto model = rampModel();
    PlotItem item;
    item.setModel(model.get());
    item.setSize(QSizeF(640, 400));
    const QPointF inside(320, 180);
    QMouseEvent press(QEvent::MouseButtonPress, inside, inside, Qt::LeftButton, Qt::LeftButton,
                      Qt::NoModifier);
    item.mousePressEvent(&press);
    QVERIFY(item.panning());

    QSignalSpy changed(&item, &SignalPlotItem::modelChanged);
    QSignalSpy panning(&item, &SignalPlotItem::panningChanged);
    model.reset();

    QVERIFY(!item.model());
    QCOMPARE(changed.count(), 1);
    QVERIFY(!item.panning());
    QCOMPARE(panning.count(), 1);
    const QImage image = render(item);
    QVERIFY(!image.isNull());
    QMouseEvent move(QEvent::MouseMove, inside + QPointF(40, 0), inside + QPointF(40, 0),
                     Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    item.mouseMoveEvent(&move);
    item.mousePressEvent(&press);
    QVERIFY(!item.panning());
}

QTEST_MAIN(TestSignalPlotItem)
#include "tst_signalplotitem.moc"
