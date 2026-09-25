// Address-space queries of the A2L memory-map model.
//
// Pins the behaviour the memory grid and MemoryView.qml depend on: byte
// ownership and overlap from the one sparse tile query (queryBytes) that
// hit-testing (objectAtAddress) and hatching (isOverlap) also read, at any
// distance into a segment; row geometry across multi-gigabyte segments; range
// queries feeding the byte-range selection readout (objectsInRange); and the
// record-layout / conversion roles feeding the hover tooltip.

#include "models/memorymapmodel.h"

#include <QTest>

#include <limits>
#include <memory>
#include <random>

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

QString rowName(const MemoryMapModel& model, int row) {
    return model.data(model.index(row, 0), MemoryMapModel::NameRole).toString();
}

} // namespace

class TestMemoryMapModel : public QObject {
    Q_OBJECT

private slots:
    void init();

    void objectAtAddressHitsAndGaps();
    void objectAtAddressCoversEarlierStartingObject();
    void objectAtAddressScansPastNonCoveringCandidate();
    void objectsInRange();
    void overlapBytesFlagged();
    void straddlerClaimsInSegmentBytes();
    void separateBlocksAreNotOverlap();
    void tileMatchesByteOracle();
    void objectBeyondSixteenMiB();
    void farApartObjectsInDerivedSegment();
    void laterRowOwnsSharedBytes();
    void intervalEndsSaturate();
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
    // 0x100C is inside both A and B; the later-starting B owns it — and A
    // still resolves where only A covers.
    QCOMPARE(_model->objectAtAddress(0x100C), 1);
    QCOMPARE(_model->objectAtAddress(0x1004), 0);
}

void TestMemoryMapModel::objectAtAddressScansPastNonCoveringCandidate() {
    // Big [0x1000, 0x1010) fully contains Small [0x1008, 0x100C). At 0x100E
    // Small, the later row, has already ended; Big still owns the byte.
    MemoryMapModel model;
    model.addObject(makeObject(QStringLiteral("Big"), 0x1000, 16));
    model.addObject(makeObject(QStringLiteral("Small"), 0x1008, 4));
    model.finalize();

    QCOMPARE(model.objectAtAddress(0x100E), 0);
    QCOMPARE(model.objectAtAddress(0x100A), 1);
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

void TestMemoryMapModel::straddlerClaimsInSegmentBytes() {
    // An object starting below the segment but reaching into it passes the
    // segment filter and claims its in-segment bytes — a second object on the
    // same bytes is an overlap. The grid paints straddlers clipped to match.
    MemoryMapModel model;
    MemorySegmentInfo seg;
    seg.name = QStringLiteral("FLASH");
    seg.address = 0x2000;
    seg.size = 0x1000;
    model.addSegment(seg);
    model.addObject(makeObject(QStringLiteral("StraddlerX"), 0x1FF8, 16)); // [0x1FF8, 0x2008)
    model.addObject(makeObject(QStringLiteral("InsideY"), 0x2000, 8));     // [0x2000, 0x2008)
    model.finalize();

    QCOMPARE(model.objectCount(), 2); // straddler passes the segment filter
    QVERIFY(model.isOverlap(0x2000));
    QVERIFY(model.isOverlap(0x2007));
    QVERIFY(!model.isOverlap(0x2008));
    QCOMPARE(model.objectAtAddress(0x1FFC), -1); // outside the segment: not shown
}

void TestMemoryMapModel::separateBlocksAreNotOverlap() {
    // A standalone block (e.g. an AxisPts referenced via AXIS_PTS_REF)
    // occupies its own range — never hatched.
    for (quint64 addr = 0x1020; addr < 0x1028; ++addr) {
        QVERIFY(!_model->isOverlap(addr));
    }
}

void TestMemoryMapModel::tileMatchesByteOracle() {
    // Random layouts — overlaps, containment, shared start addresses, unknown
    // sizes, straddlers at both segment ends — against a per-byte oracle over
    // the objects in insertion order: a byte belongs to the covering object with
    // the highest start address, ties going to the later one, and it overlaps
    // when two or more cover it. Tiles start and end anywhere, inside objects
    // and outside the segment.
    std::mt19937_64 rng(20260925);
    const uint64_t segStart = 0x1000;
    const uint64_t segEnd = 0x1200;

    for (int layout = 0; layout < 300; ++layout) {
        MemoryMapModel model;
        MemorySegmentInfo seg;
        seg.name = QStringLiteral("SEG");
        seg.address = segStart;
        seg.size = segEnd - segStart;
        model.addSegment(seg);

        struct Placed {
            uint64_t address;
            uint64_t size;
        };
        std::vector<Placed> placed;
        const int count = 1 + static_cast<int>(rng() % 14);
        for (int i = 0; i < count; ++i) {
            const uint64_t address = 0xF00 + (rng() % 0x60) * 8; // shared starts are common
            const uint64_t size = rng() % 5 == 0 ? 0 : 1 + rng() % 0x80;
            placed.push_back({address, size});
            model.addObject(makeObject(QString::number(i), address, size));
        }
        model.finalize();

        for (int trial = 0; trial < 8; ++trial) {
            const uint64_t start = 0xE80 + rng() % 0x400;
            const uint64_t length = 1 + rng() % 0x280;
            const MemoryTile tile = model.queryBytes(start, length);
            QCOMPARE(tile.start, start);
            QCOMPARE(tile.owner.size(), size_t(length));
            QCOMPARE(tile.overlap.size(), size_t(length));

            for (uint64_t k = 0; k < length; ++k) {
                const uint64_t address = start + k;
                int expected = -1;
                int covering = 0;
                if (address >= segStart && address < segEnd) {
                    for (int i = 0; i < count; ++i) {
                        const Placed& p = placed[static_cast<size_t>(i)];
                        if (address < p.address || address >= p.address + p.size) {
                            continue;
                        }
                        ++covering;
                        if (expected < 0
                            || p.address >= placed[static_cast<size_t>(expected)].address) {
                            expected = i;
                        }
                    }
                }
                const int32_t owner = tile.owner[static_cast<size_t>(k)];
                if (expected < 0) {
                    QCOMPARE(owner, -1);
                } else {
                    QVERIFY(owner >= 0);
                    QCOMPARE(rowName(model, owner), QString::number(expected));
                }
                QCOMPARE(tile.overlap[static_cast<size_t>(k)] != 0, covering >= 2);
                QCOMPARE(model.objectAtAddress(address), owner);
                QCOMPARE(model.isOverlap(address), covering >= 2);
            }
        }
    }
}

void TestMemoryMapModel::objectBeyondSixteenMiB() {
    // A real 64 MiB segment: an object 48 MiB in, and an alias overlapping
    // it, resolve exactly like objects at the segment start.
    MemoryMapModel model;
    MemorySegmentInfo seg;
    seg.name = QStringLiteral("FLASH");
    seg.address = 0x80000000;
    seg.size = uint64_t(64) << 20;
    model.addSegment(seg);
    model.addObject(makeObject(QStringLiteral("Near"), 0x80000010, 4));
    model.addObject(makeObject(QStringLiteral("Far"), 0x83000000, 16));
    model.addObject(makeObject(QStringLiteral("FarAlias"), 0x83000008, 4));
    model.finalize();

    QCOMPARE(model.objectCount(), 3);
    QCOMPARE(model.objectAtAddress(0x80000010), 0);
    QCOMPARE(model.objectAtAddress(0x83000000), 1);
    QCOMPARE(model.objectAtAddress(0x83000009), 2); // the later row owns shared bytes
    QCOMPARE(model.objectAtAddress(0x8300000C), 1);
    QCOMPARE(model.objectAtAddress(0x83000010), -1);
    QVERIFY(model.isOverlap(0x83000008));
    QVERIFY(model.isOverlap(0x8300000B));
    QVERIFY(!model.isOverlap(0x8300000C));

    // A tile crossing into Far and through the alias.
    const MemoryTile tile = model.queryBytes(0x82FFFFFC, 24);
    QCOMPARE(tile.owner[3], -1);
    QCOMPARE(tile.owner[4], 1);
    QCOMPARE(tile.owner[12], 2);
    QCOMPARE(tile.owner[16], 1);
    QCOMPARE(tile.overlap[12], uint8_t(1));
    QCOMPARE(tile.overlap[16], uint8_t(0));

    QCOMPARE(model.rowForAddress(0x83000000), quint64(0x3000000 / 16));
}

void TestMemoryMapModel::farApartObjectsInDerivedSegment() {
    // Without segments, objects 3 GiB apart derive one segment spanning both.
    // Both stay addressable and the row geometry does not wrap.
    MemoryMapModel model;
    model.addObject(makeObject(QStringLiteral("Low"), 0x00001000, 8));
    model.addObject(makeObject(QStringLiteral("High"), 0xC0001000, 8));
    model.finalize();

    QCOMPARE(model.segmentCount(), 1);
    QCOMPARE(model.viewStartAddress(), quint64(0x1000));
    QCOMPARE(model.viewEndAddress(), quint64(0xC0001100));
    QCOMPARE(model.totalRows(), quint64(0xC0000100 / 16));
    QCOMPARE(model.objectAtAddress(0x1000), 0);
    QCOMPARE(model.objectAtAddress(0xC0001007), 1);
    QCOMPARE(model.objectAtAddress(0x80000000), -1);
    QCOMPARE(model.rowForAddress(0xC0001000), quint64(0xC0000000 / 16));
}

void TestMemoryMapModel::laterRowOwnsSharedBytes() {
    // Objects sharing a start address keep document order, and the later one
    // owns the bytes they share — enough of them that an unstable sort would
    // reorder them.
    MemoryMapModel model;
    const int count = 40;
    for (int i = 0; i < count; ++i) {
        model.addObject(makeObject(QString::number(i), 0x1000, uint64_t(count - i)));
    }
    model.addObject(makeObject(QStringLiteral("before"), 0x0F00, 1));
    model.finalize();

    QCOMPARE(rowName(model, 0), QStringLiteral("before"));
    for (int i = 0; i < count; ++i) {
        QCOMPARE(rowName(model, i + 1), QString::number(i));
    }
    // Byte 0x1000 + k is covered by objects 0 .. count - 1 - k; the last owns it.
    for (int k = 0; k < count; ++k) {
        const quint64 address = 0x1000 + static_cast<quint64>(k);
        QCOMPARE(model.objectAtAddress(address), count - k);
        QCOMPARE(model.isOverlap(address), k < count - 1);
    }
}

void TestMemoryMapModel::intervalEndsSaturate() {
    const uint64_t top = std::numeric_limits<uint64_t>::max();
    QCOMPARE(addressEnd(0x1000, 8), uint64_t(0x1008));
    QCOMPARE(addressEnd(top - 3, top), top);

    // A saturated layout size ends the object at the top of the address space
    // instead of wrapping below its start; the derived segment contains it.
    MemoryMapModel model;
    model.addObject(makeObject(QStringLiteral("Huge"), top - 0x80, top));
    model.finalize();

    QCOMPARE(model.viewStartAddress(), quint64(top - 0xFF));
    QCOMPARE(model.viewEndAddress(), quint64(top));
    QCOMPARE(model.objectAtAddress(top - 0x80), 0);
    QCOMPARE(model.objectAtAddress(top - 1), 0);
    QCOMPARE(model.objectAtAddress(top - 0x81), -1);
    QCOMPARE(model.totalRows(), quint64(0xFF / 16 + 1));
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
