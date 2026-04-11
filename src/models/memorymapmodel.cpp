#include "models/memorymapmodel.h"

#include <algorithm>

MemoryMapModel::MemoryMapModel(QObject* parent)
    : QAbstractListModel(parent) {
}

int MemoryMapModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(filteredObjects_.size());
}

QVariant MemoryMapModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(filteredObjects_.size())) {
        return {};
    }

    const MemoryObject* obj = filteredObjects_[static_cast<size_t>(index.row())];
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
    return static_cast<int>(segments_.size());
}

int MemoryMapModel::currentSegment() const {
    return currentSegment_;
}

void MemoryMapModel::setCurrentSegment(int index) {
    if (index < 0 || index >= static_cast<int>(segments_.size())) {
        return;
    }
    if (currentSegment_ == index) {
        return;
    }
    currentSegment_ = index;
    emit currentSegmentChanged();
    rebuildFilteredObjects();
}

QString MemoryMapModel::segmentLabel(int index) const {
    if (index < 0 || index >= static_cast<int>(segments_.size())) {
        return {};
    }
    const auto& seg = segments_[static_cast<size_t>(index)];
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
    if (segments_.empty()) {
        return 0;
    }
    return segments_[static_cast<size_t>(currentSegment_)].address;
}

quint64 MemoryMapModel::viewEndAddress() const {
    if (segments_.empty()) {
        return 0;
    }
    const auto& seg = segments_[static_cast<size_t>(currentSegment_)];
    return seg.address + seg.size;
}

int MemoryMapModel::totalRows() const {
    if (segments_.empty()) {
        return 0;
    }
    const auto& seg = segments_[static_cast<size_t>(currentSegment_)];
    return static_cast<int>((seg.size + static_cast<uint64_t>(bytesPerRow_) - 1)
                            / static_cast<uint64_t>(bytesPerRow_));
}

int MemoryMapModel::bytesPerRow() const {
    return bytesPerRow_;
}

void MemoryMapModel::setBytesPerRow(int bpr) {
    if (bpr != 8 && bpr != 16 && bpr != 32) {
        return;
    }
    if (bytesPerRow_ == bpr) {
        return;
    }
    bytesPerRow_ = bpr;
    emit bytesPerRowChanged();
    emit currentSegmentChanged(); // totalRows changed too
}

int MemoryMapModel::objectCount() const {
    return static_cast<int>(filteredObjects_.size());
}

int MemoryMapModel::totalObjectCount() const {
    return static_cast<int>(allObjects_.size());
}

int MemoryMapModel::excludedMeasurementCount() const {
    return excludedMeasurements_;
}

int MemoryMapModel::objectAtAddress(quint64 address) const {
    // Binary search: find the last object whose address <= target address,
    // then check if the address falls within [obj.address, obj.address + obj.size).
    if (filteredObjects_.empty()) {
        return -1;
    }

    // Upper bound on address, then step back.
    auto it = std::upper_bound(
        filteredObjects_.begin(), filteredObjects_.end(), address,
        [](uint64_t addr, const MemoryObject* obj) {
            return addr < obj->address;
        });

    // Check candidates backwards (there may be overlapping objects).
    while (it != filteredObjects_.begin()) {
        --it;
        const MemoryObject* obj = *it;
        if (obj->address + obj->size <= address) {
            // Past this object and all earlier ones are even further left.
            break;
        }
        if (obj->address <= address && address < obj->address + obj->size) {
            return static_cast<int>(std::distance(filteredObjects_.begin(), it));
        }
    }
    return -1;
}

QVariantList MemoryMapModel::objectsInRange(quint64 startAddr, quint64 endAddr) const {
    QVariantList result;
    if (filteredObjects_.empty() || startAddr >= endAddr) {
        return result;
    }

    for (size_t i = 0; i < filteredObjects_.size(); ++i) {
        const MemoryObject* obj = filteredObjects_[i];
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
    if (segments_.empty()) {
        return 0;
    }
    uint64_t start = segments_[static_cast<size_t>(currentSegment_)].address;
    if (address < start) {
        return 0;
    }
    return static_cast<int>((address - start) / static_cast<uint64_t>(bytesPerRow_));
}

int MemoryMapModel::objectIndexForNodeKey(quint64 nodeKey) const {
    if (nodeKey == 0) {
        return -1;
    }
    for (size_t i = 0; i < filteredObjects_.size(); ++i) {
        if (filteredObjects_[i]->nodeKey == nodeKey) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

quint64 MemoryMapModel::objectAddress(int objectIndex) const {
    if (objectIndex < 0 || static_cast<size_t>(objectIndex) >= filteredObjects_.size()) {
        return 0;
    }
    return filteredObjects_[static_cast<size_t>(objectIndex)]->address;
}

void MemoryMapModel::addSegment(MemorySegmentInfo seg) {
    segments_.push_back(std::move(seg));
}

void MemoryMapModel::addObject(MemoryObject obj) {
    allObjects_.push_back(std::move(obj));
}

void MemoryMapModel::setExcludedMeasurementCount(int count) {
    excludedMeasurements_ = count;
}

void MemoryMapModel::finalize() {
    // Sort all objects by address.
    std::sort(allObjects_.begin(), allObjects_.end(),
              [](const MemoryObject& a, const MemoryObject& b) {
                  return a.address < b.address;
              });

    // If no segments, derive a synthetic one from object extents.
    if (segments_.empty() && !allObjects_.empty()) {
        uint64_t minAddr = allObjects_.front().address;
        uint64_t maxAddr = 0;
        for (const auto& obj : allObjects_) {
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
        segments_.push_back(std::move(synthetic));
    }

    emit segmentsChanged();
    rebuildFilteredObjects();
}

void MemoryMapModel::rebuildFilteredObjects() {
    beginResetModel();
    filteredObjects_.clear();

    if (!segments_.empty()) {
        const auto& seg = segments_[static_cast<size_t>(currentSegment_)];
        uint64_t segStart = seg.address;
        uint64_t segEnd = seg.address + seg.size;

        for (auto& obj : allObjects_) {
            uint64_t objEnd = obj.address + (obj.size > 0 ? obj.size : 1);
            if (objEnd > segStart && obj.address < segEnd) {
                filteredObjects_.push_back(&obj);
            }
        }
    }

    endResetModel();
    emit objectsChanged();
}
