// End-to-end smoke: a real writer-produced .mf4 opens, builds a tree, and
// plots. No binary fixture belongs in git, so the recording is named by
// MDF4_WRITER_SAMPLE. Without it the executable exits 77 and ctest reports the
// case as Skipped — a QSKIP would have counted as a pass and hidden the fact
// that the only end-to-end case never ran.
//
//   MDF4_WRITER_SAMPLE=<file>.mf4 ./build/tst_mdf4writerfile

#include "adapters/mdf4adapter.h"
#include "models/signalplotmodel.h"
#include "models/treemodel.h"
#include "sessions/documentsession.h"

#include <QCoreApplication>
#include <QTest>

class TestMdf4WriterFile : public QObject {
    Q_OBJECT

private slots:
    void writerFileOpensAndPlots();
};

void TestMdf4WriterFile::writerFileOpensAndPlots() {
    Mdf4Adapter adapter;
    LoadResult result = adapter.load(qEnvironmentVariable("MDF4_WRITER_SAMPLE"));
    QVERIFY2(result.session != nullptr, qPrintable(
        result.diagnostics.isEmpty() ? QStringLiteral("MDF4 session was not created")
                                     : result.diagnostics.front().detail));

    TreeModel* tree = result.session->treeModel();
    const QModelIndex file = tree->index(0, 0);
    QVERIFY(file.isValid());

    quint64 plottableKey = 0;
    for (int groupRow = 0; groupRow < tree->rowCount(file) && plottableKey == 0; ++groupRow) {
        const QModelIndex group = tree->index(groupRow, 0, file);
        for (int channelRow = 0; channelRow < tree->rowCount(group); ++channelRow) {
            const QModelIndex channel = tree->index(channelRow, 0, group);
            if (tree->data(channel, TreeModel::SemanticKindRole).toInt() ==
                static_cast<int>(SemanticKind::Entity)) {
                plottableKey = tree->data(channel, TreeModel::NodeKeyRole).toULongLong();
                break;
            }
        }
    }
    QVERIFY2(plottableKey != 0, "writer file contains no plottable channel");

    auto* model = static_cast<SignalPlotModel*>(result.session->centerPanelModel());
    result.session->selectNode(plottableKey);
    QTRY_VERIFY_WITH_TIMEOUT(!model->busy(), 5000);
    QVERIFY(model->hasSeries());
    QCOMPARE(model->series().time.size(), model->series().value.size());
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
