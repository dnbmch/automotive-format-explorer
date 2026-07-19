// Raw-JSON contract for the A2L detail presenter.
//
// Every selectable A2L node must yield raw JSON for the detail pane's raw
// toggle. Most nodes bind to exactly one protobuf message; the XCP and CCP
// summary nodes are aggregates synthesised over the module's repeated
// `if_datas`, so they are covered explicitly here.

#include "sessions/a2ldetailpresenter.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QTest>

namespace {

NodeBinding bindingFor(A2lEntityKind kind, int moduleIndex = 0, int secondary = -1) {
    return NodeBinding{SemanticKind::Entity, A2lPath{kind, moduleIndex, secondary, -1}, true};
}

// A one-module document carrying one XCP IF_DATA, one CCP IF_DATA and one
// unsupported raw IF_DATA — the mixed-family shape the summary nodes filter.
a2l::A2lFile makeDocument() {
    a2l::A2lFile doc;
    a2l::Module* module = doc.add_modules();
    module->set_name("ECU_MAIN");

    a2l::IfData* xcpIf = module->add_if_datas();
    xcpIf->set_interface_name("XCPplus");
    a2l::XcpProtocolLayer* pl = xcpIf->mutable_xcp()->mutable_protocol_layer();
    pl->set_protocol_version(0x0100);
    pl->set_max_cto(8);
    pl->set_max_dto(64);

    a2l::IfData* ccpIf = module->add_if_datas();
    ccpIf->set_interface_name("ASAP1B_CCP");
    a2l::CcpTpBlob* tp = ccpIf->mutable_ccp()->mutable_tp_blob();
    tp->set_ccp_version(0x0201);
    tp->set_station_address(0x39);

    a2l::IfData* rawIf = module->add_if_datas();
    rawIf->set_interface_name("ETK");
    rawIf->set_raw_payload("ETK_BLOB 1 2 3");

    return doc;
}

} // namespace

class TestA2lDetailPresenter : public QObject {
    Q_OBJECT

private slots:
    void moduleNodeYieldsRawJson();
    void xcpSummaryYieldsRawJson();
    void ccpSummaryYieldsRawJson();
    void summaryRawJsonExcludesOtherFamilies();
    void summaryWithoutMatchingIfDataIsEmpty();
    void singleMatchRawJsonIsOneObject();
    void multipleMatchesRawJsonIsValidArray();
};

// A plain single-message node — guards the common path.
void TestA2lDetailPresenter::moduleNodeYieldsRawJson() {
    const a2l::A2lFile doc = makeDocument();
    const A2lDetailPresenter presenter(doc);

    const QString json = presenter.buildRawJson(bindingFor(A2lEntityKind::Module));

    QVERIFY(!json.isEmpty());
    QVERIFY(json.contains(QStringLiteral("ECU_MAIN")));
}

void TestA2lDetailPresenter::xcpSummaryYieldsRawJson() {
    const a2l::A2lFile doc = makeDocument();
    const A2lDetailPresenter presenter(doc);

    const QString json = presenter.buildRawJson(bindingFor(A2lEntityKind::XcpSummary));

    QVERIFY(!json.isEmpty());
    // The aggregate carries the interface name and the typed XCP payload.
    QVERIFY(json.contains(QStringLiteral("XCPplus")));
    QVERIFY(json.contains(QStringLiteral("protocolLayer")));
    QVERIFY(json.contains(QStringLiteral("maxCto")));
}

void TestA2lDetailPresenter::ccpSummaryYieldsRawJson() {
    const a2l::A2lFile doc = makeDocument();
    const A2lDetailPresenter presenter(doc);

    const QString json = presenter.buildRawJson(bindingFor(A2lEntityKind::CcpSummary));

    QVERIFY(!json.isEmpty());
    QVERIFY(json.contains(QStringLiteral("ASAP1B_CCP")));
    QVERIFY(json.contains(QStringLiteral("tpBlob")));
    QVERIFY(json.contains(QStringLiteral("stationAddress")));
}

// The summary nodes present one protocol family each; the raw JSON must be
// filtered the same way the detail sections are.
void TestA2lDetailPresenter::summaryRawJsonExcludesOtherFamilies() {
    const a2l::A2lFile doc = makeDocument();
    const A2lDetailPresenter presenter(doc);

    const QString xcpJson = presenter.buildRawJson(bindingFor(A2lEntityKind::XcpSummary));
    QVERIFY(!xcpJson.contains(QStringLiteral("ASAP1B_CCP")));
    QVERIFY(!xcpJson.contains(QStringLiteral("ETK")));

    const QString ccpJson = presenter.buildRawJson(bindingFor(A2lEntityKind::CcpSummary));
    QVERIFY(!ccpJson.contains(QStringLiteral("XCPplus")));
    QVERIFY(!ccpJson.contains(QStringLiteral("ETK")));
}

// A module with no XCP/CCP IF_DATA has no summary node in the tree at all, so
// an empty result is the correct answer rather than a fabricated empty array.
void TestA2lDetailPresenter::summaryWithoutMatchingIfDataIsEmpty() {
    a2l::A2lFile doc;
    doc.add_modules()->set_name("BARE");
    const A2lDetailPresenter presenter(doc);

    QVERIFY(presenter.buildRawJson(bindingFor(A2lEntityKind::XcpSummary)).isEmpty());
    QVERIFY(presenter.buildRawJson(bindingFor(A2lEntityKind::CcpSummary)).isEmpty());
}

// One matching if_data renders as a bare JSON object, not a one-element array.
void TestA2lDetailPresenter::singleMatchRawJsonIsOneObject() {
    const a2l::A2lFile doc = makeDocument();
    const A2lDetailPresenter presenter(doc);

    const QString json = presenter.buildRawJson(bindingFor(A2lEntityKind::XcpSummary));
    QJsonParseError err{};
    const QJsonDocument parsed = QJsonDocument::fromJson(json.toUtf8(), &err);
    QCOMPARE(err.error, QJsonParseError::NoError);
    QVERIFY(parsed.isObject());
}

// Several matching if_datas must render as one parseable JSON array, not
// back-to-back objects (which no JSON parser accepts).
void TestA2lDetailPresenter::multipleMatchesRawJsonIsValidArray() {
    a2l::A2lFile doc;
    a2l::Module* module = doc.add_modules();
    module->set_name("ECU_MAIN");

    module->add_if_datas()->mutable_xcp()->mutable_protocol_layer()->set_max_cto(8);
    module->add_if_datas()->mutable_xcp()->mutable_protocol_layer()->set_max_cto(16);

    const A2lDetailPresenter presenter(doc);
    const QString json = presenter.buildRawJson(bindingFor(A2lEntityKind::XcpSummary));

    QJsonParseError err{};
    const QJsonDocument parsed = QJsonDocument::fromJson(json.toUtf8(), &err);
    QCOMPARE(err.error, QJsonParseError::NoError);
    QVERIFY(parsed.isArray());
    QCOMPARE(parsed.array().size(), 2);
}

QTEST_MAIN(TestA2lDetailPresenter)
#include "tst_a2ldetailpresenter.moc"
