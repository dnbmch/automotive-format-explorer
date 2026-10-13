// The production value table view, loaded from a test-time ExplorerApp module
// and shown offscreen at the center pane's minimum and a wide pane: text and
// array content load without a QML warning and show their cells, headers and
// controls, and an array's slice selectors leave the matrix its pane whatever
// the rank.

#include "models/tabledata.h"
#include "models/valuetablemodel.h"

#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTest>

#include <cstdint>
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

// The spin boxes under `item`, instantiated whether shown or not: the items
// with a range.
void collectSpinBoxes(QQuickItem* item, QList<QQuickItem*>& found) {
    if (item->property("from").isValid() && item->property("to").isValid()) {
        found.push_back(item);
    }
    const QList<QQuickItem*> children = item->childItems();
    for (QQuickItem* child : children) {
        collectSpinBoxes(child, found);
    }
}

QList<QQuickItem*> spinBoxes(QQuickItem* item) {
    QList<QQuickItem*> found;
    collectSpinBoxes(item, found);
    return found;
}

// The matrix under `item`, or null: the header views are TableView subclasses.
QQuickItem* matrix(QQuickItem* item) {
    if (QByteArrayView(item->metaObject()->className()) == "QQuickTableView") {
        return item;
    }
    const QList<QQuickItem*> children = item->childItems();
    for (QQuickItem* child : children) {
        if (QQuickItem* found = matrix(child)) {
            return found;
        }
    }
    return nullptr;
}

// The production view over `model`, shown in a window `width` × 400 px; `view`
// is null when the component fails.
struct ShownView {
    ShownView(ValueTableModel& model, int width) {
        engine.addImportPath(QStringLiteral(TABLEVIEW_IMPORT_PATH));
        component.loadUrl(QUrl::fromLocalFile(
            QStringLiteral(TABLEVIEW_IMPORT_PATH "/ExplorerApp/ValueTableView.qml")));
        window.resize(width, 400);
        view.reset(qobject_cast<QQuickItem*>(component.createWithInitialProperties(
            {{QStringLiteral("mapModel"), QVariant::fromValue(&model)}})));
        if (view) {
            view->setParentItem(window.contentItem());
            view->setSize(QSizeF(width, 400));
            window.show();
        }
    }

    QQmlEngine engine;
    QQmlComponent component{&engine};
    QQuickWindow window;
    std::unique_ptr<QQuickItem> view;
};

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

// A one-sample array channel of `dimensions`, outer first.
TableHeader arrayHeader(const QList<int>& dimensions) {
    TableHeader header;
    header.name = QStringLiteral("Signal_HighRank");
    header.sampleCount = 1;
    for (const int size : dimensions) {
        header.dimensions.push_back(static_cast<std::uint64_t>(size));
    }
    header.rowAxis = QStringLiteral("rows");
    header.columnAxis = QStringLiteral("columns");
    return header;
}

// Sample 0's slice of `dimensions`: the inner two as rows × columns of `values`.
ArraySlicePtr matrixSlice(const QList<int>& dimensions, const QList<double>& values) {
    auto slice = std::make_shared<ArraySlice>();
    slice->rows = static_cast<std::uint64_t>(dimensions[dimensions.size() - 2]);
    slice->columns = static_cast<std::uint64_t>(dimensions.back());
    slice->values.assign(values.begin(), values.end());
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
    void sliceControlsStayBounded_data();
    void sliceControlsStayBounded();
    void sliceSelectorSetsItsDimension();
    void selectorsFollowTheChannel();
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

    ShownView shown(model, width);
    QVERIFY2(shown.view, qPrintable(shown.component.errorString()));
    QVERIFY(QTest::qWaitForWindowExposed(&shown.window));
    QQuickItem* view = shown.view.get();

    QTRY_VERIFY(showing(view, header.name));
    QVERIFY(showing(view, header.role));
    if (array) {
        QTRY_VERIFY(showing(view, QStringLiteral("3698.8")));
        QVERIFY(showing(view, QStringLiteral("[−∞, 0[")));
        QVERIFY(showing(view, QStringLiteral("100")));
        // The row headers fit the corner's axis names, up to 160 px.
        const QQuickItem* corner = showing(view, QStringLiteral("throttle \\ nmot"));
        QVERIFY(corner);
        QVERIFY(!corner->property("truncated").toBool() || corner->parentItem()->width() == 160);
        // The outer dimension of two is the one to choose.
        QCOMPARE(spinBoxes(view).size(), 1);
    } else {
        QTRY_VERIFY(showing(view, QStringLiteral("zero")));
        QVERIFY(showing(view, QStringLiteral("Time [s]")));
        QVERIFY(showing(view, QStringLiteral("Rows 0–9 of 10")));
        QCOMPARE(spinBoxes(view).size(), 0);
    }
}

void TestValueTableView::sliceControlsStayBounded_data() {
    QTest::addColumn<QList<int>>("dimensions");
    QTest::addColumn<QList<double>>("values");
    QTest::addColumn<int>("width");
    QTest::addColumn<int>("fewestSpinBoxes");
    QTest::addColumn<int>("mostSpinBoxes");
    QTest::addColumn<QString>("cell");
    // Every outer dimension a single element: nothing to choose.
    QTest::addRow("128 dimensions of 1, 700 px")
        << QList<int>(128, 1) << QList<double>{42} << 700 << 0 << 0 << QStringLiteral("42");
    // 19 outer dimensions to choose from at the minimum width: the strip
    // instantiates the selectors it shows, not all of them.
    QTest::addRow("21 dimensions of 2, 300 px")
        << QList<int>(21, 2) << QList<double>{1, 2, 3, 4} << 300 << 1 << 18 << QStringLiteral("4");
    // One outer dimension of five between single ones: its one selector.
    QTest::addRow("{1, 5, 1, 3, 4}, 700 px")
        << QList<int>{1, 5, 1, 3, 4} << QList<double>{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12}
        << 700 << 1 << 1 << QStringLiteral("12");
}

// One selector per outer dimension with a choice, made as the strip shows it;
// the matrix keeps at least 240 px of a 400 px pane.
void TestValueTableView::sliceControlsStayBounded() {
    QFETCH(QList<int>, dimensions);
    QFETCH(QList<double>, values);
    QFETCH(int, width);
    QFETCH(int, fewestSpinBoxes);
    QFETCH(int, mostSpinBoxes);
    QFETCH(QString, cell);
    ValueTableModel model;
    model.setArrayChannel(arrayHeader(dimensions));
    model.setArraySlice(matrixSlice(dimensions, values));
    QCOMPARE(model.tableState(), ValueTableModel::Shown);

    ShownView shown(model, width);
    QVERIFY2(shown.view, qPrintable(shown.component.errorString()));
    QVERIFY(QTest::qWaitForWindowExposed(&shown.window));
    QQuickItem* view = shown.view.get();
    QQuickItem* table = matrix(view);
    QVERIFY(table);
    const auto layout = [&] {
        return QStringLiteral("%1 spin boxes, matrix %2 px high")
            .arg(spinBoxes(view).size())
            .arg(table->height());
    };

    QTRY_VERIFY2(table->height() >= 240, qPrintable(layout()));
    const qsizetype spins = spinBoxes(view).size();
    QVERIFY2(spins >= fewestSpinBoxes && spins <= mostSpinBoxes, qPrintable(layout()));
    QTRY_VERIFY(showing(table, cell));
}

// The one selector of {1, 5, 1, 3, 4} is the second dimension's: it offers its
// indices 0 to 4 and asks for the slice it sets, and stays as the slice changes.
void TestValueTableView::sliceSelectorSetsItsDimension() {
    const QList<int> dimensions{1, 5, 1, 3, 4};
    ValueTableModel model;
    model.setArrayChannel(arrayHeader(dimensions));
    model.setArraySlice(matrixSlice(dimensions, QList<double>(12, 1.0)));

    ShownView shown(model, 700);
    QVERIFY2(shown.view, qPrintable(shown.component.errorString()));
    QVERIFY(QTest::qWaitForWindowExposed(&shown.window));
    QQuickItem* view = shown.view.get();
    QTRY_VERIFY(showing(view, QStringLiteral("Signal_HighRank")));

    const QList<QQuickItem*> spins = spinBoxes(view);
    QCOMPARE(spins.size(), 1);
    QQuickItem* spin = spins.first();
    QCOMPARE(spin->property("to").toInt(), 4);
    QQuickItem* number = showing(spin->parentItem(), QStringLiteral("2"));
    QVERIFY(number);
    QCOMPARE(number->property("text").toString(), QStringLiteral("2"));

    QSignalSpy wanted(&model, &ValueTableModel::requestWanted);
    QVERIFY(spin->setProperty("value", 3));
    QVERIFY(QMetaObject::invokeMethod(spin, "valueModified"));
    QCOMPARE(wanted.size(), 1);
    QVERIFY(model.wantedSlice() == std::vector<std::uint64_t>({0, 3, 0}));
    QCOMPARE(spinBoxes(view), spins);
    QCOMPARE(spin->property("value").toInt(), 3);
}

// One live view through channels of other shapes: the strip follows each
// channel's dimensions with a choice, and no selector outlives its dimension.
void TestValueTableView::selectorsFollowTheChannel() {
    struct Step {
        QList<int> dimensions;
        int selectors;
        int firstDimension;  // the first selector's, zero-based
    };
    const Step steps[] = {
        {{2, 3, 1, 3, 4}, 2, 0},
        {{1, 5, 1, 3, 4}, 1, 1},
        {{3, 4}, 0, -1},
        {{2, 3, 4}, 1, 0},
        {{2, 2, 2, 2, 2, 3, 4}, 5, 0},
    };
    // The view opens on no channel: the first array lays out the row headers
    // and the corner they fit.
    ValueTableModel model;
    ShownView shown(model, 700);
    QVERIFY2(shown.view, qPrintable(shown.component.errorString()));
    QVERIFY(QTest::qWaitForWindowExposed(&shown.window));
    QQuickItem* view = shown.view.get();

    for (const Step& step : steps) {
        const qsizetype rank = step.dimensions.size();
        const int cells = step.dimensions[rank - 2] * step.dimensions.back();
        model.setArrayChannel(arrayHeader(step.dimensions));
        model.setArraySlice(matrixSlice(step.dimensions, QList<double>(cells, 1.0)));
        QVERIFY(model.wantedSlice()
                == std::vector<std::uint64_t>(rank > 2 ? static_cast<std::size_t>(rank - 2) : 0, 0));
        QTRY_COMPARE(spinBoxes(view).size(), step.selectors);
        if (step.selectors == 0) {
            continue;
        }
        QQuickItem* first = nullptr;
        for (QQuickItem* spin : spinBoxes(view)) {
            if (spin->parentItem()->property("index").toInt() == 0) {
                first = spin;
            }
        }
        QVERIFY(first);
        QCOMPARE(first->property("to").toInt(), step.dimensions[step.firstDimension] - 1);
        const QString number = QString::number(step.firstDimension + 1);
        QQuickItem* label = showing(first->parentItem(), number);
        QVERIFY(label);
        QCOMPARE(label->property("text").toString(), number);
    }

    TableHeader text;
    text.name = QStringLiteral("Data channel");
    text.sampleCount = 10;
    model.setTextChannel(text, numbersPage());
    QTRY_COMPARE(spinBoxes(view).size(), 0);
    model.clear();
    QTRY_COMPARE(spinBoxes(view).size(), 0);
}

QTEST_MAIN(TestValueTableView)
#include "tst_valuetableview.moc"
