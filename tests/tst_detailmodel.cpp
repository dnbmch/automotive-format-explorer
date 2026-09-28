// The detail panel's model: a selection builds its cards, and its raw JSON is
// produced only when something reads it, once per selection.

#include "models/detailmodel.h"

#include <QSignalSpy>
#include <QTest>

namespace {

// A raw JSON producer that counts its runs.
std::function<QString()> counting(int& runs, const QString& text) {
    return [&runs, text] {
        ++runs;
        return text;
    };
}

QList<DetailSection> cards(const QString& title) {
    return {DetailSection{title, {DetailField{QStringLiteral("Name"), title}}}};
}

} // namespace

class TestDetailModel : public QObject {
    Q_OBJECT

private slots:
    void rawJsonSerializedOnlyWhenRead();
    void emptyTextProducedOncePerSelection();
    void selectionWithoutRawFormIsUnavailable();
    void replacedSelectionShowsOnlyItsOwnText();
    void resetObserversSeeTheNewSelection();
};

// Selections build their cards; the raw form waits until it is read, and is then
// produced once, for the selection shown.
void TestDetailModel::rawJsonSerializedOnlyWhenRead() {
    DetailModel model;
    int first = 0;
    int second = 0;
    int third = 0;
    model.setSelection(cards(QStringLiteral("A")), counting(first, QStringLiteral("{\"a\": 1}")));
    model.setSelection(cards(QStringLiteral("B")), counting(second, QStringLiteral("{\"b\": 2}")));
    model.setSelection(cards(QStringLiteral("C")), counting(third, QStringLiteral("{\"c\": 3}")));

    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0), DetailModel::TitleRole).toString(), QStringLiteral("C"));
    QVERIFY(model.rawJsonAvailable());
    QCOMPARE(first + second + third, 0);

    QCOMPARE(model.rawJsonText(), QStringLiteral("{\"c\": 3}"));
    QCOMPARE(model.rawJsonText(), QStringLiteral("{\"c\": 3}"));
    QCOMPARE(third, 1);
    QCOMPARE(first + second, 0);
}

// An empty result is a result: reading again does not run the producer again.
void TestDetailModel::emptyTextProducedOncePerSelection() {
    DetailModel model;
    int runs = 0;
    model.setSelection(cards(QStringLiteral("A")), counting(runs, QString()));
    QVERIFY(model.rawJsonText().isEmpty());
    QVERIFY(model.rawJsonText().isEmpty());
    QVERIFY(model.rawJsonText().isEmpty());
    QCOMPARE(runs, 1);

    model.setSelection(cards(QStringLiteral("A")), counting(runs, QString()));
    QVERIFY(model.rawJsonText().isEmpty());
    QCOMPARE(runs, 2);
}

void TestDetailModel::selectionWithoutRawFormIsUnavailable() {
    DetailModel model;
    int runs = 0;
    model.setSelection(cards(QStringLiteral("A")), counting(runs, QStringLiteral("{}")));
    model.setSelection(cards(QStringLiteral("Overview")), {});

    QVERIFY(!model.rawJsonAvailable());
    QVERIFY(model.rawJsonText().isEmpty());
    QCOMPARE(runs, 0);
}

// With the raw view shown, its text follows each selection: a replaced
// selection's producer never supplies the text of the one that replaced it.
void TestDetailModel::replacedSelectionShowsOnlyItsOwnText() {
    DetailModel model;
    QStringList shown;
    QObject::connect(&model, &DetailModel::rawJsonChanged, &model,
                     [&model, &shown] { shown << model.rawJsonText(); });
    int first = 0;
    int second = 0;

    model.setSelection(cards(QStringLiteral("A")), counting(first, QStringLiteral("A")));
    model.setSelection(cards(QStringLiteral("B")), counting(second, QStringLiteral("B")));

    QCOMPARE(shown, QStringList({QStringLiteral("A"), QStringLiteral("B")}));
    QCOMPARE(first, 1);
    QCOMPARE(second, 1);
}

// Observers of the reset read the selection being installed, not a mix of the
// new cards and the previous raw form.
void TestDetailModel::resetObserversSeeTheNewSelection() {
    DetailModel model;
    int runs = 0;
    model.setSelection(cards(QStringLiteral("Old")), counting(runs, QStringLiteral("old")));
    QCOMPARE(model.rawJsonText(), QStringLiteral("old"));

    bool available = true;
    QString text;
    QString title;
    QObject::connect(&model, &QAbstractItemModel::modelReset, &model, [&] {
        available = model.rawJsonAvailable();
        text = model.rawJsonText();
        title = model.data(model.index(0), DetailModel::TitleRole).toString();
    });
    model.setSelection(cards(QStringLiteral("New")), {});

    QVERIFY(!available);
    QVERIFY(text.isEmpty());
    QCOMPARE(title, QStringLiteral("New"));
}

QTEST_GUILESS_MAIN(TestDetailModel)
#include "tst_detailmodel.moc"
