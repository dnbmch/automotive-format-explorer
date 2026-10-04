// The detail panel's model: a selection builds its cards, and its raw JSON is
// produced only when something reads it, once per selection.

#include "models/detailmodel.h"

#include <QSignalSpy>
#include <QTest>

#include <algorithm>
#include <cstring>
#include <iterator>
#include <memory>
#include <new>

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

// A selection's notifications, in the order the model makes them.
enum Notification : int { AboutToReset, Reset, RawForm };

// Runs action once, from inside the model's next such notification.
template <typename Action>
void callOnce(DetailModel* model, int notification, Action action) {
    auto done = std::make_shared<bool>(false);
    const auto once = [done, action] {
        if (!*done) {
            *done = true;
            action();
        }
    };
    switch (notification) {
    case AboutToReset:
        QObject::connect(model, &QAbstractItemModel::modelAboutToBeReset, once);
        break;
    case Reset:
        QObject::connect(model, &QAbstractItemModel::modelReset, once);
        break;
    default:
        QObject::connect(model, &DetailModel::rawJsonChanged, once);
        break;
    }
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
    void observerMaySelectAgain_data() { notifications(); }
    void observerMaySelectAgain();
    void observerMayDestroyModel_data() { notifications(); }
    void observerMayDestroyModel();

private:
    void notifications();
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

void TestDetailModel::notifications() {
    QTest::addColumn<int>("notification");
    QTest::newRow("pre-reset") << int(AboutToReset);
    QTest::newRow("reset") << int(Reset);
    QTest::newRow("raw form") << int(RawForm);
}

// An observer selects again from inside one of a selection's notifications:
// the cards and the raw form end on the newer selection, and every reset
// announced as about to happen is announced as done. From the pre-reset
// notification the one reset under way installs the newer selection.
void TestDetailModel::observerMaySelectAgain() {
    QFETCH(int, notification);
    DetailModel model;
    QSignalSpy aboutToReset(&model, &QAbstractItemModel::modelAboutToBeReset);
    QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
    int older = 0;
    int newer = 0;
    callOnce(&model, notification, [&] {
        model.setSelection(cards(QStringLiteral("New")), counting(newer, QStringLiteral("new")));
    });
    model.setSelection(cards(QStringLiteral("Old")), counting(older, QStringLiteral("old")));

    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0), DetailModel::TitleRole).toString(), QStringLiteral("New"));
    QCOMPARE(model.rawJsonText(), QStringLiteral("new"));
    QCOMPARE(newer, 1);
    QCOMPARE(older, 0);
    QCOMPARE(aboutToReset.size(), notification == AboutToReset ? 1 : 2);
    QCOMPARE(reset.size(), aboutToReset.size());
}

// Observers run synchronously and may destroy the model, as closing a tab
// does. The model lives in storage the test poisons once an observer of one of
// its notifications has destroyed it: the selection touches nothing after that.
void TestDetailModel::observerMayDestroyModel() {
    QFETCH(int, notification);
    alignas(DetailModel) unsigned char storage[sizeof(DetailModel)];
    auto* model = new (storage) DetailModel;
    callOnce(model, notification, [&storage, model] {
        model->~DetailModel();
        std::memset(storage, 0xA5, sizeof storage);
    });
    model->setSelection(cards(QStringLiteral("A")), {});
    QVERIFY(std::all_of(std::begin(storage), std::end(storage),
                        [](unsigned char byte) { return byte == 0xA5; }));
}

QTEST_GUILESS_MAIN(TestDetailModel)
#include "tst_detailmodel.moc"
