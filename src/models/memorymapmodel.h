#pragma once

#include <QAbstractListModel>
#include <QString>

#include <cstdint>
#include <vector>

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

// MemoryMapModel: flat list model exposing the memory map to QML.
//
// The model has two sections of data exposed via properties:
//   - segments: list of MemorySegmentInfo (for the segment selector)
//   - objects: sorted interval list of MemoryObject (for grid painting)
//
// QML reads objects for the currently selected segment.
// The grid rendering (MemoryView.qml) uses invokable methods to query
// which objects occupy a given address range.

class MemoryMapModel : public QAbstractListModel {
    Q_OBJECT

    Q_PROPERTY(int segmentCount READ segmentCount NOTIFY segmentsChanged)
    Q_PROPERTY(int currentSegment READ currentSegment WRITE setCurrentSegment NOTIFY currentSegmentChanged)
    Q_PROPERTY(int objectCount READ objectCount NOTIFY objectsChanged)
    Q_PROPERTY(int excludedMeasurementCount READ excludedMeasurementCount CONSTANT)
    Q_PROPERTY(quint64 viewStartAddress READ viewStartAddress NOTIFY currentSegmentChanged)
    Q_PROPERTY(quint64 viewEndAddress READ viewEndAddress NOTIFY currentSegmentChanged)
    Q_PROPERTY(int totalRows READ totalRows NOTIFY currentSegmentChanged)
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
    int totalRows() const;
    int bytesPerRow() const;
    void setBytesPerRow(int bpr);

    int objectCount() const;
    int totalObjectCount() const;
    int excludedMeasurementCount() const;

    // Query: which object (if any) occupies the byte at `address`.
    // Returns -1 if unoccupied, or the model row index.
    Q_INVOKABLE int objectAtAddress(quint64 address) const;

    // Query: all objects overlapping [startAddr, endAddr).
    // Returns list of model row indices.
    Q_INVOKABLE QVariantList objectsInRange(quint64 startAddr, quint64 endAddr) const;

    // Scroll target: returns the row index that contains `address`.
    Q_INVOKABLE int rowForAddress(quint64 address) const;

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

    std::vector<MemorySegmentInfo> segments_;
    std::vector<MemoryObject> allObjects_;

    // Objects filtered to the current segment's address range, sorted by address.
    std::vector<const MemoryObject*> filteredObjects_;

    int currentSegment_ = 0;
    int bytesPerRow_ = 16;
    int excludedMeasurements_ = 0;
};
