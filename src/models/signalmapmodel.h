#pragma once

#include <QAbstractListModel>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <vector>

struct SignalEntry {
    QString name;
    int startBit = 0;
    int bitLength = 0;
    bool bigEndian = false;
    bool isSigned = false;
    double factor = 1.0;
    double offset = 0.0;
    double minimum = 0.0;
    double maximum = 0.0;
    QString unit;
    int colorIndex = 0;
    quint64 nodeKey = 0;          // FrameSignal tree key
    quint64 signalNodeKey = 0;    // Standalone Signal tree key
    int multiplexType = 0;   // 0=none, 1=multiplexor, 2=multiplexed, 3=both
    int multiplexValue = -1;
    QString sender;
    QStringList receivers;
};

struct MessageEntry {
    QString name;
    uint32_t id = 0;
    int dlc = 0;
    bool isExtendedId = false;
    QString sender;
    std::vector<SignalEntry> signalEntries;
    quint64 nodeKey = 0;
};

// SignalMapModel: list model exposing message/signal layout to QML.
//
// Two-level data: a list of messages (selectable via dropdown), and for each
// message a list of signals (exposed as the list model rows).
//
// Multiplexing: when a message has multiplexed signals, the model exposes
// mux groups. currentMuxGroup -1 = show all, >= 0 = filter to that mux value.
// Static signals and the multiplexor are always visible regardless of filter.

class SignalMapModel : public QAbstractListModel {
    Q_OBJECT

    Q_PROPERTY(int messageCount READ messageCount NOTIFY messagesChanged)
    Q_PROPERTY(int currentMessage READ currentMessage WRITE setCurrentMessage NOTIFY currentMessageChanged)
    Q_PROPERTY(int dlcBytes READ dlcBytes NOTIFY currentMessageChanged)
    Q_PROPERTY(int dlcBits READ dlcBits NOTIFY currentMessageChanged)
    Q_PROPERTY(int signalCount READ signalCount NOTIFY currentMessageChanged)
    Q_PROPERTY(int visibleSignalCount READ visibleSignalCount NOTIFY muxGroupChanged)
    Q_PROPERTY(int usedBits READ usedBits NOTIFY muxGroupChanged)
    Q_PROPERTY(QString messageName READ messageName NOTIFY currentMessageChanged)
    Q_PROPERTY(QString messageSender READ messageSender NOTIFY currentMessageChanged)
    Q_PROPERTY(bool hasMux READ hasMux NOTIFY currentMessageChanged)
    Q_PROPERTY(int muxGroupCount READ muxGroupCount NOTIFY currentMessageChanged)
    Q_PROPERTY(int currentMuxGroup READ currentMuxGroup WRITE setCurrentMuxGroup NOTIFY muxGroupChanged)

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        StartBitRole,
        BitLengthRole,
        BigEndianRole,
        ColorIndexRole,
        NodeKeyRole,
        MultiplexTypeRole,
        MultiplexValueRole,
        UnitRole,
    };

    explicit SignalMapModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Message management.
    int messageCount() const;
    int currentMessage() const;
    void setCurrentMessage(int index);

    Q_INVOKABLE QString messageLabel(int index) const;

    // Current message properties.
    int dlcBytes() const;
    int dlcBits() const;
    int signalCount() const;       // total signals in message
    int visibleSignalCount() const; // signals visible with current mux filter
    int usedBits() const;
    QString messageName() const;
    QString messageSender() const;

    // Mux group management.
    bool hasMux() const;
    int muxGroupCount() const;
    int currentMuxGroup() const;
    void setCurrentMuxGroup(int group);
    Q_INVOKABLE QString muxGroupLabel(int index) const;

    // Bit queries (for grid rendering).
    // Returns signal index at the given bit, or -1 if unoccupied.
    // Returns index into the FULL signal list (not filtered).
    Q_INVOKABLE int signalAtBit(int bitPosition) const;

    // Is signal visible with current mux filter?
    Q_INVOKABLE bool isSignalVisible(int signalIndex) const;

    // Is a bit position overlapped by multiple signals?
    Q_INVOKABLE bool isOverlap(int bitPosition) const;

    // Lookup by nodeKey (for tree->grid navigation).
    Q_INVOKABLE int signalIndexForNodeKey(quint64 nodeKey) const;
    Q_INVOKABLE int messageIndexForNodeKey(quint64 nodeKey) const;

    // Signal properties by index (for QML legend/tooltip).
    Q_INVOKABLE quint64 signalNodeKey(int signalIndex) const;
    Q_INVOKABLE QString signalName(int signalIndex) const;
    Q_INVOKABLE int signalColorIndex(int signalIndex) const;
    Q_INVOKABLE int signalMuxType(int signalIndex) const;
    Q_INVOKABLE QString signalTooltip(int signalIndex) const;

    // Building (called by session during construction).
    void addMessage(MessageEntry msg);
    void finalize();

    // Resolve DBC bit numbering to a list of physical display positions.
    // Static so sessions can reuse it for DLC derivation.
    static std::vector<int> bitPositions(const SignalEntry& sig);

signals:
    void messagesChanged();
    void currentMessageChanged();
    void muxGroupChanged();

private:
    void rebuildBitMap();
    void rebuildMuxGroups();

    const std::vector<SignalEntry>& currentSignals() const;

    std::vector<MessageEntry> messages_;
    int currentMessage_ = 0;

    // Pre-computed bit map for current message + mux filter.
    std::vector<int16_t> bitMap_;
    std::vector<bool> overlapMap_; // true if multiple signals claim this bit
    int usedBits_ = 0;

    // Mux groups for current message: sorted unique mux values.
    std::vector<int> muxValues_;
    int currentMuxGroup_ = -1; // -1 = all
};
