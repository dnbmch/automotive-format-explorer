// The painted signal plot, offscreen into an image: overview bins stand apart,
// exact lines break at nonfinite values, and a model that goes away first, as
// a replaced center view's does, leaves the item inert.

#include "models/signalplotmodel.h"
#include "plotscan.h"
#include "ui/signalplotitem.h"

#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QSignalSpy>
#include <QTest>

#include <cmath>
#include <limits>
#include <memory>

using namespace plotscan;

namespace {

const QColor kSeries(255, 0, 0);

// Test access to the item's input handlers.
class PlotItem : public SignalPlotItem {
public:
    using SignalPlotItem::mouseMoveEvent;
    using SignalPlotItem::mousePressEvent;
};

PlotHeader header() {
    return {QStringLiteral("ramp"), {}, QStringLiteral("Time"), QStringLiteral("s")};
}

std::unique_ptr<SignalPlotModel> modelOf(std::uint64_t count, const Samples& samples) {
    auto model = std::make_unique<SignalPlotModel>();
    model->setSignal(header(), overviewOf(count, samples));
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

// Pixels of the series color in one column of the plot area.
int seriesPixels(const QImage& image, int x) {
    int found = 0;
    for (int y = 19; y < image.height() - 53; ++y) {
        const QColor pixel = image.pixelColor(x, y);
        if (pixel.red() > 128 && pixel.green() < 64 && pixel.blue() < 64) {
            ++found;
        }
    }
    return found;
}

void paintable(PlotItem& item, SignalPlotModel& model) {
    item.setModel(&model);
    item.setSize(QSizeF(640, 400));
    item.setColors(Qt::black, Qt::black, Qt::black, kSeries, Qt::yellow);
}

} // namespace

class TestSignalPlotItem : public QObject {
    Q_OBJECT

private slots:
    void modelDestroyedBeforeItem();
    void overviewBinsAreNotConnected();
    void exactLineBreaksAtNonfiniteValue();
};

// The item then holds no model, ends a drag in progress, paints its empty
// state and ignores input.
void TestSignalPlotItem::modelDestroyedBeforeItem() {
    auto model = modelOf(100, [](std::uint64_t index) {
        return std::make_pair(static_cast<double>(index), static_cast<double>(index));
    });
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

// Two runs far apart at different levels. Their bins are drawn where they lie;
// no line joins the last extremum of one to the first of the other.
void TestSignalPlotItem::overviewBinsAreNotConnected() {
    const Samples samples = [](std::uint64_t index) {
        const bool second = index >= 50'000;
        return std::make_pair(static_cast<double>(index) + (second ? 1'000'000.0 : 0.0),
                              (second ? 100.0 : 0.0) + static_cast<double>(index % 10));
    };
    auto model = modelOf(100'000, samples);
    PlotItem item;
    paintable(item, *model);
    QCOMPARE(model->plotState(), SignalPlotModel::Overview);

    // The plot spans x 72 to 622 for coordinates 0 to 1,099,999.
    const QImage image = render(item);
    QVERIFY(seriesPixels(image, 75) > 0);
    QVERIFY(seriesPixels(image, 619) > 0);
    for (int x = 110; x < 590; x += 20) {
        QCOMPARE(seriesPixels(image, x), 0);
    }
}

// Exact samples at 5.0 with a NaN at 10: the line stops before it and resumes
// after; the sample alone between NaNs at 12 and 14 is still drawn.
void TestSignalPlotItem::exactLineBreaksAtNonfiniteValue() {
    const Samples samples = [](std::uint64_t index) {
        const bool missing = index == 10 || index == 12 || index == 14;
        return std::make_pair(static_cast<double>(index),
                              missing ? std::numeric_limits<double>::quiet_NaN() : 5.0);
    };
    auto model = modelOf(21, samples);
    const std::optional<PlotWindowRequest> request = model->detailRequest();
    QVERIFY(request);
    model->setWindow(windowOf(*request, samples));
    QCOMPARE(model->plotState(), SignalPlotModel::Detail);
    PlotItem item;
    paintable(item, *model);

    // Coordinate t is at x = 72 + 27.5 t.
    const QImage image = render(item);
    QVERIFY(seriesPixels(image, 209) > 0);
    QCOMPARE(seriesPixels(image, 347), 0);
    QVERIFY(seriesPixels(image, 429) > 0);
    QCOMPARE(seriesPixels(image, 402), 0);
    QVERIFY(seriesPixels(image, 525) > 0);
}

QTEST_MAIN(TestSignalPlotItem)
#include "tst_signalplotitem.moc"
