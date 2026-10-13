// The production value table view, loaded from a test-time ExplorerApp module
// and shown offscreen at the center pane's minimum and a wide pane: text and
// array content load without a QML warning and show their cells, headers and
// controls.

#include "models/tabledata.h"
#include "models/valuetablemodel.h"

#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QTest>

#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

// The visible item under `item` showing `text`, or null. Table cells are the
// table's visual children only, so the walk follows the item tree.
QQuickItem* showing(QQuickItem* item, const QString& text) {
    if (!item->isVisible()) {
        return nullptr;
    }
    if (item->property("text").toString().contains(text)) {
        return item;
    }
    const QList<QQuickItem*> children = item->childItems();
    for (QQuickItem* child : children) {
        if (QQuickItem* found = showing(child, text)) {
            return found;
        }
    }
    return nullptr;
}

const char* const kNumbers[] = {"zero", "one", "two", "three", "four",
                                "five", "six", "seven", "eight", "nine"};

TextPagePtr numbersPage() {
    std::vector<double> time;
    std::vector<std::string_view> texts;
    for (std::size_t i = 0; i < std::size(kNumbers); ++i) {
        time.push_back(static_cast<double>(i));
        texts.push_back(kNumbers[i]);
    }
    TextPageBuilder builder(0, texts.size());
    builder.add(time.data(), texts.data(), texts.size());
    return builder.finish(0);
}

ArraySlicePtr classification() {
    constexpr double kInf = std::numeric_limits<double>::infinity();
    auto slice = std::make_shared<ArraySlice>();
    slice->sample = 1;
    slice->time = 0.5;
    slice->rows = 3;
    slice->columns = 4;
    for (std::size_t k = 0; k < 12; ++k) {
        slice->values.push_back(k == 4 ? 3698.800048828125 : static_cast<double>(k));
    }
    slice->values[5] = std::numeric_limits<double>::quiet_NaN();
    slice->rowAxis = {TableAxis::Kind::Intervals, {-kInf, 0.0, 10.0, 20.0}, false, {}};
    slice->columnAxis = {TableAxis::Kind::Points, {100.0, 200.0, 300.0, 400.0}, false, {}};
    return slice;
}

} // namespace

class TestValueTableView : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void showsContent_data();
    void showsContent();
};

// The application's style, and its registration of the table model.
void TestValueTableView::initTestCase() {
    QQuickStyle::setStyle(QStringLiteral("Fusion"));
    qmlRegisterUncreatableType<ValueTableModel>("ExplorerApp", 1, 0, "ValueTableModel",
                                                QStringLiteral("Provided by a document session"));
}

// Any QML warning or error fails the case.
void TestValueTableView::init() {
    QTest::failOnWarning(QRegularExpression(QStringLiteral("\\.qml:")));
}

void TestValueTableView::showsContent_data() {
    QTest::addColumn<bool>("array");
    QTest::addColumn<int>("width");
    for (const bool array : {false, true}) {
        // The center pane's minimum and a wide one.
        for (const int width : {300, 700}) {
            QTest::addRow("%s, %d px", array ? "array" : "text", width) << array << width;
        }
    }
}

void TestValueTableView::showsContent() {
    QFETCH(bool, array);
    QFETCH(int, width);
    ValueTableModel model;
    TableHeader header;
    header.sampleCount = 10;
    if (array) {
        header.name = QStringLiteral("Signal_MatrixDim");
        header.role = QStringLiteral("Array 2 × 3 × 4");
        header.dimensions = {2, 3, 4};
        header.rowAxis = QStringLiteral("throttle");
        header.columnAxis = QStringLiteral("nmot");
        model.setArrayChannel(header);
        model.showSample(1);
        model.setArraySlice(classification());
    } else {
        header.name = QStringLiteral("Data channel");
        header.role = QStringLiteral("Text channel");
        header.timeName = QStringLiteral("Time");
        header.timeUnit = QStringLiteral("s");
        model.setTextChannel(header, numbersPage());
    }
    QCOMPARE(model.tableState(), ValueTableModel::Shown);

    QQmlEngine engine;
    engine.addImportPath(QStringLiteral(TABLEVIEW_IMPORT_PATH));
    QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(
                                         TABLEVIEW_IMPORT_PATH "/ExplorerApp/ValueTableView.qml")));
    QQuickWindow window;
    window.resize(width, 400);
    std::unique_ptr<QQuickItem> view(qobject_cast<QQuickItem*>(component.createWithInitialProperties(
        {{QStringLiteral("mapModel"), QVariant::fromValue(&model)}})));
    QVERIFY2(view, qPrintable(component.errorString()));
    view->setParentItem(window.contentItem());
    view->setSize(QSizeF(width, 400));
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    QTRY_VERIFY(showing(view.get(), header.name));
    QVERIFY(showing(view.get(), header.role));
    if (array) {
        QTRY_VERIFY(showing(view.get(), QStringLiteral("3698.8")));
        QVERIFY(showing(view.get(), QStringLiteral("[−∞, 0[")));
        QVERIFY(showing(view.get(), QStringLiteral("100")));
        QVERIFY(showing(view.get(), QStringLiteral("throttle \\ nmot")));
    } else {
        QTRY_VERIFY(showing(view.get(), QStringLiteral("zero")));
        QVERIFY(showing(view.get(), QStringLiteral("Time [s]")));
        QVERIFY(showing(view.get(), QStringLiteral("Rows 0–9 of 10")));
    }
}

QTEST_MAIN(TestValueTableView)
#include "tst_valuetableview.moc"
