#include "models/signalmapmodel.h"

#include <algorithm>

SignalMapModel::SignalMapModel(QObject* parent)
    : QAbstractListModel(parent) {
}

const std::vector<SignalEntry>& SignalMapModel::currentSignals() const {
    static const std::vector<SignalEntry> empty;
    if (_current_message < 0 || _current_message >= static_cast<int>(_messages.size())) {
        return empty;
    }
    return _messages[_current_message].signalEntries;
}

int SignalMapModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return static_cast<int>(currentSignals().size());
}

QVariant SignalMapModel::data(const QModelIndex& index, int role) const {
    const auto& sigs = currentSignals();
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(sigs.size())) {
        return {};
    }
    const auto& sig = sigs[index.row()];
    switch (role) {
    case NameRole:            return sig.name;
    case StartBitRole:        return sig.startBit;
    case BitLengthRole:       return sig.bitLength;
    case BigEndianRole:       return sig.bigEndian;
    case ColorIndexRole:      return sig.colorIndex;
    case NodeKeyRole:         return sig.nodeKey;
    case MultiplexTypeRole:   return sig.multiplexType;
    case MultiplexValueRole:  return sig.multiplexValue;
    case UnitRole:            return sig.unit;
    }
    return {};
}

QHash<int, QByteArray> SignalMapModel::roleNames() const {
    return {
        {NameRole,           "name"},
        {StartBitRole,       "startBit"},
        {BitLengthRole,      "bitLength"},
        {BigEndianRole,      "bigEndian"},
        {ColorIndexRole,     "colorIndex"},
        {NodeKeyRole,        "nodeKey"},
        {MultiplexTypeRole,  "multiplexType"},
        {MultiplexValueRole, "multiplexValue"},
        {UnitRole,           "unit"},
    };
}

int SignalMapModel::messageCount() const {
    return static_cast<int>(_messages.size());
}

int SignalMapModel::currentMessage() const {
    return _current_message;
}

void SignalMapModel::setCurrentMessage(int index) {
    if (index < 0 || index >= static_cast<int>(_messages.size()) || index == _current_message) {
        return;
    }
    beginResetModel();
    _current_message = index;
    _current_mux_group = -1;
    rebuildMuxGroups();
    rebuildBitMap();
    endResetModel();
    emit currentMessageChanged();
    emit muxGroupChanged();
}

QString SignalMapModel::messageLabel(int index) const {
    if (index < 0 || index >= static_cast<int>(_messages.size())) {
        return {};
    }
    const auto& msg = _messages[index];
    QString idStr = msg.isExtendedId
        ? QStringLiteral("0x%1x").arg(msg.id, 0, 16).toUpper()
        : QStringLiteral("0x%1").arg(msg.id, 0, 16).toUpper();
    return QStringLiteral("%1  %2  DLC=%3").arg(idStr, msg.name).arg(msg.dlc);
}

int SignalMapModel::dlcBytes() const {
    if (_current_message < 0 || _current_message >= static_cast<int>(_messages.size())) {
        return 0;
    }
    return _messages[_current_message].dlc;
}

int SignalMapModel::dlcBits() const {
    return dlcBytes() * 8;
}

int SignalMapModel::signalCount() const {
    return static_cast<int>(currentSignals().size());
}

int SignalMapModel::visibleSignalCount() const {
    if (_current_mux_group < 0) return signalCount();
    int count = 0;
    for (int i = 0; i < signalCount(); ++i) {
        if (isSignalVisible(i)) ++count;
    }
    return count;
}

int SignalMapModel::usedBits() const {
    return _used_bits;
}

QString SignalMapModel::messageName() const {
    if (_current_message < 0 || _current_message >= static_cast<int>(_messages.size())) {
        return {};
    }
    return _messages[_current_message].name;
}

QString SignalMapModel::messageSender() const {
    if (_current_message < 0 || _current_message >= static_cast<int>(_messages.size())) {
        return {};
    }
    return _messages[_current_message].sender;
}

// --- Mux group management ---

bool SignalMapModel::hasMux() const {
    return !_mux_values.empty();
}

int SignalMapModel::muxGroupCount() const {
    return static_cast<int>(_mux_values.size());
}

int SignalMapModel::currentMuxGroup() const {
    return _current_mux_group;
}

void SignalMapModel::setCurrentMuxGroup(int group) {
    if (group == _current_mux_group) return;
    // -1 = all, 0..N-1 = index into _mux_values
    if (group < -1 || group >= static_cast<int>(_mux_values.size())) return;
    _current_mux_group = group;
    rebuildBitMap();
    emit muxGroupChanged();
    // Repaint the grid via model reset.
    beginResetModel();
    endResetModel();
}

QString SignalMapModel::muxGroupLabel(int index) const {
    if (index < 0 || index >= static_cast<int>(_mux_values.size())) {
        return {};
    }
    return QStringLiteral("m%1").arg(_mux_values[index]);
}

bool SignalMapModel::isSignalVisible(int signalIndex) const {
    const auto& sigs = currentSignals();
    if (signalIndex < 0 || signalIndex >= static_cast<int>(sigs.size())) {
        return false;
    }
    if (_current_mux_group < 0) return true; // "All" mode

    const auto& sig = sigs[signalIndex];
    // Static signals and multiplexor always visible.
    if (sig.multiplexType == 0 || sig.multiplexType == 1) return true;
    // Multiplexed: visible if mux value matches selected group.
    int selectedValue = _mux_values[_current_mux_group];
    return sig.multiplexValue == selectedValue;
}

// --- Bit queries ---

int SignalMapModel::signalAtBit(int bitPosition) const {
    if (bitPosition < 0 || bitPosition >= static_cast<int>(_bit_map.size())) {
        return -1;
    }
    return _bit_map[bitPosition];
}

bool SignalMapModel::isOverlap(int bitPosition) const {
    if (bitPosition < 0 || bitPosition >= static_cast<int>(_overlap_map.size())) {
        return false;
    }
    return _overlap_map[bitPosition];
}

int SignalMapModel::signalIndexForNodeKey(quint64 nodeKey) const {
    if (nodeKey == 0) return -1;
    const auto& sigs = currentSignals();
    for (int i = 0; i < static_cast<int>(sigs.size()); ++i) {
        if (sigs[i].nodeKey == nodeKey || sigs[i].signalNodeKey == nodeKey) return i;
    }
    return -1;
}

int SignalMapModel::messageIndexForNodeKey(quint64 nodeKey) const {
    if (nodeKey == 0) return -1;
    for (int m = 0; m < static_cast<int>(_messages.size()); ++m) {
        if (_messages[m].nodeKey == nodeKey) return m;
        for (const auto& sig : _messages[m].signalEntries) {
            if (sig.nodeKey == nodeKey || sig.signalNodeKey == nodeKey) return m;
        }
    }
    return -1;
}

bool SignalMapModel::isValidSignalIndex(int index) const {
    const auto& sigs = currentSignals();
    return index >= 0 && index < static_cast<int>(sigs.size());
}

quint64 SignalMapModel::signalNodeKey(int signalIndex) const {
    if (!isValidSignalIndex(signalIndex)) return 0;
    return currentSignals()[signalIndex].nodeKey;
}

QString SignalMapModel::signalName(int signalIndex) const {
    if (!isValidSignalIndex(signalIndex)) return {};
    return currentSignals()[signalIndex].name;
}

int SignalMapModel::signalColorIndex(int signalIndex) const {
    if (!isValidSignalIndex(signalIndex)) return 0;
    return currentSignals()[signalIndex].colorIndex;
}

int SignalMapModel::signalMuxType(int signalIndex) const {
    if (!isValidSignalIndex(signalIndex)) return 0;
    return currentSignals()[signalIndex].multiplexType;
}

QString SignalMapModel::signalTooltip(int signalIndex) const {
    if (!isValidSignalIndex(signalIndex)) return {};
    const auto& sig = currentSignals()[signalIndex];

    QString orderStr = sig.bigEndian ? QStringLiteral("big-endian") : QStringLiteral("little-endian");
    QString tip = QStringLiteral("%1\nBits: [%2..%3] (%4-bit, %5)")
        .arg(sig.name)
        .arg(sig.startBit)
        .arg(sig.startBit + sig.bitLength - 1)
        .arg(sig.bitLength)
        .arg(orderStr);

    // Scaling.
    if (sig.factor != 0.0 || sig.offset != 0.0) {
        tip += QStringLiteral("\nPhysical: raw");
        if (sig.factor != 1.0) tip += QStringLiteral(" x %1").arg(sig.factor);
        if (sig.offset != 0.0) tip += QStringLiteral(" + %1").arg(sig.offset);
        if (sig.minimum != 0.0 || sig.maximum != 0.0)
            tip += QStringLiteral("  [%1 .. %2]").arg(sig.minimum).arg(sig.maximum);
        if (!sig.unit.isEmpty()) tip += QStringLiteral(" %1").arg(sig.unit);
    } else if (!sig.unit.isEmpty()) {
        tip += QStringLiteral("\nUnit: %1").arg(sig.unit);
    }

    // Mux info.
    if (sig.multiplexType == 1) tip += QStringLiteral("\nMultiplexor (M)");
    else if (sig.multiplexType == 2) tip += QStringLiteral("\nMultiplexed: m%1").arg(sig.multiplexValue);
    else if (sig.multiplexType == 3) tip += QStringLiteral("\nMux: m%1 + multiplexor").arg(sig.multiplexValue);

    // Sender/receivers.
    if (!sig.sender.isEmpty()) tip += QStringLiteral("\nPublisher: %1").arg(sig.sender);
    if (!sig.receivers.isEmpty()) tip += QStringLiteral("\nReceivers: %1").arg(sig.receivers.join(QStringLiteral(", ")));

    return tip;
}

// --- Building ---

void SignalMapModel::addMessage(MessageEntry msg) {
    _messages.push_back(std::move(msg));
}

void SignalMapModel::finalize() {
    if (!_messages.empty()) {
        _current_message = 0;
        _current_mux_group = -1;
        rebuildMuxGroups();
        rebuildBitMap();
    }
    emit messagesChanged();
    emit currentMessageChanged();
    emit muxGroupChanged();
}

void SignalMapModel::rebuildMuxGroups() {
    _mux_values.clear();
    const auto& sigs = currentSignals();
    for (const auto& sig : sigs) {
        if ((sig.multiplexType == 2 || sig.multiplexType == 3) && sig.multiplexValue >= 0) {
            _mux_values.push_back(sig.multiplexValue);
        }
    }
    // Sort and deduplicate.
    std::sort(_mux_values.begin(), _mux_values.end());
    _mux_values.erase(std::unique(_mux_values.begin(), _mux_values.end()), _mux_values.end());
}

void SignalMapModel::rebuildBitMap() {
    const int totalBits = dlcBits();
    _bit_map.assign(totalBits, -1);
    _overlap_map.assign(totalBits, false);
    _used_bits = 0;

    const auto& sigs = currentSignals();
    for (int si = 0; si < static_cast<int>(sigs.size()); ++si) {
        if (!isSignalVisible(si)) continue;

        auto positions = bitPositions(sigs[si]);
        for (int pos : positions) {
            if (pos >= 0 && pos < totalBits) {
                if (_bit_map[pos] < 0) {
                    _bit_map[pos] = si;
                } else if (_bit_map[pos] != si) {
                    _overlap_map[pos] = true;
                }
            }
        }
    }

    for (int32_t v : _bit_map) {
        if (v >= 0) ++_used_bits;
    }
}

// DBC bit numbering resolution.
std::vector<int> SignalMapModel::bitPositions(const SignalEntry& sig) {
    std::vector<int> result;
    if (sig.bitLength <= 0 || sig.startBit < 0) return result;
    // Cap at 512 bits (64 bytes CAN FD max) to guard against malformed data.
    const int len = std::min(sig.bitLength, 512);
    result.reserve(len);

    if (!sig.bigEndian) {
        for (int i = 0; i < len; ++i) {
            int dbcBit = sig.startBit + i;
            int byteIdx = dbcBit / 8;
            int bitInByte = dbcBit % 8;
            result.push_back(byteIdx * 8 + (7 - bitInByte));
        }
    } else {
        int dbcBit = sig.startBit;
        for (int i = 0; i < len; ++i) {
            int byteIdx = dbcBit / 8;
            int bitInByte = dbcBit % 8;
            result.push_back(byteIdx * 8 + (7 - bitInByte));
            if (bitInByte == 0)
                dbcBit += 15;
            else
                dbcBit -= 1;
        }
    }

    return result;
}
