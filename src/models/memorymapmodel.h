#pragma once

#include <QAbstractListModel>
#include <QString>

#include <cstdint>
#include <limits>
#include <vector>

// The exclusive end of [address, address + size), saturated at the top of the
// address space: sizes come from saturating layout arithmetic.
inline uint64_t addressEnd(uint64_t address, uint64_t size) {
    return size > std::numeric_limits<uint64_t>::max() - address
        ? std::numeric_limits<uint64_t>::max()
        : address + size;
}

// One addressable object placed on the memory grid.
struct MemoryObject {
    QString name;
    QString longIdentifier;
    QString typeName;       // "VALUE", "CURVE", "MAP", "MEASUREMENT", "AXIS_PTS", etc.
    uint64_t address = 0;
    uint64_t size = 0;      // byte footprint (0 = unknown)
    bool sizeApproximate = false;
    int colorIndex = 0;     // index into color palette (0-7)
    quint64 nodeKey = 0;    // key into NodeRegistry for tree/detail selection
    QString recordLayoutRef;
    QString conversion;
};

// A memory segment (real from proto, or synthetic).
struct MemorySegmentInfo {
    QString name;
    uint64_t address = 0;
    uint64_t size = 0;
    QString memoryType;     // "FLASH", "RAM", etc.
    QString prgType;        // "CALIBRATION_VARIABLES", "CODE", etc.
    bool synthetic = false;
};

// Bytes [start, start + owner.size()) of the memory map: the model row owning
// each byte, or -1, and whether more than one object claims it.
struct MemoryTile {
    uint64_t start = 0;
    std::vector<int32_t> owner;
    std::vector<uint8_t> overlap;
};

// MemoryMapModel: flat list model exposing the memory map to QML.
//
// The rows are the current segment's objects as sorted intervals; nothing is
// stored per byte. queryBytes() resolves any byte range from those intervals,
// and ownership, overlap, hit-testing and painting all go through it, so they
// agree at every address the segment spans.

class MemoryMapModel : public QAbstractListModel {
    Q_OBJECT

    Q_PROPERTY(int segmentCount READ segmentCount NOTIFY segmentsChanged)
    Q_PROPERTY(int currentSegment READ currentSegment WRITE setCurrentSegment NOTIFY currentSegmentChanged)
    Q_PROPERTY(int objectCount READ objectCount NOTIFY objectsChanged)
    Q_PROPERTY(int excludedMeasurementCount READ excludedMeasurementCount CONSTANT)
    Q_PROPERTY(quint64 viewStartAddress READ viewStartAddress NOTIFY currentSegmentChanged)
    Q_PROPERTY(quint64 viewEndAddress READ viewEndAddress NOTIFY currentSegmentChanged)
    Q_PROPERTY(quint64 totalRows READ totalRows NOTIFY currentSegmentChanged)
    Q_PROPERTY(int bytesPerRow READ bytesPerRow WRITE setBytesPerRow NOTIFY bytesPerRowChanged)

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        AddressRole,
        SizeRole,
        TypeNameRole,
        ColorIndexRole,
        SizeApproximateRole,
        NodeKeyRole,
        RecordLayoutRole,
        ConversionRole,
    };

    explicit MemoryMapModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Segment management
    int segmentCount() const;
    int currentSegment() const;
    void setCurrentSegment(int index);

    Q_INVOKABLE QString segmentLabel(int index) const;

    // View geometry
    quint64 viewStartAddress() const;
    quint64 viewEndAddress() const;
    quint64 totalRows() const;
    int bytesPerRow() const;
    void setBytesPerRow(int bpr);

    int objectCount() const;
    int totalObjectCount() const;
    int excludedMeasurementCount() const;

    // Ownership of the `count` bytes from `start`. A byte belongs to the last
    // row whose known footprint covers it; a byte two or more rows claim is an
    // overlap; bytes outside the current segment are unowned. Work and memory
    // grow with `count` and the objects reaching into the range, not with the
    // segment, so callers pass the range they draw or test.
    MemoryTile queryBytes(uint64_t start, uint64_t count) const;

    // Query: which object (if any) owns the byte at `address`.
    // Returns -1 if unoccupied, or the model row index.
    Q_INVOKABLE int objectAtAddress(quint64 address) const;

    // Query: all objects overlapping [startAddr, endAddr).
    // Returns list of model row indices.
    Q_INVOKABLE QVariantList objectsInRange(quint64 startAddr, quint64 endAddr) const;

    // Query: is the byte at `address` claimed by more than one object in the
    // current segment?
    Q_INVOKABLE bool isOverlap(quint64 address) const;

    // Scroll target: returns the row index that contains `address`.
    Q_INVOKABLE quint64 rowForAddress(quint64 address) const;

    // Lookup object by tree nodeKey. Returns model row index or -1.
    Q_INVOKABLE int objectIndexForNodeKey(quint64 nodeKey) const;

    // Segment containing the object with this nodeKey. Scans all objects (not
    // just the filtered set) so cross-segment tree navigation can switch first.
    // Returns segment index or -1 if none.
    Q_INVOKABLE int segmentIndexForNodeKey(quint64 nodeKey) const;

    // Address of an object by model row index. Returns 0 if invalid.
    Q_INVOKABLE quint64 objectAddress(int objectIndex) const;

    // Building — called by A2lDocumentSession during construction.
    void addSegment(MemorySegmentInfo seg);
    void addObject(MemoryObject obj);
    void setExcludedMeasurementCount(int count);
    void finalize(); // sort objects, build index, derive synthetic segment if needed

signals:
    void segmentsChanged();
    void currentSegmentChanged();
    void objectsChanged();
    void bytesPerRowChanged();

private:
    void rebuildFilteredObjects();

    std::vector<MemorySegmentInfo> _segments;
    std::vector<MemoryObject> _all_objects;

    // Objects filtered to the current segment's address range, sorted by address.
    std::vector<const MemoryObject*> _filtered_objects;

    // Running maximum of the rows' footprint ends: the rows before the first
    // entry past an address all end at or before it.
    std::vector<uint64_t> _reach_end;

    int _current_segment = 0;
    int _bytes_per_row = 16;
    int _excluded_measurements = 0;
};
