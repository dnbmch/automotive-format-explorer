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
    return seg.address + seg.size;
}

int MemoryMapModel::totalRows() const {
    if (_segments.empty()) {
        return 0;
    }
    const auto& seg = _segments[static_cast<size_t>(_current_segment)];
    return static_cast<int>((seg.size + static_cast<uint64_t>(_bytes_per_row) - 1)
                            / static_cast<uint64_t>(_bytes_per_row));
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

int MemoryMapModel::objectAtAddress(quint64 address) const {
    // Binary search: find the last object whose address <= target address,
    // then check if the address falls within [obj.address, obj.address + obj.size).
    if (_filtered_objects.empty()) {
        return -1;
    }

    // Upper bound on address, then step back.
    auto it = std::upper_bound(
        _filtered_objects.begin(), _filtered_objects.end(), address,
        [](uint64_t addr, const MemoryObject* obj) {
            return addr < obj->address;
        });

    // Check candidates backwards. Objects are sorted by start address only, so
    // an earlier-starting object with a larger footprint can still cover the
    // address — scan every candidate rather than stopping at the first miss.
    while (it != _filtered_objects.begin()) {
        --it;
        const MemoryObject* obj = *it;
        if (obj->address <= address && address < obj->address + obj->size) {
            return static_cast<int>(std::distance(_filtered_objects.begin(), it));
        }
    }
    return -1;
}

QVariantList MemoryMapModel::objectsInRange(quint64 startAddr, quint64 endAddr) const {
    QVariantList result;
    if (_filtered_objects.empty() || startAddr >= endAddr) {
        return result;
    }

    for (size_t i = 0; i < _filtered_objects.size(); ++i) {
        const MemoryObject* obj = _filtered_objects[i];
        uint64_t objEnd = obj->address + obj->size;
        if (objEnd <= startAddr) {
            continue;
        }
        if (obj->address >= endAddr) {
            break; // sorted, no more can overlap
        }
        result.append(static_cast<int>(i));
    }
    return result;
}

int MemoryMapModel::rowForAddress(quint64 address) const {
    if (_segments.empty()) {
        return 0;
    }
    uint64_t start = _segments[static_cast<size_t>(_current_segment)].address;
    if (address < start) {
        return 0;
    }
    return static_cast<int>((address - start) / static_cast<uint64_t>(_bytes_per_row));
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

quint64 MemoryMapModel::objectAddress(int objectIndex) const {
    if (objectIndex < 0 || static_cast<size_t>(objectIndex) >= _filtered_objects.size()) {
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
    // Sort all objects by address.
    std::sort(_all_objects.begin(), _all_objects.end(),
              [](const MemoryObject& a, const MemoryObject& b) {
                  return a.address < b.address;
              });

    // If no segments, derive a synthetic one from object extents.
    if (_segments.empty() && !_all_objects.empty()) {
        uint64_t minAddr = _all_objects.front().address;
        uint64_t maxAddr = 0;
        for (const auto& obj : _all_objects) {
            uint64_t end = obj.address + (obj.size > 0 ? obj.size : 1);
            if (end > maxAddr) {
                maxAddr = end;
            }
        }

        // Align to 256-byte boundaries.
        minAddr = minAddr & ~uint64_t(0xFF);
        maxAddr = (maxAddr + 0xFF) & ~uint64_t(0xFF);

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

    if (!_segments.empty()) {
        const auto& seg = _segments[static_cast<size_t>(_current_segment)];
        uint64_t segStart = seg.address;
        uint64_t segEnd = seg.address + seg.size;

        for (auto& obj : _all_objects) {
            uint64_t objEnd = obj.address + (obj.size > 0 ? obj.size : 1);
            if (objEnd > segStart && obj.address < segEnd) {
                _filtered_objects.push_back(&obj);
            }
        }
    }

    endResetModel();
    emit objectsChanged();
}
