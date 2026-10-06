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

#include <atomic>

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
    void cancelledOpeningYieldsDroppedDiagnostic();
};

void TestMdf4WriterFile::writerFileOpensAndPlots() {
    const std::atomic<bool> cancel{false};
    Mdf4Adapter adapter;
    LoadResult result = adapter.load(qEnvironmentVariable("MDF4_WRITER_SAMPLE"), cancel);
    QVERIFY2(result.session != nullptr, qPrintable(
        result.diagnostics.isEmpty() ? QStringLiteral("MDF4 session was not created")
                                     : result.diagnostics.front().detail));

    const quint64 plottableKey = firstPlottableKey(*result.session);
    QVERIFY2(plottableKey != 0, "writer file contains no plottable channel");

    // The overview of the whole channel, then the exact samples of the view.
    auto* model = static_cast<SignalPlotModel*>(result.session->centerPanelModel());
    result.session->selectNode(plottableKey);
    QTRY_VERIFY_WITH_TIMEOUT(model->plotState() == SignalPlotModel::Detail && !model->busy(),
                             5000);
    QVERIFY(model->hasSamples());
    QVERIFY(!model->incomplete());
    QCOMPARE(static_cast<std::uint64_t>(model->window()->time.size()),
             model->overview()->sampleCount);
    QCOMPARE(model->window()->time.size(), model->window()->value.size());
    QVERIFY(model->overview()->domain == PlotDomain::Time);
}

// The session's reader keeps the file it opened. Truncating that file behind
// it fails the next scan with a request to reopen it instead of plotting a
// prefix.
void TestMdf4WriterFile::changedSourceAsksForReload() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString copy = dir.filePath(QStringLiteral("recording.mf4"));
    QVERIFY(QFile::copy(qEnvironmentVariable("MDF4_WRITER_SAMPLE"), copy));

    const std::atomic<bool> cancel{false};
    Mdf4Adapter adapter;
    LoadResult result = adapter.load(copy, cancel);
    QVERIFY(result.session != nullptr);
    const quint64 plottableKey = firstPlottableKey(*result.session);
    QVERIFY(plottableKey != 0);

    QFile file(copy);
    QVERIFY(file.resize(file.size() - 1));
    auto* model = static_cast<SignalPlotModel*>(result.session->centerPanelModel());
    result.session->selectNode(plottableKey);
    QTRY_VERIFY_WITH_TIMEOUT(model->plotState() == SignalPlotModel::Failed && !model->busy(),
                             5000);
    QVERIFY(!model->hasSamples());
    QVERIFY2(model->message().contains(QStringLiteral("changed since it was opened; reopen it")),
             qPrintable(model->message()));
    result.session.reset();
}

// An unopenable path still yields a session showing why nothing can be read.
void TestMdf4WriterFile::missingFileOpensForInspection() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const std::atomic<bool> cancel{false};
    Mdf4Adapter adapter;
    LoadResult result = adapter.load(dir.filePath(QStringLiteral("missing.mf4")), cancel);
    QVERIFY(result.session != nullptr);
    QCOMPARE(result.diagnostics.size(), 1);
    QCOMPARE(result.diagnostics.front().severity, DiagnosticSeverity::Error);
    QCOMPARE(result.diagnostics.front().title, QStringLiteral("file cannot be opened"));
    QCOMPARE(result.session->treeModel()->rowCount(result.session->treeModel()->index(0, 0)), 0);
}

// A cancellation already requested reaches the reader: the opening stops before
// any structure is read, and the session shows the reader's one DROPPED
// diagnostic naming it.
void TestMdf4WriterFile::cancelledOpeningYieldsDroppedDiagnostic() {
    const std::atomic<bool> cancel{true};
    Mdf4Adapter adapter;
    LoadResult result = adapter.load(qEnvironmentVariable("MDF4_WRITER_SAMPLE"), cancel);
    QVERIFY(result.session != nullptr);
    QCOMPARE(result.diagnostics.size(), 1);
    QCOMPARE(result.diagnostics.front().severity, DiagnosticSeverity::Error);
    QVERIFY2(result.diagnostics.front().title.contains(QStringLiteral("opening cancelled")),
             qPrintable(result.diagnostics.front().title));
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
