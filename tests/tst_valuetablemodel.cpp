// The value table's model: the text of its cells and headers, a page's columns
// and its text cells' cap, the requests a view makes, and observers that act
// from inside its notifications.

#include "models/tabledata.h"
#include "models/valuetablemodel.h"

#include <QLocale>
#include <QSignalSpy>
#include <QTest>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

// The page of `texts` from `first`, row i at time `times[i]` or i: a text
// channel's, one Text column.
TablePagePtr pageOf(std::uint64_t first, const std::vector<std::string>& texts,
                    std::vector<double> times = {}, std::uint64_t invalid = 0) {
    std::vector<std::string_view> views(texts.begin(), texts.end());
    for (std::size_t i = times.size(); i < texts.size(); ++i) {
        times.push_back(static_cast<double>(i));
    }
    TablePageBuilder builder(first, texts.size(), {{TableColumn::Kind::Text}});
    builder.addTime(times.data(), views.size());
    builder.addText(0, views.data(), views.size());
    return builder.finish(invalid);
}

TableHeader textHeader(std::uint64_t count, bool timed = true) {
    TableHeader header;
    header.name = QStringLiteral("DriverMessage");
    header.role = QStringLiteral("Text channel");
    header.sampleCount = count;
    header.values = QStringLiteral("Texts");
    header.columns = {{QStringLiteral("Text"), false}};
    if (timed) {
        header.timeName = QStringLiteral("Time");
        header.timeUnit = QStringLiteral("s");
    }
    return header;
}

TableHeader arrayHeader(std::vector<std::uint64_t> dimensions, std::uint64_t count = 4,
                        bool timed = true) {
    TableHeader header;
    header.name = QStringLiteral("2D_classification");
    header.role = QStringLiteral("Classification result");
    header.unit = QStringLiteral("s");
    header.sampleCount = count;
    header.values = QStringLiteral("Values");
    if (timed) {
        header.timeName = QStringLiteral("t");
        header.timeUnit = QStringLiteral("s");
    }
    header.dimensions = std::move(dimensions);
    header.rowAxis = QStringLiteral("throttle");
    header.columnAxis = QStringLiteral("nmot");
    return header;
}

ArraySlicePtr sliceOf(std::uint64_t rows, std::uint64_t columns, std::vector<double> values,
                      TableAxis rowAxis = {}, TableAxis columnAxis = {}) {
    auto slice = std::make_shared<ArraySlice>();
    slice->time = 0.25;
    slice->rows = rows;
    slice->columns = columns;
    slice->values = std::move(values);
    slice->rowAxis = std::move(rowAxis);
    slice->columnAxis = std::move(columnAxis);
    return slice;
}

QString cell(const ValueTableModel& model, int row, int column) {
    return model.data(model.index(row, column)).toString();
}

QString header(const ValueTableModel& model, int section, Qt::Orientation orientation) {
    return model.headerData(section, orientation).toString();
}

// The model's notifications.
enum Notification : int { AboutToReset, Reset, Content, Wanted, Busy };

// Runs action once, from inside the model's next such notification.
template <typename Action>
void callOnce(ValueTableModel* model, int notification, Action action) {
    auto done = std::make_shared<bool>(false);
    const auto once = [done, action] {
        if (!*done) {
            *done = true;
            action();
        }
    };
    switch (notification) {
    case AboutToReset:
        QObject::connect(model, &QAbstractItemModel::modelAboutToBeReset, once);
        break;
    case Reset:
        QObject::connect(model, &QAbstractItemModel::modelReset, once);
        break;
    case Content:
        QObject::connect(model, &ValueTableModel::contentChanged, once);
        break;
    case Wanted:
        QObject::connect(model, &ValueTableModel::requestWanted, once);
        break;
    default:
        QObject::connect(model, &ValueTableModel::busyChanged, once);
        break;
    }
}

} // namespace

class TestValueTableModel : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void valuesAndInfinities();
    void axisLabels();
    void textCellCutOnCharacterBoundary();
    void textPageRowsAndColumns();
    void textWithoutTimeAxisAndEmpty();
    void pageColumns();
    void showSampleClamps();
    void arraySampleAndSlice();
    void observerMayDestroyModel_data();
    void observerMayDestroyModel();
    void observerMayInstallAgain_data();
    void observerMayInstallAgain();
};

void TestValueTableModel::initTestCase() {
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
}

// A value cell shows 7 significant digits, an invalid value as an empty cell
// and the infinities by their signs.
void TestValueTableModel::valuesAndInfinities() {
    ValueTableModel model;
    model.setArrayChannel(arrayHeader({1, 6}),
                          sliceOf(1, 6, {kNaN, kInf, -kInf, 3698.800048828125, 4.0, -2.5}));
    QCOMPARE(model.tableState(), ValueTableModel::Shown);
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.columnCount(), 6);
    QCOMPARE(cell(model, 0, 0), QString());
    QCOMPARE(cell(model, 0, 1), QStringLiteral("∞"));
    QCOMPARE(cell(model, 0, 2), QStringLiteral("−∞"));
    QCOMPARE(cell(model, 0, 3), QStringLiteral("3698.8"));
    QCOMPARE(cell(model, 0, 4), QStringLiteral("4"));
    QCOMPARE(cell(model, 0, 5), QStringLiteral("-2.5"));
    QCOMPARE(ValueTableModel::valueText(1.0 / 3.0), QStringLiteral("0.3333333"));
    QCOMPARE(ValueTableModel::valueText(123456789.0), QStringLiteral("1.234568e+08"));
}

// Interval axes read [a, b[, or ]a, b] when left-open, their open ends as
// infinities; point axes give their points, index axes the zero-based index.
void TestValueTableModel::axisLabels() {
    ValueTableModel model;
    model.setArrayChannel(
        arrayHeader({2, 2}),
        sliceOf(2, 2, {1.0, 2.0, 3.0, 4.0},
                TableAxis{TableAxis::Kind::Intervals, {-kInf, 0.0, 10.0}, false, {}},
                TableAxis{TableAxis::Kind::Intervals, {-kInf, 0.0, 2.5}, true, {}}));
    QCOMPARE(header(model, 0, Qt::Vertical), QStringLiteral("[−∞, 0["));
    QCOMPARE(header(model, 1, Qt::Vertical), QStringLiteral("[0, 10["));
    QCOMPARE(header(model, 0, Qt::Horizontal), QStringLiteral("]−∞, 0]"));
    QCOMPARE(header(model, 1, Qt::Horizontal), QStringLiteral("]0, 2.5]"));

    model.setArraySlice(sliceOf(2, 2, {1.0, 2.0, 3.0, 4.0},
                                TableAxis{TableAxis::Kind::Points, {0.5, 1250.004}, false, {}}));
    QCOMPARE(header(model, 0, Qt::Vertical), QStringLiteral("0.5"));
    QCOMPARE(header(model, 1, Qt::Vertical), QStringLiteral("1250.004"));
    QCOMPARE(header(model, 0, Qt::Horizontal), QStringLiteral("0"));
    QCOMPARE(header(model, 1, Qt::Horizontal), QStringLiteral("1"));
}

// A text past the cell cap keeps its longest prefix that ends on a character
// boundary and leaves room for "…"; a text of exactly the cap stays whole. The
// page never holds more than its reservation.
void TestValueTableModel::textCellCutOnCharacterBoundary() {
    // 1,020 Bytes, then a three-Byte character across the cut, then two more.
    const std::string longText = std::string(1020, 'a') + "€" + "ab";
    QCOMPARE(longText.size(), std::size_t(1025));
    const std::string capText(kCellBytes, 'b');
    const TablePagePtr page = pageOf(0, {longText, capText, "short"});
    QCOMPARE(std::string(page->columns[0].textAt(0)), std::string(1020, 'a') + "…");
    QCOMPARE(std::string(page->columns[0].textAt(1)), capText);
    QCOMPARE(std::string(page->columns[0].textAt(2)), std::string("short"));
    QVERIFY(page->bytes() <= TablePageBuilder::reservation(3, {{TableColumn::Kind::Text}}));

    ValueTableModel model;
    model.setPageChannel(textHeader(3), page);
    QCOMPARE(cell(model, 0, 1), QString(1020, QLatin1Char('a')) + QStringLiteral("…"));
    QCOMPARE(cell(model, 1, 1).size(), qsizetype(1024));
}

// A page's rows are indexed by absolute sample; Time shows 12 significant
// digits; the range, the count and the invalid samples are stated.
void TestValueTableModel::textPageRowsAndColumns() {
    ValueTableModel model;
    QSignalSpy wanted(&model, &ValueTableModel::requestWanted);
    model.setPageChannel(textHeader(3000));
    QCOMPARE(model.tableState(), ValueTableModel::Pending);
    QCOMPARE(model.message(), QStringLiteral("Reading texts…"));
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(model.columnCount(), 2);
    QCOMPARE(header(model, 0, Qt::Horizontal), QStringLiteral("Time [s]"));
    QCOMPARE(header(model, 1, Qt::Horizontal), QStringLiteral("Text"));
    QCOMPARE(model.kind(), ValueTableModel::Page);
    QCOMPARE(model.countText(), QStringLiteral("3,000 samples"));
    QCOMPARE(model.cornerText(), QStringLiteral("Sample"));
    QVERIFY(model.rangeText().isEmpty());

    model.showSample(2100);
    QCOMPARE(wanted.count(), 1);
    QCOMPARE(model.firstRow(), qulonglong(2048));
    model.setPage(pageOf(2048, {"1 Rekorder mit Aufzeichnung gestartet", "", "Messung gestoppt"},
                         {0.000480912, 4.700965438, 13.452942673}, 1));
    QCOMPARE(wanted.count(), 1);
    QCOMPARE(model.tableState(), ValueTableModel::Shown);
    QVERIFY(model.message().isEmpty());
    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(header(model, 0, Qt::Vertical), QStringLiteral("2048"));
    QCOMPARE(header(model, 2, Qt::Vertical), QStringLiteral("2050"));
    QCOMPARE(cell(model, 0, 0), QStringLiteral("0.000480912"));
    QCOMPARE(cell(model, 2, 0), QStringLiteral("13.452942673"));
    QCOMPARE(cell(model, 0, 1), QStringLiteral("1 Rekorder mit Aufzeichnung gestartet"));
    QCOMPARE(cell(model, 1, 1), QString());
    QCOMPARE(model.invalidCount(), qulonglong(1));
    QCOMPARE(model.rangeText(), QStringLiteral("Rows 2,048–2,050 of 3,000"));
    QVERIFY(model.hasPrevious());
    QVERIFY(!model.hasNext());
}

// A group without a time master shows the Text column alone; a channel without
// samples says so.
void TestValueTableModel::textWithoutTimeAxisAndEmpty() {
    ValueTableModel model;
    model.setPageChannel(textHeader(3, false), pageOf(0, {"a", "b", "c"}));
    QCOMPARE(model.columnCount(), 1);
    QCOMPARE(header(model, 0, Qt::Horizontal), QStringLiteral("Text"));
    QCOMPARE(cell(model, 2, 0), QStringLiteral("c"));
    QVERIFY(!model.hasPrevious());
    QVERIFY(!model.hasNext());

    model.setPageChannel(textHeader(0), pageOf(0, {}));
    QCOMPARE(model.tableState(), ValueTableModel::Empty);
    QCOMPARE(model.message(), QStringLiteral("No samples recorded"));
    QCOMPARE(model.countText(), QStringLiteral("0 samples"));

    model.clear();
    QCOMPARE(model.tableState(), ValueTableModel::NoChannel);
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(model.columnCount(), 0);
}

// A page of the time, a Values column, a Text column and the channel's own
// column: a value or text per cell, an empty one past a short member column,
// headers by name, numbers right-aligned, the page's own note as the message
// and the other notes in the page's word. An array has no time column and
// numbers in every column.
void TestValueTableModel::pageColumns() {
    TableHeader frames;
    frames.name = QStringLiteral("CAN_DataFrame");
    frames.role = QStringLiteral("Structure of 3 members");
    frames.sampleCount = 3;
    frames.values = QStringLiteral("Frames");
    frames.timeName = QStringLiteral("t");
    frames.timeUnit = QStringLiteral("s");
    frames.columns = {{QStringLiteral("ID"), true},
                      {QStringLiteral("Note"), false},
                      {QStringLiteral("Bytes"), false}};

    ValueTableModel model;
    model.setPageChannel(frames);
    QCOMPARE(model.kind(), ValueTableModel::Page);
    QCOMPARE(model.tableState(), ValueTableModel::Pending);
    QCOMPARE(model.message(), QStringLiteral("Reading frames…"));
    QVERIFY(model.hasTimeColumn());
    QCOMPARE(model.columnCount(), 4);
    QCOMPARE(header(model, 0, Qt::Horizontal), QStringLiteral("t [s]"));
    QCOMPARE(header(model, 1, Qt::Horizontal), QStringLiteral("ID"));
    QCOMPARE(header(model, 2, Qt::Horizontal), QStringLiteral("Note"));
    QCOMPARE(header(model, 3, Qt::Horizontal), QStringLiteral("Bytes"));
    for (int column = -1; column <= 4; ++column) {
        QCOMPARE(model.numericColumn(column), column == 1);
    }

    // ID for every row, Note for two of three, the structure's own Bytes for
    // every row but an invalid one.
    TablePageBuilder builder(0, 3, {{TableColumn::Kind::Values},
                                    {TableColumn::Kind::Text},
                                    {TableColumn::Kind::Text, 45}});
    const double times[] = {0.5, 1.5, 2.5};
    const double ids[] = {100.0, kNaN, 102.0};
    const std::string_view notes[] = {"first", "second"};
    const std::string_view bytes[] = {std::string_view("\x64\0\0\0", 4), {}, std::string_view("\xFF\x01", 2)};
    builder.addTime(times, 3);
    builder.addValues(0, ids, 3);
    builder.addText(1, notes, 2);
    builder.addBytes(2, bytes, 3);
    model.setPage(builder.finish(1, "Note could not be read: test"));
    QCOMPARE(model.tableState(), ValueTableModel::Shown);
    QCOMPARE(model.message(), QStringLiteral("Note could not be read: test"));
    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(cell(model, 0, 0), QStringLiteral("0.5"));
    QCOMPARE(cell(model, 0, 1), QStringLiteral("100"));
    QCOMPARE(cell(model, 1, 1), QString());
    QCOMPARE(cell(model, 1, 2), QStringLiteral("second"));
    QCOMPARE(cell(model, 2, 2), QString());
    QCOMPARE(cell(model, 0, 3), QStringLiteral("64 00 00 00"));
    QCOMPARE(cell(model, 1, 3), QString());
    QCOMPARE(cell(model, 2, 3), QStringLiteral("FF 01"));
    QCOMPARE(header(model, 2, Qt::Vertical), QStringLiteral("2"));
    QCOMPARE(model.cornerText(), QStringLiteral("Sample"));
    QCOMPARE(model.rangeText(), QStringLiteral("Rows 0–2 of 3"));
    QCOMPARE(model.invalidCount(), qulonglong(1));

    // Without a time master the members lead.
    frames.timeName.clear();
    frames.timeUnit.clear();
    model.setPageChannel(frames);
    QVERIFY(!model.hasTimeColumn());
    QCOMPARE(model.columnCount(), 3);
    QCOMPARE(header(model, 0, Qt::Horizontal), QStringLiteral("ID"));
    QVERIFY(model.numericColumn(0));
    QVERIFY(!model.numericColumn(2));

    model.setPage({}, TableNote::Failed);
    QCOMPARE(model.message(), QStringLiteral("Frames could not be read"));
    model.setPage({}, TableNote::Refused);
    QCOMPARE(model.message(), QStringLiteral("Frames exceed the viewer's limits"));

    model.setArrayChannel(arrayHeader({2, 3}), sliceOf(2, 3, {1.0, 2.0, 3.0, 4.0, 5.0, 6.0}));
    QVERIFY(!model.hasTimeColumn());
    QVERIFY(model.numericColumn(0));
    QVERIFY(model.numericColumn(2));
    QVERIFY(!model.numericColumn(3));
}

// A view's request clamps to the channel's samples, asks once per new page or
// sample, and asks again for what failed or was refused.
void TestValueTableModel::showSampleClamps() {
    ValueTableModel model;
    QSignalSpy wanted(&model, &ValueTableModel::requestWanted);
    model.setPageChannel(textHeader(3000));
    QCOMPARE(wanted.count(), 0);

    model.showSample(5000);
    QCOMPARE(model.wantedSample(), std::uint64_t(2048));
    QCOMPARE(wanted.count(), 1);
    model.showSample(2100);
    QCOMPARE(wanted.count(), 1);
    model.showSample(-7);
    QCOMPARE(model.wantedSample(), std::uint64_t(0));
    QCOMPARE(wanted.count(), 2);
    model.showSample(std::nan(""));
    QCOMPARE(wanted.count(), 2);
    model.nextPage();
    model.nextPage();
    QCOMPARE(model.wantedSample(), std::uint64_t(2048));
    model.nextPage();
    QCOMPARE(wanted.count(), 4);
    model.previousPage();
    QCOMPARE(model.wantedSample(), std::uint64_t(1024));
    QCOMPARE(wanted.count(), 5);

    model.setArrayChannel(arrayHeader({2, 3}, 4));
    model.showSample(10);
    QCOMPARE(model.wantedSample(), std::uint64_t(3));
    QCOMPARE(model.sample(), qulonglong(3));
    QCOMPARE(wanted.count(), 6);
    model.setArraySlice({}, TableNote::Failed, QStringLiteral("Values could not be read: test"));
    QCOMPARE(model.tableState(), ValueTableModel::Failed);
    QCOMPARE(model.message(), QStringLiteral("Values could not be read: test"));
    model.showSample(3);
    QCOMPARE(wanted.count(), 7);
    QCOMPARE(model.tableState(), ValueTableModel::Pending);
    model.previousPage();
    QCOMPARE(model.wantedSample(), std::uint64_t(2));
    QCOMPARE(wanted.count(), 8);
}

// An array table states its sample, the sample's time against a time master,
// its dimensions and the outer indices of its slice, which a view sets one at a
// time, clamped; the corner names the row and column axes, and an axis note
// shows as the message.
void TestValueTableModel::arraySampleAndSlice() {
    ValueTableModel model;
    QSignalSpy wanted(&model, &ValueTableModel::requestWanted);
    model.setArrayChannel(arrayHeader({2, 3, 4}, 5));
    QCOMPARE(model.kind(), ValueTableModel::Array);
    QCOMPARE(model.tableState(), ValueTableModel::Pending);
    QCOMPARE(model.message(), QStringLiteral("Reading values…"));
    QCOMPARE(model.dimensions(), (QVariantList{qulonglong(2), qulonglong(3), qulonglong(4)}));
    QCOMPARE(model.slice(), QVariantList{qulonglong(0)});
    QCOMPARE(model.wantedSlice(), std::vector<std::uint64_t>{0});
    QCOMPARE(model.cornerText(), QStringLiteral("throttle \\ nmot"));

    model.setSlice(0, 1);
    QCOMPARE(model.wantedSlice(), std::vector<std::uint64_t>{1});
    QCOMPARE(wanted.count(), 1);
    model.setSlice(0, 7);
    model.setSlice(1, 0);
    QCOMPARE(wanted.count(), 1);

    TableAxis rows;
    rows.note = "no common master with Curve1";
    std::vector<double> values(12);
    for (std::size_t k = 0; k < values.size(); ++k) {
        values[k] = static_cast<double>(12 + k);
    }
    model.setArraySlice(sliceOf(3, 4, values, rows));
    QCOMPARE(model.tableState(), ValueTableModel::Shown);
    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(model.columnCount(), 4);
    QCOMPARE(cell(model, 2, 3), QStringLiteral("23"));
    QCOMPARE(model.sample(), qulonglong(0));
    QCOMPARE(model.timeText(), QStringLiteral("0.25 s"));
    QCOMPARE(model.message(), QStringLiteral("no common master with Curve1"));
    QVERIFY(!model.hasPrevious());
    QVERIFY(model.hasNext());

    // Without a time master the sample's time is its index: no time is stated.
    model.setArrayChannel(arrayHeader({3, 4}, 5, false), sliceOf(3, 4, values));
    QCOMPARE(model.tableState(), ValueTableModel::Shown);
    QVERIFY(model.timeText().isEmpty());
    QCOMPARE(model.cornerText(), QStringLiteral("throttle \\ nmot"));
}

void TestValueTableModel::observerMayDestroyModel_data() {
    QTest::addColumn<int>("notification");
    QTest::addColumn<bool>("request");
    for (const bool request : {false, true}) {
        const char* by = request ? "request" : "page";
        QTest::addRow("pre-reset, %s", by) << int(AboutToReset) << request;
        QTest::addRow("reset, %s", by) << int(Reset) << request;
        QTest::addRow("content, %s", by) << int(Content) << request;
    }
    QTest::addRow("wanted, request") << int(Wanted) << true;
    QTest::addRow("busy") << int(Busy) << false;
}

// Observers run synchronously and may destroy the model, as closing a tab does.
// The model lives in storage the test poisons once an observer of one of its
// notifications has destroyed it: the call touches nothing after that.
void TestValueTableModel::observerMayDestroyModel() {
    QFETCH(int, notification);
    QFETCH(bool, request);
    alignas(ValueTableModel) unsigned char storage[sizeof(ValueTableModel)];
    auto* model = new (storage) ValueTableModel;
    model->setPageChannel(textHeader(3000));
    callOnce(model, notification, [&storage, model] {
        model->~ValueTableModel();
        std::memset(storage, 0xA5, sizeof storage);
    });
    if (notification == Busy) {
        model->setBusy(true);
    } else if (request) {
        model->showSample(2500);
    } else {
        model->setPage(pageOf(0, {"zero"}));
    }
    QVERIFY(std::all_of(std::begin(storage), std::end(storage),
                        [](unsigned char byte) { return byte == 0xA5; }));
}

void TestValueTableModel::observerMayInstallAgain_data() {
    QTest::addColumn<int>("notification");
    QTest::newRow("pre-reset") << int(AboutToReset);
    QTest::newRow("reset") << int(Reset);
    QTest::newRow("content") << int(Content);
}

// An observer installs anew from inside a notification of the page's
// installation: the table ends on the newer page, and every reset announced as
// about to happen is announced as done. From the pre-reset notification the
// one reset under way installs the newer page.
void TestValueTableModel::observerMayInstallAgain() {
    QFETCH(int, notification);
    ValueTableModel model;
    model.setPageChannel(textHeader(2));
    QSignalSpy aboutToReset(&model, &QAbstractItemModel::modelAboutToBeReset);
    QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
    callOnce(&model, notification, [&model] { model.setPage(pageOf(0, {"new", "newer"})); });
    model.setPage(pageOf(0, {"old"}));

    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(cell(model, 1, 1), QStringLiteral("newer"));
    QCOMPARE(aboutToReset.size(), notification == AboutToReset ? 1 : 2);
    QCOMPARE(reset.size(), aboutToReset.size());
}

QTEST_GUILESS_MAIN(TestValueTableModel)
#include "tst_valuetablemodel.moc"
