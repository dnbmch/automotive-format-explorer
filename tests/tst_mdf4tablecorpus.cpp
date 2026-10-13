// The value table over recordings of the ASAM MDF 4.2.0 examples, through the
// production adapter and the reader. ctest points MDF4_EXAMPLES at the
// examples' directory (BaseStandard/Examples of the corpus); the corpus is not
// redistributable, so when that directory is missing the executable exits 77
// and ctest reports the suite as Skipped, never as a silent pass.
//
//   MDF4_EXAMPLES=<.../BaseStandard/Examples> ./build/tst_mdf4tablecorpus
//
// The array expectations are those of mdf4-parser's tests/corpus_array.cpp.

#include "adapters/mdf4adapter.h"
#include "models/treemodel.h"
#include "models/valuetablemodel.h"
#include "sessions/documentsession.h"

#include <QCoreApplication>
#include <QDir>
#include <QLocale>
#include <QTest>

#include <atomic>

namespace {

QString example(const char* relative) {
    return QDir(qEnvironmentVariable("MDF4_EXAMPLES")).filePath(QString::fromUtf8(relative));
}

quint64 channelKey(DocumentSession& session, int group, int channel) {
    TreeModel* tree = session.treeModel();
    const QModelIndex groupRow = tree->index(group, 0, tree->index(0, 0));
    return tree->data(tree->index(channel, 0, groupRow), TreeModel::NodeKeyRole).toULongLong();
}

QString cell(const ValueTableModel& table, int row, int column) {
    return table.data(table.index(row, column)).toString();
}

QString rowHeader(const ValueTableModel& table, int row) {
    return table.headerData(row, Qt::Vertical).toString();
}

QString columnHeader(const ValueTableModel& table, int column) {
    return table.headerData(column, Qt::Horizontal).toString();
}

// The table's read has settled: what it asked for shows, or why it does not.
bool settled(ValueTableModel* table) {
    return QTest::qWaitFor([table] {
        return table->tableState() != ValueTableModel::Pending && !table->busy();
    }, 10000);
}

} // namespace

class TestMdf4TableCorpus : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void vlsdText();
    void unsortedVlsdText();
    void classificationResult();
    void fixedAxisAndChannelAxisBeforeItsFirstSample();
    void slicesOfARealArray();

private:
    // Opens `file`, selects its channel and waits until the table settles.
    ValueTableModel* show(const char* file, int group, int channel);

    LoadResult _loaded;
};

void TestMdf4TableCorpus::initTestCase() {
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
}

ValueTableModel* TestMdf4TableCorpus::show(const char* file, int group, int channel) {
    const std::atomic<bool> cancel{false};
    _loaded = Mdf4Adapter().load(example(file), cancel);
    if (!_loaded.session || !_loaded.openingError.isEmpty()) {
        return nullptr;
    }
    _loaded.session->selectNode(channelKey(*_loaded.session, group, channel));
    auto* table = qobject_cast<ValueTableModel*>(_loaded.session->centerPanelModel());
    return table && settled(table) ? table : nullptr;
}

// Ten UTF-8 texts behind 64-bit offsets in one ##SD, against a time master.
void TestMdf4TableCorpus::vlsdText() {
    ValueTableModel* table = show("ChannelTypes/VLSD/Vector_VLSDStringUTF8.mf4", 0, 1);
    QVERIFY(table);
    QCOMPARE(table->tableState(), ValueTableModel::Shown);
    QCOMPARE(table->rowCount(), 10);
    QCOMPARE(table->columnCount(), 2);
    QCOMPARE(rowHeader(*table, 0), QStringLiteral("0"));
    QCOMPARE(cell(*table, 0, 0), QStringLiteral("0"));
    QCOMPARE(cell(*table, 0, 1), QStringLiteral("zero"));
    QCOMPARE(cell(*table, 9, 0), QStringLiteral("9"));
    QCOMPARE(cell(*table, 9, 1), QStringLiteral("nine"));
    QCOMPARE(table->rangeText(), QStringLiteral("Rows 0–9 of 10"));
}

// An unsorted data group's VLSD channel group: its 140 texts, from the first
// recorder message to the stop of the measurement.
void TestMdf4TableCorpus::unsortedVlsdText() {
    ValueTableModel* table = show("UnsortedData/VLSD/Vector_Unsorted_VLSD.MF4", 1, 1);
    QVERIFY(table);
    QCOMPARE(table->tableState(), ValueTableModel::Shown);
    QCOMPARE(table->rowCount(), 140);
    QCOMPARE(table->columnCount(), 2);
    QCOMPARE(cell(*table, 0, 0), QStringLiteral("0.000480912"));
    QCOMPARE(cell(*table, 0, 1), QStringLiteral("1 Rekorder mit Aufzeichnung gestartet"));
    QCOMPARE(cell(*table, 139, 0), QStringLiteral("13.452942673"));
    QCOMPARE(cell(*table, 139, 1), QStringLiteral("Messung gestoppt"));
}

// POR's 2D_classification: 52 x 142 on the interval axes throttle (rows) and
// nmot (columns), both from -inf, left-closed. [1][0] = 3698.800048828125 and
// the maximum 10697.0595703125 at [26][0] (corpus_array.cpp).
void TestMdf4TableCorpus::classificationResult() {
    ValueTableModel* table =
        show("Arrays/Classification/Porsche_2D_classification_result.mf4", 0, 0);
    QVERIFY(table);
    QCOMPARE(table->tableState(), ValueTableModel::Shown);
    QCOMPARE(table->role(), QStringLiteral("Classification result 52 × 142"));
    QCOMPARE(table->cornerText(), QStringLiteral("throttle \\ nmot"));
    QCOMPARE(table->rowCount(), 52);
    QCOMPARE(table->columnCount(), 142);
    QCOMPARE(cell(*table, 1, 0), QStringLiteral("3698.8"));
    QCOMPARE(cell(*table, 26, 0), QStringLiteral("10697.06"));
    QVERIFY2(rowHeader(*table, 0).startsWith(QStringLiteral("[−∞, ")), qPrintable(rowHeader(*table, 0)));
    QVERIFY2(columnHeader(*table, 0).startsWith(QStringLiteral("[−∞, ")),
             qPrintable(columnHeader(*table, 0)));
    QVERIFY(rowHeader(*table, 51).endsWith(QStringLiteral(", ∞[")));
    QVERIFY(columnHeader(*table, 141).endsWith(QStringLiteral(", ∞[")));
    QVERIFY(table->message().isEmpty());
}

// VMA's KF4, 6 x 8, sample 0: the fixed points 0 ... 5 head the rows; Curve1,
// whose first sample (0.771 s) comes after KF4's (0.661 s), heads the columns
// with indices and a note. [2][4] is 24 (corpus_array.cpp).
void TestMdf4TableCorpus::fixedAxisAndChannelAxisBeforeItsFirstSample() {
    ValueTableModel* table = show("Arrays/Simple/Vector_MeasurementArrays.mf4", 8, 1);
    QVERIFY(table);
    QCOMPARE(table->tableState(), ValueTableModel::Shown);
    QCOMPARE(table->name(), QStringLiteral("KF4"));
    QCOMPARE(table->rowCount(), 6);
    QCOMPARE(table->columnCount(), 8);
    for (int row = 0; row < 6; ++row) {
        QCOMPARE(rowHeader(*table, row), QString::number(row));
    }
    for (int column = 0; column < 8; ++column) {
        QCOMPARE(columnHeader(*table, column), QString::number(column));
    }
    QCOMPARE(table->message(), QStringLiteral("no Curve1 sample at or before this one"));
    QCOMPARE(cell(*table, 2, 4), QStringLiteral("24"));
}

// dSPACE's Signal_MatrixDim, 2 x 3 x 4 on index axes, element [i1][i2][i3] of
// sample s being i1 + 0.1 i2 + 0.01 i3 + 0.001 s: slice 0 of sample 0 at (0, 1)
// is 0.01, slice 1 at (0, 0) is 1, and sample 4's slice 1 at (2, 3) is 1.234
// (corpus_array.cpp).
void TestMdf4TableCorpus::slicesOfARealArray() {
    ValueTableModel* table = show("Arrays/Simple/dSPACE_MeasurementArrays.mf4", 0, 2);
    QVERIFY(table);
    QCOMPARE(table->tableState(), ValueTableModel::Shown);
    QCOMPARE(table->name(), QStringLiteral("Signal_MatrixDim"));
    QCOMPARE(table->rowCount(), 3);
    QCOMPARE(table->columnCount(), 4);
    QCOMPARE(cell(*table, 0, 1), QStringLiteral("0.01"));

    table->setSlice(0, 1);
    QVERIFY(settled(table));
    QCOMPARE(table->tableState(), ValueTableModel::Shown);
    QCOMPARE(cell(*table, 0, 0), QStringLiteral("1"));

    table->showSample(4);
    QVERIFY(settled(table));
    QCOMPARE(table->tableState(), ValueTableModel::Shown);
    QCOMPARE(table->sample(), qulonglong(4));
    QCOMPARE(table->slice(), QVariantList{qulonglong(1)});
    QCOMPARE(cell(*table, 2, 3), QStringLiteral("1.234"));
}

int main(int argc, char* argv[]) {
    const QString examples = qEnvironmentVariable("MDF4_EXAMPLES");
    if (examples.isEmpty() || !QDir(examples).exists()) {
        qInfo("MDF4_EXAMPLES names no directory - skipping the table corpus");
        return 77;
    }

    QCoreApplication app(argc, argv);
    TestMdf4TableCorpus testCase;
    return QTest::qExec(&testCase, argc, argv);
}

#include "tst_mdf4tablecorpus.moc"
