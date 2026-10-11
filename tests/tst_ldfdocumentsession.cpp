// An LDF through the production adapter, the real parser and the session: the
// signal map's tooltip states each signal's physical range, its encoding's raw
// bounds converted by the encoding's factor and offset.

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
    void cleanupTestCase();

private:
    QTemporaryDir _dir;
    LoadResult _loaded;
    SignalMapModel* _model = nullptr;
};

void TestLdfDocumentSession::initTestCase() {
    QVERIFY(_dir.isValid());
    const QString path = _dir.filePath(QStringLiteral("sensor.ldf"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(kLdf, sizeof(kLdf) - 1), qint64(sizeof(kLdf) - 1));
    file.close();

    const std::atomic<bool> cancel{false};
    _loaded = LdfAdapter().load(path, cancel);
    QVERIFY(_loaded.session);
    QVERIFY2(_loaded.diagnostics.isEmpty(),
             qPrintable(_loaded.diagnostics.isEmpty() ? QString()
                                                      : _loaded.diagnostics.front().title));
    _model = qobject_cast<SignalMapModel*>(_loaded.session->centerPanelModel());
    QVERIFY(_model);
    QCOMPARE(_model->messageCount(), 1);
    QCOMPARE(_model->messageName(), QStringLiteral("SensorStatus"));
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

void TestLdfDocumentSession::cleanupTestCase() {
    _loaded.session.reset();
}

QTEST_GUILESS_MAIN(TestLdfDocumentSession)
#include "tst_ldfdocumentsession.moc"
