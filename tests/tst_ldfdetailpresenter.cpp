// Contract for the LDF detail presenter: a signal's Init Value is written as the
// file declares it, braced for a byte array (one byte included), bare for a scalar.

#include "sessions/ldfdetailpresenter.h"

#include <QTest>

#include <initializer_list>

namespace {

void addSignal(ldf::LdfFile& doc, const char* name, bool byteArray,
               std::initializer_list<int64_t> initValues) {
    ldf::Signal* signal = doc.add_signals();
    signal->set_name(name);
    signal->set_bit_length(8 * static_cast<uint32_t>(initValues.size()));
    signal->set_byte_array(byteArray);
    for (int64_t value : initValues) signal->add_init_values(value);
}

// The Identity card's Init Value of the document's signal at `index`.
QString initValue(const ldf::LdfFile& doc, int index) {
    const LdfDetailPresenter presenter(doc);
    for (const DetailSection& section : presenter.buildDetails(LdfPath{LdfEntityKind::Signal, index, -1})) {
        if (section.title != QStringLiteral("Identity")) continue;
        for (const DetailField& field : section.fields) {
            if (field.key == QStringLiteral("Init Value")) return field.value;
        }
    }
    return {};
}

} // namespace

class TestLdfDetailPresenter : public QObject {
    Q_OBJECT

private slots:
    void initValueFollowsTheDeclaredForm_data();
    void initValueFollowsTheDeclaredForm();
};

void TestLdfDetailPresenter::initValueFollowsTheDeclaredForm_data() {
    QTest::addColumn<int>("signalIndex");
    QTest::addColumn<QString>("text");

    QTest::newRow("8, 5") << 0 << "5";
    QTest::newRow("8, {5}") << 1 << "{5}";
    QTest::newRow("16, {1, 2}") << 2 << "{1, 2}";
}

void TestLdfDetailPresenter::initValueFollowsTheDeclaredForm() {
    QFETCH(int, signalIndex);
    QFETCH(QString, text);

    ldf::LdfFile doc;
    addSignal(doc, "Scalar", false, {5});
    addSignal(doc, "OneByte", true, {5});
    addSignal(doc, "TwoBytes", true, {1, 2});
    QCOMPARE(initValue(doc, signalIndex), text);
}

QTEST_GUILESS_MAIN(TestLdfDetailPresenter)
#include "tst_ldfdetailpresenter.moc"
