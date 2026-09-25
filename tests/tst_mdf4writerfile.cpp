// End-to-end smoke: a real writer-produced .mf4 opens through the production
// adapter, builds a tree, and plots from the session's one reader. ctest points
// MDF4_WRITER_SAMPLE at the bundled samples/demo_recording.mf4; set it to
// another recording to smoke that instead. When the variable resolves to
// nothing the executable exits 77 and ctest reports the case as Skipped — a
// QSKIP would have counted as a pass and hidden the fact that the only
// end-to-end case never ran.
//
//   MDF4_WRITER_SAMPLE=<file>.mf4 ./build/tst_mdf4writerfile

#include "adapters/mdf4adapter.h"
#include "models/signalplotmodel.h"
#include "models/treemodel.h"
#include "sessions/documentsession.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

namespace {

quint64 firstPlottableKey(DocumentSession& session) {
    TreeModel* tree = session.treeModel();
    const QModelIndex file = tree->index(0, 0);
    for (int groupRow = 0; groupRow < tree->rowCount(file); ++groupRow) {
        const QModelIndex group = tree->index(groupRow, 0, file);
        for (int channelRow = 0; channelRow < tree->rowCount(group); ++channelRow) {
            const QModelIndex channel = tree->index(channelRow, 0, group);
            if (tree->data(channel, TreeModel::SemanticKindRole).toInt() ==
                static_cast<int>(SemanticKind::Entity)) {
                return tree->data(channel, TreeModel::NodeKeyRole).toULongLong();
            }
        }
    }
    return 0;
}

} // namespace

class TestMdf4WriterFile : public QObject {
    Q_OBJECT

private slots:
    void writerFileOpensAndPlots();
    void changedSourceAsksForReload();
    void missingFileOpensForInspection();
};

void TestMdf4WriterFile::writerFileOpensAndPlots() {
    Mdf4Adapter adapter;
    LoadResult result = adapter.load(qEnvironmentVariable("MDF4_WRITER_SAMPLE"));
    QVERIFY2(result.session != nullptr, qPrintable(
        result.diagnostics.isEmpty() ? QStringLiteral("MDF4 session was not created")
                                     : result.diagnostics.front().detail));

    const quint64 plottableKey = firstPlottableKey(*result.session);
    QVERIFY2(plottableKey != 0, "writer file contains no plottable channel");

    auto* model = static_cast<SignalPlotModel*>(result.session->centerPanelModel());
    result.session->selectNode(plottableKey);
    QTRY_VERIFY_WITH_TIMEOUT(!model->busy(), 5000);
    QVERIFY(model->hasSeries());
    QCOMPARE(model->series().time.size(), model->series().value.size());
}

// The session's reader keeps the file it opened. Truncating that file behind
// it fails the next read with a reload request instead of plotting a prefix.
void TestMdf4WriterFile::changedSourceAsksForReload() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString copy = dir.filePath(QStringLiteral("recording.mf4"));
    QVERIFY(QFile::copy(qEnvironmentVariable("MDF4_WRITER_SAMPLE"), copy));

    Mdf4Adapter adapter;
    LoadResult result = adapter.load(copy);
    QVERIFY(result.session != nullptr);
    const quint64 plottableKey = firstPlottableKey(*result.session);
    QVERIFY(plottableKey != 0);

    QFile file(copy);
    QVERIFY(file.resize(file.size() - 1));
    auto* model = static_cast<SignalPlotModel*>(result.session->centerPanelModel());
    result.session->selectNode(plottableKey);
    QTRY_VERIFY_WITH_TIMEOUT(!model->busy(), 5000);
    QVERIFY(!model->hasSeries());
    QVERIFY2(model->placeholderText().contains(QStringLiteral("changed since it was opened")),
             qPrintable(model->placeholderText()));
    result.session.reset();
}

// An unopenable path still yields a session showing why nothing can be read.
void TestMdf4WriterFile::missingFileOpensForInspection() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Mdf4Adapter adapter;
    LoadResult result = adapter.load(dir.filePath(QStringLiteral("missing.mf4")));
    QVERIFY(result.session != nullptr);
    QCOMPARE(result.diagnostics.size(), 1);
    QCOMPARE(result.diagnostics.front().severity, DiagnosticSeverity::Error);
    QCOMPARE(result.diagnostics.front().title, QStringLiteral("file cannot be opened"));
    QCOMPARE(result.session->treeModel()->rowCount(result.session->treeModel()->index(0, 0)), 0);
}

int main(int argc, char* argv[]) {
    if (qEnvironmentVariableIsEmpty("MDF4_WRITER_SAMPLE")) {
        qInfo("MDF4_WRITER_SAMPLE names no recording - skipping the writer-file smoke");
        return 77;
    }

    QCoreApplication app(argc, argv);
    TestMdf4WriterFile testCase;
    return QTest::qExec(&testCase, argc, argv);
}

#include "tst_mdf4writerfile.moc"
