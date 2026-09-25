#include "builtinformats.h"
#include "core/appcontroller.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

// The application's production format list: suffix lookup, derived dialog
// filters and sample classification, and one bundled sample of every format
// opened through a controller composed from it.
class TestBuiltInFormats : public QObject {
    Q_OBJECT

private slots:
    void resolvesSupportedSuffixes_data();
    void resolvesSupportedSuffixes();
    void rejectsUnsupportedPaths_data();
    void rejectsUnsupportedPaths();
    void derivesDialogFilters();
    void classifiesSupportedFiles();
    void opensBundledSamples_data();
    void opensBundledSamples();
    void opensUppercaseSuffix();
    void reportsUnsupportedFile();

private:
    void openAndCheck(const QString& path, const QString& format);
};

void TestBuiltInFormats::resolvesSupportedSuffixes_data() {
    QTest::addColumn<QString>("path");
    QTest::addColumn<int>("format");

    QTest::newRow("a2l") << "ecu.a2l" << int(FormatId::A2L);
    QTest::newRow("A2L") << "ECU.A2L" << int(FormatId::A2L);
    QTest::newRow("dbc") << "bus.dbc" << int(FormatId::DBC);
    QTest::newRow("Dbc") << "C:/data/Bus.Dbc" << int(FormatId::DBC);
    QTest::newRow("ldf") << "lin.ldf" << int(FormatId::LDF);
    QTest::newRow("LDF") << "LIN.LDF" << int(FormatId::LDF);
    QTest::newRow("mf4") << "recording.mf4" << int(FormatId::MDF4);
    QTest::newRow("MF4 in dotted dir") << "run.v2/RECORDING.MF4" << int(FormatId::MDF4);
}

void TestBuiltInFormats::resolvesSupportedSuffixes() {
    QFETCH(QString, path);
    QFETCH(int, format);

    const FormatList formats = builtInFormats();
    const FormatEntry* entry = formatForPath(formats, path);
    QVERIFY(entry);
    QCOMPARE(int(entry->id), format);
    QVERIFY(entry->adapter);
}

void TestBuiltInFormats::rejectsUnsupportedPaths_data() {
    QTest::addColumn<QString>("path");

    QTest::newRow("text") << "notes.txt";
    QTest::newRow("trailing suffix") << "bus.dbc.bak";
    QTest::newRow("no suffix") << "recording";
    QTest::newRow("bare format name") << "a2l";
    QTest::newRow("other MDF suffix") << "recording.mdf";
    QTest::newRow("empty") << "";
}

void TestBuiltInFormats::rejectsUnsupportedPaths() {
    QFETCH(QString, path);
    QVERIFY(!formatForPath(builtInFormats(), path));
}

void TestBuiltInFormats::derivesDialogFilters() {
    const QStringList expected = {
        QStringLiteral("Automotive files (*.a2l *.dbc *.ldf *.mf4)"),
        QStringLiteral("A2L files (*.a2l)"),
        QStringLiteral("DBC files (*.dbc)"),
        QStringLiteral("LDF files (*.ldf)"),
        QStringLiteral("MDF4 files (*.mf4)"),
        QStringLiteral("All files (*)"),
    };
    QCOMPARE(fileDialogFilters(builtInFormats()), expected);

    // The list QML binds to is the same derivation.
    AppController controller(builtInFormats());
    QCOMPARE(controller.fileDialogFilters(), expected);
}

void TestBuiltInFormats::classifiesSupportedFiles() {
    const FormatList formats = builtInFormats();

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    for (const char* name : {"z.mf4", "b.DBC", "readme.md", "a.a2l", "c.ldf", "noext", "x.dbc.bak"}) {
        QFile file(dir.filePath(QString::fromLatin1(name)));
        QVERIFY(file.open(QIODevice::WriteOnly));
    }
    QVERIFY(QDir(dir.path()).mkdir(QStringLiteral("folder.dbc")));

    QStringList names;
    for (const QFileInfo& entry : supportedFiles(formats, QDir(dir.path()))) {
        names.push_back(entry.fileName());
    }
    QCOMPARE(names, QStringList({QStringLiteral("a.a2l"), QStringLiteral("b.DBC"),
                                 QStringLiteral("c.ldf"), QStringLiteral("z.mf4")}));

    // The bundled samples: one per format; the provenance notes are not offered.
    names.clear();
    for (const QFileInfo& entry : supportedFiles(formats, QDir(QStringLiteral(EXPLORER_SAMPLES_DIR)))) {
        names.push_back(entry.fileName());
    }
    QCOMPARE(names, QStringList({QStringLiteral("demo_ecu.a2l"), QStringLiteral("demo_recording.mf4"),
                                 QStringLiteral("demo_seat.ldf"), QStringLiteral("tesla_can.dbc")}));
}

void TestBuiltInFormats::opensBundledSamples_data() {
    QTest::addColumn<QString>("file");
    QTest::addColumn<QString>("format");

    QTest::newRow("A2L") << "demo_ecu.a2l" << "A2L";
    QTest::newRow("DBC") << "tesla_can.dbc" << "DBC";
    QTest::newRow("LDF") << "demo_seat.ldf" << "LDF";
    QTest::newRow("MDF4") << "demo_recording.mf4" << "MDF4";
}

void TestBuiltInFormats::opensBundledSamples() {
    QFETCH(QString, file);
    QFETCH(QString, format);
    openAndCheck(QDir(QStringLiteral(EXPLORER_SAMPLES_DIR)).filePath(file), format);
}

void TestBuiltInFormats::opensUppercaseSuffix() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("DEMO_SEAT.LDF"));
    QVERIFY(QFile::copy(QDir(QStringLiteral(EXPLORER_SAMPLES_DIR)).filePath(QStringLiteral("demo_seat.ldf")),
                        path));
    openAndCheck(path, QStringLiteral("LDF"));
}

void TestBuiltInFormats::reportsUnsupportedFile() {
    AppController controller(builtInFormats());
    controller.openFile(QUrl::fromLocalFile(QDir(QStringLiteral(EXPLORER_SAMPLES_DIR))
                                                .filePath(QStringLiteral("SAMPLES.md"))));

    QCOMPARE(controller.lastError(), QStringLiteral("Unsupported file type: SAMPLES.md"));
    QVERIFY(!controller.fileLoading());
    QCOMPARE(controller.tabModel()->rowCount(), 0);
}

// Opens path through a controller composed from the production list and checks
// the resulting tab's identity, which the session reports.
void TestBuiltInFormats::openAndCheck(const QString& path, const QString& format) {
    AppController controller(builtInFormats());
    QSignalSpy loaded(&controller, &AppController::fileLoaded);

    controller.openFile(QUrl::fromLocalFile(path));
    QVERIFY2(loaded.wait(30000), qPrintable(controller.lastError()));

    const QString name = QFileInfo(path).fileName();
    QCOMPARE(loaded.first().first().toString(), name);
    TabModel* tabs = controller.tabModel();
    QCOMPARE(tabs->rowCount(), 1);
    const QModelIndex tab = tabs->index(0);
    QCOMPARE(tabs->data(tab, TabModel::TitleRole).toString(), name);
    QCOMPARE(tabs->data(tab, TabModel::FormatRole).toString(), format);
    QCOMPARE(tabs->data(tab, TabModel::SourcePathRole).toString(), path);
    QCOMPARE(controller.currentTabIndex(), 0);
    QVERIFY(controller.currentTreeModel()->rowCount() > 0);
    QVERIFY(controller.lastError().isEmpty());
}

QTEST_GUILESS_MAIN(TestBuiltInFormats)
#include "tst_builtinformats.moc"
