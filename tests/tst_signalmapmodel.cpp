// The signal map model's hover tooltip names a signal's bits in its own byte
// order: a Motorola signal from its MSB to its LSB, an Intel one as a range.

#include "models/signalmapmodel.h"

#include <QTest>

namespace {

SignalEntry signal(const QString& name, int startBit, int bitLength, bool bigEndian) {
    SignalEntry sig;
    sig.name = name;
    sig.startBit = startBit;
    sig.bitLength = bitLength;
    sig.bigEndian = bigEndian;
    return sig;
}

// The tooltip's `Bits:` line.
QString bitsLine(const QString& tooltip) {
    for (const QString& line : tooltip.split(QLatin1Char('\n'))) {
        if (line.startsWith(QStringLiteral("Bits:"))) return line;
    }
    return {};
}

} // namespace

class TestSignalMapModel : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void tooltipBits_data();
    void tooltipBits();

private:
    SignalMapModel _model;
};

// One eight-byte frame: a 12-bit Motorola signal from bit 13, bits 13-8 of byte 1
// then 23-18 of byte 2, and a 12-bit Intel signal over bits 28-39.
void TestSignalMapModel::initTestCase() {
    MessageEntry message;
    message.name = QStringLiteral("Frame");
    message.dlc = 8;
    message.signalEntries.push_back(signal(QStringLiteral("Motorola"), 13, 12, true));
    message.signalEntries.push_back(signal(QStringLiteral("Intel"), 28, 12, false));
    _model.addMessage(std::move(message));
    _model.finalize();
}

void TestSignalMapModel::tooltipBits_data() {
    QTest::addColumn<int>("signalIndex");
    QTest::addColumn<QString>("bits");

    QTest::newRow("Motorola") << 0 << "Bits: MSB 13 .. LSB 18 (12-bit, big-endian)";
    QTest::newRow("Intel") << 1 << "Bits: [28..39] (12-bit, little-endian)";
}

void TestSignalMapModel::tooltipBits() {
    QFETCH(int, signalIndex);
    QFETCH(QString, bits);
    QCOMPARE(bitsLine(_model.signalTooltip(signalIndex)), bits);
}

QTEST_GUILESS_MAIN(TestSignalMapModel)
#include "tst_signalmapmodel.moc"
