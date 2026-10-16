// The value table over recordings of the ASAM MDF 4.2.0 examples, through the
// production adapter and the reader. ctest points MDF4_EXAMPLES at the
// examples' directory (BaseStandard/Examples of the corpus); the corpus is not
// redistributable, so when that directory is missing the executable exits 77
// and ctest reports the suite as Skipped, never as a silent pass.
//
//   MDF4_EXAMPLES=<.../BaseStandard/Examples> ./build/tst_mdf4tablecorpus
//
// The array expectations are those of mdf4-parser's tests/corpus_array.cpp, the
// bus-logging ones those of its tests/corpus_bus.cpp, an independent asammdf
// dump.

#include "adapters/mdf4adapter.h"
#include "models/treemodel.h"
#include "models/valuetablemodel.h"
#include "sessions/documentsession.h"

#include <QCoreApplication>
#include <QDir>
#include <QLocale>
#include <QTest>

#include <atomic>
#include <initializer_list>

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
    void canFramesSortedByBus();
    void canFramesSortedById();

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

// Vector's CAN log sorted by bus: 1,457 frames of the 15-Byte structure
// CAN_DataFrame and its nine members, titled without the parent's name although
// the group states no separator. The structure's Bytes: ID little-endian at its
// start (record Byte 8), Flags at 12, DLC at 13, DataLength at 14, the payload
// from 15. The first frame carries ID 100 and the payload 64 00 00 00 at
// 0.019968196 s, the last ID 18 and 00 01 00 00 at 7.960498196 s.
void TestMdf4TableCorpus::canFramesSortedByBus() {
    ValueTableModel* table = show("BusLogging/CAN/Vector_CAN_DataFrame_Sort_Bus.MF4", 0, 1);
    QVERIFY(table);
    QCOMPARE(table->tableState(), ValueTableModel::Shown);
    QCOMPARE(table->role(), QStringLiteral("Structure of 9 members"));
    const QStringList headers{"t [s]", "BusChannel", "DataBytes", "DLC", "Flags", "ID",
                              "IDE", "SingleWire", "WakeUp", "DataLength", "Bytes"};
    QCOMPARE(table->columnCount(), int(headers.size()));
    for (int column = 0; column < int(headers.size()); ++column) {
        QCOMPARE(columnHeader(*table, column), headers[column]);
    }
    QCOMPARE(cell(*table, 0, 0), QStringLiteral("0.019968196"));
    QCOMPARE(cell(*table, 0, 1), QStringLiteral("1"));
    QCOMPARE(cell(*table, 0, 2), QStringLiteral("64 00 00 00"));
    QCOMPARE(cell(*table, 0, 3), QStringLiteral("4"));
    QCOMPARE(cell(*table, 0, 5), QStringLiteral("100"));
    QCOMPARE(cell(*table, 0, 9), QStringLiteral("4"));
    const QString bytes = cell(*table, 0, 10);
    QCOMPARE(bytes.size(), qsizetype(44));
    QVERIFY2(bytes.startsWith(QStringLiteral("64 00 00 00")) &&
                 bytes.mid(15).startsWith(QStringLiteral("04 04 64 00 00 00")),
             qPrintable(bytes));
    QCOMPARE(bytes, QStringLiteral("64 00 00 00 00 04 04 64 00 00 00 00 00 00 00"));
    QCOMPARE(table->rangeText(), QStringLiteral("Rows 0–1,023 of 1,457"));

    table->nextPage();
    QVERIFY(settled(table));
    QCOMPARE(table->tableState(), ValueTableModel::Shown);
    QCOMPARE(rowHeader(*table, 432), QStringLiteral("1456"));
    QCOMPARE(cell(*table, 432, 5), QStringLiteral("18"));
    QCOMPARE(cell(*table, 432, 2), QStringLiteral("00 01 00 00"));
    QCOMPARE(cell(*table, 432, 0), QStringLiteral("7.960498196"));
    QCOMPARE(table->rangeText(), QStringLiteral("Rows 1,024–1,456 of 1,457"));
}

// The same log sorted by ID: the first group holds ID 100's 795 frames, one
// page, its members in the file's order, ID and BusChannel virtual. The tree
// lists the nine members under the structure without its name and the group's
// separator.
void TestMdf4TableCorpus::canFramesSortedById() {
    ValueTableModel* table = show("BusLogging/CAN/Vector_CAN_DataFrame_Sort_ID.MF4", 0, 1);
    QVERIFY(table);
    QCOMPARE(table->tableState(), ValueTableModel::Shown);
    QCOMPARE(table->role(), QStringLiteral("Structure of 9 members"));
    const QStringList members{"DataLength", "WakeUp", "SingleWire", "IDE", "ID",
                              "Flags",      "DLC",    "DataBytes",  "BusChannel"};
    QCOMPARE(table->columnCount(), int(members.size()) + 2);
    QCOMPARE(columnHeader(*table, 0), QStringLiteral("t [s]"));
    for (int member = 0; member < int(members.size()); ++member) {
        QCOMPARE(columnHeader(*table, member + 1), members[member]);
    }
    QCOMPARE(columnHeader(*table, 10), QStringLiteral("Bytes"));
    QCOMPARE(table->rowCount(), 795);
    for (const int row : {0, 794}) {
        QCOMPARE(cell(*table, row, 5), QStringLiteral("100"));
        QCOMPARE(cell(*table, row, 9), QStringLiteral("1"));
    }
    QCOMPARE(cell(*table, 0, 0), QStringLiteral("0.019968196"));
    QCOMPARE(cell(*table, 0, 8), QStringLiteral("64 00 00 00"));
    QCOMPARE(cell(*table, 0, 10), QStringLiteral("64 00 00 00 00 04 04 64 00 00 00 00 00 00 00"));
    QCOMPARE(cell(*table, 794, 0), QStringLiteral("7.960354196"));
    QCOMPARE(cell(*table, 794, 8), QStringLiteral("54 03 00 00"));
    QCOMPARE(table->rangeText(), QStringLiteral("Rows 0–794 of 795"));
    QVERIFY(!table->hasNext());

    TreeModel* tree = _loaded.session->treeModel();
    const QModelIndex group = tree->index(0, 0, tree->index(0, 0));
    QCOMPARE(tree->rowCount(group), 2);
    const QModelIndex frame = tree->index(1, 0, group);
    QCOMPARE(tree->data(frame, TreeModel::TitleRole).toString(), QStringLiteral("CAN_DataFrame"));
    QCOMPARE(tree->rowCount(frame), int(members.size()));
    for (int member = 0; member < int(members.size()); ++member) {
        QCOMPARE(tree->data(tree->index(member, 0, frame), TreeModel::TitleRole).toString(),
                 members[member]);
    }
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
