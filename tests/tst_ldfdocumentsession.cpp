// An LDF through the production adapter, the real parser and the session: the
// signal map's tooltip states each signal's physical range, its encoding's raw
// bounds converted by the encoding's factor and offset, and the map lays each
// signal out in its file's byte order.

#include "adapters/ldfadapter.h"
#include "models/signalmapmodel.h"
#include "sessions/documentsession.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <atomic>

namespace {

// One frame of two signals: a positive factor with an offset, and a negative
// factor whose converted raw bounds swap order.
constexpr char kLdf[] = R"(LIN_description_file;
LIN_protocol_version = "2.2";
LIN_language_version = "2.2";
LIN_speed = 19.2 kbps;

Nodes {
    Master: Body, 5 ms, 0.1 ms;
    Slaves: Sensor;
}

Signals {
    Temperature: 8, 0, Sensor, Body;
    Level: 8, 0, Sensor, Body;
}

Frames {
    SensorStatus: 0x20, Sensor, 2 {
        Temperature, 0;
        Level, 8;
    }
}

Signal_encoding_types {
    TempEncoding {
        physical_value, 0, 250, 0.5, -40, "degC";
        logical_value, 255, "fault";
    }
    LevelEncoding {
        physical_value, 0, 200, -0.25, 100, "%";
    }
}

Signal_representation {
    TempEncoding: Temperature;
    LevelEncoding: Level;
}
)";

// An ISO 17987 file declaring big-endian signals. Each scalar's offset names its
// MSB: Speed fills bytes 0-1, Flag the top nibble of byte 2, Level the low nibble
// of byte 2 and byte 3. Serial, a byte array, keeps the little-endian layout from
// its offset, bytes 4-5.
constexpr char kBigEndianLdf[] = R"(LIN_description_file;
LIN_protocol_version = "ISO17987:2015";
LIN_language_version = "ISO17987:2015";
LIN_speed = 19.2 kbps;
LIN_sig_byte_order_big_endian;

Nodes {
    Master: Body, 5 ms, 0.1 ms;
    Slaves: Sensor;
}

Signals {
    Speed: 16, 0, Body, Sensor;
    Flag: 4, 0, Body, Sensor;
    Level: 12, 0, Body, Sensor;
    Serial: 16, {1, 2}, Body, Sensor;
}

Frames {
    Drive: 0x21, Body, 6 {
        Speed, 7;
        Flag, 23;
        Level, 19;
        Serial, 32;
    }
}
)";

LoadResult loadLdf(const QTemporaryDir& dir, const QString& name, const char* ldf, size_t size) {
    const QString path = dir.filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(ldf, qint64(size)) != qint64(size)) return {};
    file.close();
    const std::atomic<bool> cancel{false};
    return LdfAdapter().load(path, cancel);
}

QList<int> range(int first, int last) {
    QList<int> bits;
    for (int bit = first; bit <= last; ++bit) bits.push_back(bit);
    return bits;
}

// The bracketed range on the tooltip's `Physical:` line.
QString physicalRange(const QString& tooltip) {
    for (const QString& line : tooltip.split(QLatin1Char('\n'))) {
        if (line.startsWith(QStringLiteral("Physical:"))) {
            const qsizetype open = line.indexOf(QLatin1Char('['));
            const qsizetype close = line.indexOf(QLatin1Char(']'), open);
            return open < 0 || close < 0 ? QString() : line.mid(open + 1, close - open - 1);
        }
    }
    return {};
}

} // namespace

class TestLdfDocumentSession : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void tooltipRangeIsPhysical_data();
    void tooltipRangeIsPhysical();
    void layoutFollowsByteOrder_data();
    void layoutFollowsByteOrder();
    void cleanupTestCase();

private:
    QTemporaryDir _dir;
    LoadResult _loaded;
    LoadResult _big_endian;
    SignalMapModel* _model = nullptr;
    SignalMapModel* _big_endian_model = nullptr;
};

void TestLdfDocumentSession::initTestCase() {
    QVERIFY(_dir.isValid());
    _loaded = loadLdf(_dir, QStringLiteral("sensor.ldf"), kLdf, sizeof(kLdf) - 1);
    QVERIFY(_loaded.session);
    QVERIFY2(_loaded.diagnostics.isEmpty(),
             qPrintable(_loaded.diagnostics.isEmpty() ? QString()
                                                      : _loaded.diagnostics.front().title));
    _model = qobject_cast<SignalMapModel*>(_loaded.session->centerPanelModel());
    QVERIFY(_model);
    QCOMPARE(_model->messageCount(), 1);
    QCOMPARE(_model->messageName(), QStringLiteral("SensorStatus"));

    _big_endian = loadLdf(_dir, QStringLiteral("drive.ldf"), kBigEndianLdf, sizeof(kBigEndianLdf) - 1);
    QVERIFY(_big_endian.session);
    QVERIFY(_big_endian.diagnostics.isEmpty());
    _big_endian_model = qobject_cast<SignalMapModel*>(_big_endian.session->centerPanelModel());
    QVERIFY(_big_endian_model);
    QCOMPARE(_big_endian_model->messageCount(), 1);
    QCOMPARE(_big_endian_model->dlcBytes(), 6);
}

void TestLdfDocumentSession::tooltipRangeIsPhysical_data() {
    QTest::addColumn<int>("signalIndex");
    QTest::addColumn<QString>("name");
    QTest::addColumn<QString>("range");

    // raw 0..250 x 0.5 - 40
    QTest::newRow("factor and offset") << 0 << "Temperature" << "-40 .. 85";
    // raw 0..200 x -0.25 + 100: raw 200 is the physical minimum
    QTest::newRow("negative factor") << 1 << "Level" << "50 .. 100";
}

void TestLdfDocumentSession::tooltipRangeIsPhysical() {
    QFETCH(int, signalIndex);
    QFETCH(QString, name);
    QFETCH(QString, range);

    QCOMPARE(_model->signalName(signalIndex), name);
    QCOMPARE(physicalRange(_model->signalTooltip(signalIndex)), range);
}

void TestLdfDocumentSession::layoutFollowsByteOrder_data() {
    QTest::addColumn<bool>("bigEndianFile");
    QTest::addColumn<int>("signalIndex");
    QTest::addColumn<QString>("name");
    QTest::addColumn<bool>("bigEndian");
    QTest::addColumn<QList<int>>("displayBits");

    // Display bit = byte * 8 + (7 - bit in byte): byte 0's bit 7 is display bit 0.
    QTest::newRow("LIN 2.2 byte 0") << false << 0 << "Temperature" << false << range(0, 7);
    QTest::newRow("LIN 2.2 byte 1") << false << 1 << "Level" << false << range(8, 15);
    QTest::newRow("16-bit scalar, MSB at 7") << true << 0 << "Speed" << true << range(0, 15);
    QTest::newRow("4-bit scalar, MSB at 23") << true << 1 << "Flag" << true << range(16, 19);
    QTest::newRow("12-bit scalar across bytes 2-3") << true << 2 << "Level" << true << range(20, 31);
    QTest::newRow("byte array") << true << 3 << "Serial" << false << range(32, 47);
}

// Each signal owns exactly its bits, and its tooltip names its byte order.
void TestLdfDocumentSession::layoutFollowsByteOrder() {
    QFETCH(bool, bigEndianFile);
    QFETCH(int, signalIndex);
    QFETCH(QString, name);
    QFETCH(bool, bigEndian);
    QFETCH(QList<int>, displayBits);

    const SignalMapModel* model = bigEndianFile ? _big_endian_model : _model;
    QCOMPARE(model->signalName(signalIndex), name);
    QCOMPARE(model->data(model->index(signalIndex, 0), SignalMapModel::BigEndianRole).toBool(), bigEndian);
    QVERIFY(model->signalTooltip(signalIndex)
                .contains(bigEndian ? QStringLiteral("big-endian") : QStringLiteral("little-endian")));

    QList<int> owned;
    for (int bit = 0; bit < model->dlcBits(); ++bit) {
        if (model->signalAtBit(bit) == signalIndex) owned.push_back(bit);
        QVERIFY(!model->isOverlap(bit));
    }
    QCOMPARE(owned, displayBits);
}

void TestLdfDocumentSession::cleanupTestCase() {
    _big_endian.session.reset();
    _loaded.session.reset();
}

QTEST_GUILESS_MAIN(TestLdfDocumentSession)
#include "tst_ldfdocumentsession.moc"
