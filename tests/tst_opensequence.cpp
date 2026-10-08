#include "core/appcontroller.h"
#include "core/opensequence.h"
#include "sessions/adaptersessionbase.h"

#include <QDir>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTest>

#include <atomic>
#include <memory>

namespace {

class FakeSession final : public AdapterSessionBase {
public:
    explicit FakeSession(const QString& path)
        : AdapterSessionBase(FormatId::Unknown, QStringLiteral("Fake"),
                             QFileInfo(path).fileName(), path) {}

    void selectNode(quint64) override {}
};

// Opens every file it is given, except one whose name starts with "bad".
class FakeAdapter final : public FormatAdapter {
public:
    LoadResult load(const QString& path, const std::atomic<bool>&) const override {
        LoadResult result;
        if (QFileInfo(path).fileName().startsWith(QStringLiteral("bad"))) {
            result.diagnostics.push_back(
                {DiagnosticSeverity::Error, QStringLiteral("Failed"), QStringLiteral("fake failure")});
        } else {
            result.session = std::make_unique<FakeSession>(path);
        }
        return result;
    }
};

FormatList fakeFormats() {
    FormatList formats;
    formats.push_back({FormatId::Unknown, {QStringLiteral("fake")}, std::make_unique<FakeAdapter>()});
    return formats;
}

QUrl fakeFile(const QString& name) {
    return QUrl::fromLocalFile(QDir::temp().filePath(name));
}

}  // namespace

class TestOpenSequence : public QObject {
    Q_OBJECT

private slots:
    void opensInOrderAndReportsEachOutcome();
    void emptySequenceFinishesFromTheEventLoop();
};

// The controller runs one load at a time; the sequence opens each file after the
// previous one's outcome, a refused open and a failed load included.
void TestOpenSequence::opensInOrderAndReportsEachOutcome() {
    AppController controller(fakeFormats());
    const QList<QUrl> files{fakeFile(QStringLiteral("one.fake")), fakeFile(QStringLiteral("notes.txt")),
                            fakeFile(QStringLiteral("bad.fake")), fakeFile(QStringLiteral("two.fake"))};
    OpenSequence sequence(controller, files);
    QSignalSpy outcomes(&sequence, &OpenSequence::outcome);
    QSignalSpy finished(&sequence, &OpenSequence::finished);

    sequence.start();
    QVERIFY(finished.wait(10000));

    QCOMPARE(outcomes.size(), 4);
    for (int i = 0; i < files.size(); ++i) {
        QCOMPARE(outcomes[i][0].toUrl(), files[i]);
    }
    QCOMPARE(outcomes[0][1].toString(), QString());
    QCOMPARE(outcomes[1][1].toString(), QStringLiteral("Unsupported file type: notes.txt"));
    QCOMPARE(outcomes[2][1].toString(), QStringLiteral("fake failure"));
    QCOMPARE(outcomes[3][1].toString(), QString());
    QCOMPARE(sequence.failures(), 2);
    QCOMPARE(finished.size(), 1);

    QCOMPARE(controller.tabModel()->rowCount(), 2);
    QCOMPARE(controller.currentTab()->session()->displayName(), QStringLiteral("two.fake"));
}

void TestOpenSequence::emptySequenceFinishesFromTheEventLoop() {
    AppController controller(fakeFormats());
    OpenSequence sequence(controller, {});
    QSignalSpy finished(&sequence, &OpenSequence::finished);

    sequence.start();
    QCOMPARE(finished.size(), 0);
    QVERIFY(finished.wait(10000));
    QCOMPARE(finished.size(), 1);
    QCOMPARE(sequence.failures(), 0);
}

QTEST_GUILESS_MAIN(TestOpenSequence)
#include "tst_opensequence.moc"
