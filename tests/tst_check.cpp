#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QTest>

// Run the real application's check from an isolated installation. Only its
// sample payload changes; no test edits the source or build-tree samples.
class TestCheck : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void completeBundle();
    void missingSample_data();
    void missingSample();
    void corruptMdf4();
    void explicitFiles_data();
    void explicitFiles();
    void unicodePaths();

private:
    void check(const QStringList& files, int expectedExit, const QByteArray& report);

    QTemporaryDir _dir;
    QString _app;
    QString _samples;
};

void TestCheck::initTestCase() {
    QVERIFY(_dir.isValid());
    const QString appDir = _dir.filePath(QStringLiteral("app"));
    QVERIFY(QDir().mkpath(appDir));
    _app = QDir(appDir).filePath(QFileInfo(QStringLiteral(EXPLORER_EXECUTABLE)).fileName());
    QVERIFY(QFile::copy(QStringLiteral(EXPLORER_EXECUTABLE), _app));
    _samples = QDir(appDir).filePath(QStringLiteral("samples"));
    QVERIFY(QDir().mkpath(_samples));
}

void TestCheck::init() {
    const QDir source(QStringLiteral(EXPLORER_SAMPLES_DIR));
    for (const QString& name : source.entryList(QDir::Files)) {
        const QString target = QDir(_samples).filePath(name);
        if (QFile::exists(target)) {
            QVERIFY(QFile::remove(target));
        }
        QVERIFY(QFile::copy(source.filePath(name), target));
    }
}

void TestCheck::check(const QStringList& files, int expectedExit, const QByteArray& report) {
    QProcess process;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
    env.insert(QStringLiteral("QT_QUICK_BACKEND"), QStringLiteral("software"));
    process.setProcessEnvironment(env);
    process.setWorkingDirectory(_dir.path());
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(_app, QStringList{QStringLiteral("--check")} + files);
    QVERIFY2(process.waitForStarted(), qPrintable(process.errorString()));
    if (!process.waitForFinished(15000)) {
        process.kill();
        process.waitForFinished();
        QFAIL("the application's check did not finish");
    }
    const QByteArray output = process.readAll();
    QCOMPARE(process.exitStatus(), QProcess::NormalExit);
    QVERIFY2(process.exitCode() == expectedExit, output.constData());
    QVERIFY2(output.contains(report), output.constData());
}

void TestCheck::completeBundle() {
    check({}, 0, "passed: 4 file(s)");
}

void TestCheck::missingSample_data() {
    QTest::addColumn<QString>("sample");
    for (const char* name : {"demo_ecu.a2l", "tesla_can.dbc", "demo_seat.ldf", "demo_recording.mf4"}) {
        QTest::newRow(name) << QString::fromLatin1(name);
    }
}

void TestCheck::missingSample() {
    QFETCH(QString, sample);
    QVERIFY(QFile::remove(QDir(_samples).filePath(sample)));
    check({}, 1, sample.toUtf8());
}

void TestCheck::corruptMdf4() {
    QFile file(QDir(_samples).filePath(QStringLiteral("demo_recording.mf4")));
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(file.write("not an MDF4 file\n"), qint64(17));
    file.close();
    check({}, 1, "demo_recording.mf4:");
}

void TestCheck::explicitFiles_data() {
    QTest::addColumn<QString>("file");
    QTest::addColumn<int>("expectedExit");
    QTest::newRow("valid-mdf4") << QStringLiteral("app/samples/demo_recording.mf4") << 0;
    QTest::newRow("missing-mdf4") << QStringLiteral("missing.mf4") << 1;
    QTest::newRow("missing-a2l") << QStringLiteral("missing.a2l") << 1;
}

void TestCheck::explicitFiles() {
    QFETCH(QString, file);
    QFETCH(int, expectedExit);
    check({file}, expectedExit, QFileInfo(file).fileName().toUtf8());
}

// Every format under a directory and file names outside ASCII: umlauts, a
// Hungarian double acute outside the Windows-1252 code page, Japanese. The
// parsers open the UTF-8 path an adapter hands them with a narrow file stream,
// which Windows reads in the process code page: UTF-8 under the manifest.
void TestCheck::unicodePaths() {
    const QDir source(QStringLiteral(EXPLORER_SAMPLES_DIR));
    const QDir target(_dir.filePath(QStringLiteral("K\u00fchlung-Gy\u0151r")));
    QVERIFY(QDir().mkpath(target.path()));
    const QList<QPair<QString, QString>> samples{
        {QStringLiteral("demo_ecu.a2l"), QStringLiteral("Messung_K\u00fchlung.a2l")},
        {QStringLiteral("tesla_can.dbc"), QStringLiteral("Fahrzeug_Gy\u0151r.dbc")},
        {QStringLiteral("demo_seat.ldf"), QStringLiteral("Sitz_\u00dcl\u00e9s.ldf")},
        {QStringLiteral("demo_recording.mf4"), QStringLiteral("Aufzeichnung_\u8a18\u9332.mf4")},
    };
    QStringList files;
    for (const auto& [sample, name] : samples) {
        files.push_back(target.filePath(name));
        QVERIFY(QFile::copy(source.filePath(sample), files.last()));
    }
    check(files, 0, "passed: 4 file(s)");
}

QTEST_GUILESS_MAIN(TestCheck)
#include "tst_check.moc"
