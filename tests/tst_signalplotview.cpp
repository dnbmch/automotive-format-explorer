// The production signal plot view, loaded from a test-time ExplorerApp module
// and shown offscreen at the widths the center pane allows: a source that ended
// early is marked incomplete at every width, over the overview and over exact
// samples, and complete data never is.

#include "models/signalplotmodel.h"
#include "plotscan.h"
#include "ui/signalplotitem.h"

#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QTest>

#include <memory>

namespace {

// The visible item of the view showing `text`, or null.
QQuickItem* showing(QQuickItem* view, const QString& text) {
    const QList<QQuickItem*> items = view->findChildren<QQuickItem*>();
    for (QQuickItem* item : items) {
        if (item->isVisible() && item->property("text").toString().contains(text)) {
            return item;
        }
    }
    return nullptr;
}

// Some item shows `text` whole: visible, not squeezed below the text's width and
// not pushed past either edge of the view.
bool showsWhole(QQuickItem* view, const QString& text) {
    const QList<QQuickItem*> items = view->findChildren<QQuickItem*>();
    for (QQuickItem* item : items) {
        if (!item->isVisible() || !item->property("text").toString().contains(text)) {
            continue;
        }
        const qreal left = item->mapToItem(view, QPointF(0, 0)).x();
        if (item->width() + 0.5 >= item->implicitWidth() && left >= 0.0 &&
            left + item->width() <= view->width()) {
            return true;
        }
    }
    return false;
}

} // namespace

class TestSignalPlotView : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void incompleteMarkedAtEveryWidth_data();
    void incompleteMarkedAtEveryWidth();
};

// The application's style, and its registration of the plot's C++ types.
void TestSignalPlotView::initTestCase() {
    QQuickStyle::setStyle(QStringLiteral("Fusion"));
    qmlRegisterType<SignalPlotItem>("ExplorerApp", 1, 0, "SignalPlotItem");
    qmlRegisterUncreatableType<SignalPlotModel>("ExplorerApp", 1, 0, "SignalPlotModel",
                                                QStringLiteral("Provided by a document session"));
}

// Any QML warning or error fails the case.
void TestSignalPlotView::init() {
    QTest::failOnWarning(QRegularExpression(QStringLiteral("\\.qml:")));
}

void TestSignalPlotView::incompleteMarkedAtEveryWidth_data() {
    QTest::addColumn<bool>("incomplete");
    QTest::addColumn<bool>("exact");
    QTest::addColumn<int>("width");
    for (const bool incomplete : {true, false}) {
        for (const bool exact : {false, true}) {
            // The center pane's minimum, a narrow pane and a wide one.
            for (const int width : {300, 450, 700}) {
                QTest::addRow("%s, %s, %d px", incomplete ? "4 of 100" : "4 of 4",
                              exact ? "exact samples" : "overview", width)
                    << incomplete << exact << width;
            }
        }
    }
}

// Four samples of a source that stated 100, or 4. The overview is shown while
// its exact samples are read, with progress; the exact window once it came.
// The wide pane also gives the counts.
void TestSignalPlotView::incompleteMarkedAtEveryWidth() {
    QFETCH(bool, incomplete);
    QFETCH(bool, exact);
    QFETCH(int, width);
    const plotscan::Samples samples = [](std::uint64_t index) {
        return std::make_pair(static_cast<double>(index), 10.0 * static_cast<double>(index + 1));
    };
    SignalPlotModel model;
    model.setSignal({QStringLiteral("EngineSpeed"), QStringLiteral("rpm"), QStringLiteral("Time"),
                     QStringLiteral("s")},
                    plotscan::overviewOf(4, samples, incomplete ? 100 : 4));
    if (exact) {
        model.setWindow(plotscan::windowOf(*model.detailRequest(), samples));
    } else {
        model.setBusy(true);
        model.setProgress(0.4);
    }
    QCOMPARE(model.plotState(), exact ? SignalPlotModel::Detail : SignalPlotModel::Overview);
    QCOMPARE(model.incomplete(), incomplete);

    QQmlEngine engine;
    engine.addImportPath(QStringLiteral(PLOTVIEW_IMPORT_PATH));
    QQmlComponent component(
        &engine, QUrl::fromLocalFile(QStringLiteral(PLOTVIEW_IMPORT_PATH "/ExplorerApp/SignalPlotView.qml")));
    QQuickWindow window;
    window.resize(width, 400);
    std::unique_ptr<QQuickItem> view(qobject_cast<QQuickItem*>(component.createWithInitialProperties(
        {{QStringLiteral("mapModel"), QVariant::fromValue(&model)}})));
    QVERIFY2(view, qPrintable(component.errorString()));
    view->setParentItem(window.contentItem());
    view->setSize(QSizeF(width, 400));
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    // The header is laid out once the reset button sits at its right end.
    QTRY_VERIFY(showing(view.get(), QStringLiteral("Reset view")) &&
                showing(view.get(), QStringLiteral("Reset view"))->mapToItem(view.get(), QPointF(0, 0)).x() >
                    width / 2.0);
    QCOMPARE(showsWhole(view.get(), QStringLiteral("Incomplete")), incomplete);
    QCOMPARE(showing(view.get(), QStringLiteral("Incomplete")) != nullptr, incomplete);
    if (width >= 650) {
        QVERIFY(showsWhole(view.get(), incomplete ? QStringLiteral("4 of 100 samples")
                                                  : QStringLiteral("4 samples")));
    }
}

QTEST_MAIN(TestSignalPlotView)
#include "tst_signalplotview.moc"
