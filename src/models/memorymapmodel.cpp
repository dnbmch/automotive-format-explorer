#include "models/memorymapmodel.h"

#include <algorithm>

MemoryMapModel::MemoryMapModel(QObject* parent)
    : QAbstractListModel(parent) {
}

int MemoryMapModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(_filtered_objects.size());
}

QVariant MemoryMapModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(_filtered_objects.size())) {
        return {};
    }

    const MemoryObject* obj = _filtered_objects[static_cast<size_t>(index.row())];
    switch (role) {
    case NameRole:          return obj->name;
    case AddressRole:       return QVariant::fromValue(static_cast<qulonglong>(obj->address));
    case SizeRole:          return QVariant::fromValue(static_cast<qulonglong>(obj->size));
    case TypeNameRole:      return obj->typeName;
    case ColorIndexRole:    return obj->colorIndex;
    case SizeApproximateRole: return obj->sizeApproximate;
    case NodeKeyRole:       return QVariant::fromValue(static_cast<qulonglong>(obj->nodeKey));
    case RecordLayoutRole:  return obj->recordLayoutRef;
    case ConversionRole:    return obj->conversion;
    }
    return {};
}

QHash<int, QByteArray> MemoryMapModel::roleNames() const {
    return {
        {NameRole, "name"},
        {AddressRole, "address"},
        {SizeRole, "size"},
        {TypeNameRole, "typeName"},
        {ColorIndexRole, "colorIndex"},
        {SizeApproximateRole, "sizeApproximate"},
        {NodeKeyRole, "nodeKey"},
        {RecordLayoutRole, "recordLayout"},
        {ConversionRole, "conversion"},
    };
}

int MemoryMapModel::segmentCount() const {
    return static_cast<int>(_segments.size());
}

int MemoryMapModel::currentSegment() const {
    return _current_segment;
}

void MemoryMapModel::setCurrentSegment(int index) {
    if (index < 0 || index >= static_cast<int>(_segments.size())) {
        return;
    }
    if (_current_segment == index) {
        return;
    }
    _current_segment = index;
    emit currentSegmentChanged();
    rebuildFilteredObjects();
}

QString MemoryMapModel::segmentLabel(int index) const {
    if (index < 0 || index >= static_cast<int>(_segments.size())) {
        return {};
    }
    const auto& seg = _segments[static_cast<size_t>(index)];
    if (seg.synthetic) {
        return QStringLiteral("[derived]  [0x%1 .. 0x%2]")
            .arg(seg.address, 0, 16)
            .arg(seg.address + seg.size - 1, 0, 16)
            .toUpper();
    }
    return QStringLiteral("%1  [0x%2 .. 0x%3]  %4 / %5")
        .arg(seg.name)
        .arg(seg.address, 0, 16)
        .arg(seg.address + seg.size - 1, 0, 16)
        .arg(seg.memoryType, seg.prgType)
        .toUpper();
}

quint64 MemoryMapModel::viewStartAddress() const {
    if (_segments.empty()) {
        return 0;
    }
    return _segments[static_cast<size_t>(_current_segment)].address;
}

quint64 MemoryMapModel::viewEndAddress() const {
    if (_segments.empty()) {
        return 0;
    }
    const auto& seg = _segments[static_cast<size_t>(_current_segment)];
    return addressEnd(seg.address, seg.size);
}

quint64 MemoryMapModel::totalRows() const {
    const uint64_t span = viewEndAddress() - viewStartAddress();
    const auto bpr = static_cast<uint64_t>(_bytes_per_row);
    return span / bpr + (span % bpr != 0 ? 1 : 0);
}

int MemoryMapModel::bytesPerRow() const {
    return _bytes_per_row;
}

void MemoryMapModel::setBytesPerRow(int bpr) {
    if (bpr != 8 && bpr != 16 && bpr != 32) {
        return;
    }
    if (_bytes_per_row == bpr) {
        return;
    }
    _bytes_per_row = bpr;
    emit bytesPerRowChanged();
    emit currentSegmentChanged(); // totalRows changed too
}

int MemoryMapModel::objectCount() const {
    return static_cast<int>(_filtered_objects.size());
}

int MemoryMapModel::totalObjectCount() const {
    return static_cast<int>(_all_objects.size());
}

int MemoryMapModel::excludedMeasurementCount() const {
    return _excluded_measurements;
}

MemoryTile MemoryMapModel::queryBytes(uint64_t start, uint64_t count) const {
    MemoryTile tile;
    tile.start = start;
    tile.owner.assign(static_cast<size_t>(count), -1);
    tile.overlap.assign(static_cast<size_t>(count), 0);

    const uint64_t first = std::max<uint64_t>(start, viewStartAddress());
    const uint64_t last = std::min<uint64_t>(addressEnd(start, count), viewEndAddress());
    if (first >= last) {
        return tile;
    }

    // Rows before `from` end at or before `first`; rows from `to` start at or
    // after `last`. Later rows claim over earlier ones, as they are drawn.
    const auto from = static_cast<size_t>(
        std::upper_bound(_reach_end.begin(), _reach_end.end(), first) - _reach_end.begin());
    const auto to = static_cast<size_t>(
        std::lower_bound(_filtered_objects.begin(), _filtered_objects.end(), last,
                         [](const MemoryObject* obj, uint64_t address) {
                             return obj->address < address;
                         })
        - _filtered_objects.begin());
    for (size_t row = from; row < to; ++row) {
        const MemoryObject* obj = _filtered_objects[row];
        const uint64_t claimFirst = std::max(obj->address, first);
        const uint64_t claimLast = std::min(addressEnd(obj->address, obj->size), last);
        for (uint64_t address = claimFirst; address < claimLast; ++address) {
            const auto i = static_cast<size_t>(address - start);
            if (tile.owner[i] >= 0) {
                tile.overlap[i] = 1;
            }
            tile.owner[i] = static_cast<int32_t>(row);
        }
    }
    return tile;
}

int MemoryMapModel::objectAtAddress(quint64 address) const {
    return queryBytes(address, 1).owner.front();
}

QVariantList MemoryMapModel::objectsInRange(quint64 startAddr, quint64 endAddr) const {
    QVariantList result;
    if (_filtered_objects.empty() || startAddr >= endAddr) {
        return result;
    }

    for (size_t i = 0; i < _filtered_objects.size(); ++i) {
        const MemoryObject* obj = _filtered_objects[i];
        if (addressEnd(obj->address, obj->size) <= startAddr) {
            continue;
        }
        if (obj->address >= endAddr) {
            break; // sorted, no more can overlap
        }
        result.append(static_cast<int>(i));
    }
    return result;
}

bool MemoryMapModel::isOverlap(quint64 address) const {
    return queryBytes(address, 1).overlap.front() != 0;
}

quint64 MemoryMapModel::rowForAddress(quint64 address) const {
    const quint64 start = viewStartAddress();
    if (address < start) {
        return 0;
    }
    return (address - start) / static_cast<uint64_t>(_bytes_per_row);
}

int MemoryMapModel::objectIndexForNodeKey(quint64 nodeKey) const {
    if (nodeKey == 0) {
        return -1;
    }
    for (size_t i = 0; i < _filtered_objects.size(); ++i) {
        if (_filtered_objects[i]->nodeKey == nodeKey) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int MemoryMapModel::segmentIndexForNodeKey(quint64 nodeKey) const {
    if (nodeKey == 0) {
        return -1;
    }
    for (const auto& obj : _all_objects) {
        if (obj.nodeKey != nodeKey) {
            continue;
        }
        const uint64_t objEnd = addressEnd(obj.address, obj.size > 0 ? obj.size : 1);
        for (size_t s = 0; s < _segments.size(); ++s) {
            const auto& seg = _segments[s];
            if (objEnd > seg.address && obj.address < addressEnd(seg.address, seg.size)) {
                return static_cast<int>(s);
            }
        }
        return -1;
    }
    return -1;
}

quint64 MemoryMapModel::objectAddress(int objectIndex) const {
    if (static_cast<size_t>(objectIndex) >= _filtered_objects.size()) {
        return 0;
    }
    return _filtered_objects[static_cast<size_t>(objectIndex)]->address;
}

void MemoryMapModel::addSegment(MemorySegmentInfo seg) {
    _segments.push_back(std::move(seg));
}

void MemoryMapModel::addObject(MemoryObject obj) {
    _all_objects.push_back(std::move(obj));
}

void MemoryMapModel::setExcludedMeasurementCount(int count) {
    _excluded_measurements = count;
}

void MemoryMapModel::finalize() {
    // Sort all objects by address; objects at one address keep document order,
    // which decides who owns their shared bytes.
    std::stable_sort(_all_objects.begin(), _all_objects.end(),
                     [](const MemoryObject& a, const MemoryObject& b) {
                         return a.address < b.address;
                     });

    // If no segments, derive a synthetic one from object extents.
    if (_segments.empty() && !_all_objects.empty()) {
        uint64_t minAddr = _all_objects.front().address;
        uint64_t maxAddr = 0;
        for (const auto& obj : _all_objects) {
            maxAddr = std::max(maxAddr, addressEnd(obj.address, obj.size > 0 ? obj.size : 1));
        }

        // Align to 256-byte boundaries; an end in the last 256 bytes of the
        // address space cannot round up and stays at the top.
        minAddr = minAddr & ~uint64_t(0xFF);
        const uint64_t alignedEnd = addressEnd(maxAddr, 0xFF) & ~uint64_t(0xFF);
        maxAddr = alignedEnd >= maxAddr ? alignedEnd : std::numeric_limits<uint64_t>::max();

        MemorySegmentInfo synthetic;
        synthetic.name = QStringLiteral("[derived]");
        synthetic.address = minAddr;
        synthetic.size = maxAddr - minAddr;
        synthetic.synthetic = true;
        _segments.push_back(std::move(synthetic));
    }

    emit segmentsChanged();
    rebuildFilteredObjects();
}

void MemoryMapModel::rebuildFilteredObjects() {
    beginResetModel();
    _filtered_objects.clear();
    _reach_end.clear();

    if (!_segments.empty()) {
        const uint64_t segStart = viewStartAddress();
        const uint64_t segEnd = viewEndAddress();

        // An object of unknown size (0) still counts as one byte for segment
        // membership, so it stays listed; it claims no bytes.
        uint64_t reach = 0;
        for (auto& obj : _all_objects) {
            if (addressEnd(obj.address, obj.size > 0 ? obj.size : 1) > segStart
                && obj.address < segEnd) {
                _filtered_objects.push_back(&obj);
                reach = std::max(reach, addressEnd(obj.address, obj.size));
                _reach_end.push_back(reach);
            }
        }
    }

    endResetModel();
    emit objectsChanged();
}
