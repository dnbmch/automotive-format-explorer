// Address-space queries of the A2L memory-map model.
//
// Pins the behaviour the memory grid and MemoryView.qml depend on: byte-level
// object lookup (objectAtAddress), range queries feeding the byte-range
// selection readout (objectsInRange), byte-level overlap detection feeding the
// grid's overlap hatching (isOverlap), and the record-layout / conversion roles
// feeding the hover tooltip.

#include "models/memorymapmodel.h"

#include <QTest>

#include <memory>

namespace {

MemoryObject makeObject(const QString& name, uint64_t address, uint64_t size,
                        const QString& recordLayout = {}, const QString& conversion = {}) {
    MemoryObject obj;
    obj.name = name;
    obj.address = address;
    obj.size = size;
    obj.recordLayoutRef = recordLayout;
    obj.conversion = conversion;
    return obj;
}

} // namespace

class TestMemoryMapModel : public QObject {
    Q_OBJECT

private slots:
    void init();

    void objectAtAddressHitsAndGaps();
    void objectAtAddressCoversEarlierStartingObject();
    void objectsInRange();
    void overlapBytesFlagged();
    void separateBlocksAreNotOverlap();
    void tooltipRolesExposed();

private:
    std::unique_ptr<MemoryMapModel> _model;
};

// Fresh model per test:
//   A "CharA" [0x1000, 0x1010)  RL_A / CM_A
//   B "MeasB" [0x1008, 0x1010)  — overlaps the tail of A
//   C "AxisC" [0x1020, 0x1028)  — its own block, no overlap
// No segments added → finalize() derives one synthetic segment.
void TestMemoryMapModel::init() {
    _model = std::make_unique<MemoryMapModel>();
    _model->addObject(makeObject(QStringLiteral("CharA"), 0x1000, 16,
                                 QStringLiteral("RL_A"), QStringLiteral("CM_A")));
    _model->addObject(makeObject(QStringLiteral("MeasB"), 0x1008, 8));
    _model->addObject(makeObject(QStringLiteral("AxisC"), 0x1020, 8));
    _model->finalize();
}

void TestMemoryMapModel::objectAtAddressHitsAndGaps() {
    QCOMPARE(_model->objectAtAddress(0x1000), 0);
    QCOMPARE(_model->objectAtAddress(0x1007), 0);
    QCOMPARE(_model->objectAtAddress(0x1020), 2);
    QCOMPARE(_model->objectAtAddress(0x1018), -1); // gap between B and C
    QCOMPARE(_model->objectAtAddress(0x1028), -1); // past the last object
}

void TestMemoryMapModel::objectAtAddressCoversEarlierStartingObject() {
    // 0x100C is inside both A and B; the backward scan finds the
    // later-starting B — and still resolves A where only A covers.
    QCOMPARE(_model->objectAtAddress(0x100C), 1);
    QCOMPARE(_model->objectAtAddress(0x1004), 0);
}

void TestMemoryMapModel::objectsInRange() {
    QCOMPARE(_model->objectsInRange(0x1004, 0x1024), QVariantList({0, 1, 2}));
    QCOMPARE(_model->objectsInRange(0x100F, 0x1010), QVariantList({0, 1}));
    QCOMPARE(_model->objectsInRange(0x1010, 0x1020), QVariantList{}); // gap only
    QCOMPARE(_model->objectsInRange(0x1020, 0x1010), QVariantList{}); // inverted
}

void TestMemoryMapModel::overlapBytesFlagged() {
    QVERIFY(_model->isOverlap(0x1008));
    QVERIFY(_model->isOverlap(0x100F));
    QVERIFY(!_model->isOverlap(0x1007)); // A only
    QVERIFY(!_model->isOverlap(0x1010)); // both A and B end here (exclusive)
}

void TestMemoryMapModel::separateBlocksAreNotOverlap() {
    // A standalone block (e.g. an AxisPts referenced via AXIS_PTS_REF)
    // occupies its own range — never hatched.
    for (quint64 addr = 0x1020; addr < 0x1028; ++addr) {
        QVERIFY(!_model->isOverlap(addr));
    }
}

void TestMemoryMapModel::tooltipRolesExposed() {
    const auto mi = _model->index(0, 0);
    QCOMPARE(_model->data(mi, MemoryMapModel::RecordLayoutRole).toString(),
             QStringLiteral("RL_A"));
    QCOMPARE(_model->data(mi, MemoryMapModel::ConversionRole).toString(),
             QStringLiteral("CM_A"));

    const auto names = _model->roleNames();
    QVERIFY(names.contains(MemoryMapModel::RecordLayoutRole));
    QVERIFY(names.contains(MemoryMapModel::ConversionRole));
}

QTEST_MAIN(TestMemoryMapModel)
#include "tst_memorymapmodel.moc"
