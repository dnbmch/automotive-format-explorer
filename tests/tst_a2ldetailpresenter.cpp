// Contract for the A2L detail presenter.
//
// Every selectable A2L node must yield raw JSON for the detail pane's raw
// toggle. Most nodes bind to exactly one protobuf message; the XCP and CCP
// summary nodes are aggregates synthesised over the module's repeated
// `if_datas`, so they are covered explicitly here. The cards speak A2L: its
// keywords, addresses in hex, the conversion as phys = f(int), and a name of
// another entity as a link to its tree row.

#include "sessions/a2ldetailpresenter.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QTest>

namespace {

A2lPath pathFor(A2lEntityKind kind, int moduleIndex = 0, int secondary = -1) {
    return A2lPath{kind, moduleIndex, secondary, -1};
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
    void characteristicCardSpeaksA2l();
    void conversionFormulaReadsPhysFromInt();
    void verbalTableRowsFollowTheConversion();
    void enumKeywordDropsThePrefix();
};

namespace {

// The field `key` in the section titled `title`, or a field with an empty key.
DetailField fieldIn(const QList<DetailSection>& sections, const QString& title, const QString& key) {
    for (const DetailSection& section : sections) {
        if (section.title != title) continue;
        for (const DetailField& field : section.fields) {
            if (field.key == key) return field;
        }
    }
    return {};
}

a2l::CompuMethod ratFunc(double a, double b, double c, double d, double e, double f) {
    a2l::CompuMethod method;
    method.set_conversion_type(a2l::CONVERSION_TYPE_RAT_FUNC);
    a2l::Coeffs* k = method.mutable_coeffs();
    k->set_a(a); k->set_b(b); k->set_c(c); k->set_d(d); k->set_e(e); k->set_f(f);
    return method;
}

} // namespace

// A plain single-message node — guards the common path.
void TestA2lDetailPresenter::moduleNodeYieldsRawJson() {
    const a2l::A2lFile doc = makeDocument();
    const A2lLinks links;
    const A2lDetailPresenter presenter(doc, links);

    const QString json = presenter.buildRawJson(pathFor(A2lEntityKind::Module));

    QVERIFY(!json.isEmpty());
    QVERIFY(json.contains(QStringLiteral("ECU_MAIN")));
}

void TestA2lDetailPresenter::xcpSummaryYieldsRawJson() {
    const a2l::A2lFile doc = makeDocument();
    const A2lLinks links;
    const A2lDetailPresenter presenter(doc, links);

    const QString json = presenter.buildRawJson(pathFor(A2lEntityKind::XcpSummary));

    QVERIFY(!json.isEmpty());
    // The aggregate carries the interface name and the typed XCP payload.
    QVERIFY(json.contains(QStringLiteral("XCPplus")));
    QVERIFY(json.contains(QStringLiteral("protocolLayer")));
    QVERIFY(json.contains(QStringLiteral("maxCto")));
}

void TestA2lDetailPresenter::ccpSummaryYieldsRawJson() {
    const a2l::A2lFile doc = makeDocument();
    const A2lLinks links;
    const A2lDetailPresenter presenter(doc, links);

    const QString json = presenter.buildRawJson(pathFor(A2lEntityKind::CcpSummary));

    QVERIFY(!json.isEmpty());
    QVERIFY(json.contains(QStringLiteral("ASAP1B_CCP")));
    QVERIFY(json.contains(QStringLiteral("tpBlob")));
    QVERIFY(json.contains(QStringLiteral("stationAddress")));
}

// The summary nodes present one protocol family each; the raw JSON must be
// filtered the same way the detail sections are.
void TestA2lDetailPresenter::summaryRawJsonExcludesOtherFamilies() {
    const a2l::A2lFile doc = makeDocument();
    const A2lLinks links;
    const A2lDetailPresenter presenter(doc, links);

    const QString xcpJson = presenter.buildRawJson(pathFor(A2lEntityKind::XcpSummary));
    QVERIFY(!xcpJson.contains(QStringLiteral("ASAP1B_CCP")));
    QVERIFY(!xcpJson.contains(QStringLiteral("ETK")));

    const QString ccpJson = presenter.buildRawJson(pathFor(A2lEntityKind::CcpSummary));
    QVERIFY(!ccpJson.contains(QStringLiteral("XCPplus")));
    QVERIFY(!ccpJson.contains(QStringLiteral("ETK")));
}

// A module with no XCP/CCP IF_DATA has no summary node in the tree at all, so
// an empty result is the correct answer rather than a fabricated empty array.
void TestA2lDetailPresenter::summaryWithoutMatchingIfDataIsEmpty() {
    a2l::A2lFile doc;
    doc.add_modules()->set_name("BARE");
    const A2lLinks links;
    const A2lDetailPresenter presenter(doc, links);

    QVERIFY(presenter.buildRawJson(pathFor(A2lEntityKind::XcpSummary)).isEmpty());
    QVERIFY(presenter.buildRawJson(pathFor(A2lEntityKind::CcpSummary)).isEmpty());
}

// One matching if_data renders as a bare JSON object, not a one-element array.
void TestA2lDetailPresenter::singleMatchRawJsonIsOneObject() {
    const a2l::A2lFile doc = makeDocument();
    const A2lLinks links;
    const A2lDetailPresenter presenter(doc, links);

    const QString json = presenter.buildRawJson(pathFor(A2lEntityKind::XcpSummary));
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

    const A2lLinks links;
    const A2lDetailPresenter presenter(doc, links);
    const QString json = presenter.buildRawJson(pathFor(A2lEntityKind::XcpSummary));

    QJsonParseError err{};
    const QJsonDocument parsed = QJsonDocument::fromJson(json.toUtf8(), &err);
    QCOMPARE(err.error, QJsonParseError::NoError);
    QVERIFY(parsed.isArray());
    QCOMPARE(parsed.array().size(), 2);
}

// A MAP with its conversion: keywords, a hex address, the resolved formula
// under Identity, and the conversion as a link to the row the links name.
void TestA2lDetailPresenter::characteristicCardSpeaksA2l() {
    a2l::A2lFile doc;
    a2l::Module* module = doc.add_modules();
    module->set_name("ECU");
    a2l::Characteristic* map = module->add_characteristics();
    map->set_name("KF_IGN");
    map->set_type(a2l::CHARACTERISTIC_TYPE_MAP);
    map->set_address(0x80040000);
    map->set_record_layout_ref("RL_MAP");
    map->set_conversion("CM_RPM");
    a2l::CompuMethod* cm = module->add_compu_methods();
    *cm = ratFunc(0, 4, 8, 0, 0, 5);
    cm->set_name("CM_RPM");
    cm->set_unit("rpm");

    A2lLinks links;
    links.add(0, A2lEntityKind::CompuMethod, "CM_RPM", 42);
    const A2lDetailPresenter presenter(doc, links);
    const QList<DetailSection> sections =
        presenter.buildDetails(pathFor(A2lEntityKind::Characteristic, 0, 0));

    QCOMPARE(fieldIn(sections, QStringLiteral("Identity"), QStringLiteral("Type")).value, QStringLiteral("MAP"));
    QCOMPARE(fieldIn(sections, QStringLiteral("Identity"), QStringLiteral("Address")).value,
             QStringLiteral("0x80040000"));
    const DetailField conversion = fieldIn(sections, QStringLiteral("Identity"), QStringLiteral("Conversion"));
    QCOMPARE(conversion.value, QStringLiteral("CM_RPM"));
    QCOMPARE(conversion.target, quint64(42));
    // The file has no RL_MAP row: the name stays plain text.
    QCOMPARE(fieldIn(sections, QStringLiteral("Identity"), QStringLiteral("Record Layout")).target, quint64(0));

    QVERIFY(sections.size() > 2);
    QCOMPARE(sections[2].title, QStringLiteral("Resolved Conversion"));
    QCOMPARE(fieldIn(sections, QStringLiteral("Resolved Conversion"), QStringLiteral("Formula")).value,
             QStringLiteral("phys = 1.25 * int - 2"));
    QCOMPARE(fieldIn(sections, QStringLiteral("Resolved Conversion"), QStringLiteral("Physical Unit")).value,
             QStringLiteral("rpm"));
}

// RAT_FUNC's COEFFS give int = f(phys) (ASAP2 1.7.1 section 4.7.38); LINEAR
// gives phys = f(int). Both read as phys of int where an inverse exists.
void TestA2lDetailPresenter::conversionFormulaReadsPhysFromInt() {
    using a2ldetail::conversionFormula;
    QCOMPARE(conversionFormula(ratFunc(0, 4, 8, 0, 0, 5)), QStringLiteral("phys = 1.25 * int - 2"));
    QCOMPARE(conversionFormula(ratFunc(0, 1, 0, 0, 0, 1)), QStringLiteral("phys = int"));
    QCOMPARE(conversionFormula(ratFunc(0, 2, 1, 0, 1, 3)),
             QStringLiteral("phys = (3 * int - 1) / (-1 * int + 2)"));
    QCOMPARE(conversionFormula(ratFunc(1, 2, 3, 4, 5, 6)),
             QStringLiteral("int = (1 * phys^2 + 2 * phys + 3) / (4 * phys^2 + 5 * phys + 6)"));

    a2l::CompuMethod linear;
    linear.set_conversion_type(a2l::CONVERSION_TYPE_LINEAR);
    linear.mutable_coeffs_linear()->set_a(0.5);
    linear.mutable_coeffs_linear()->set_b(-10);
    QCOMPARE(conversionFormula(linear), QStringLiteral("phys = 0.5 * int - 10"));

    a2l::CompuMethod identical;
    identical.set_conversion_type(a2l::CONVERSION_TYPE_IDENTICAL);
    QCOMPARE(conversionFormula(identical), QStringLiteral("phys = int"));
}

// A TAB_VERB conversion's COMPU_VTAB rows follow its resolved conversion.
void TestA2lDetailPresenter::verbalTableRowsFollowTheConversion() {
    a2l::A2lFile doc;
    a2l::Module* module = doc.add_modules();
    module->set_name("ECU");
    a2l::Measurement* gear = module->add_measurements();
    gear->set_name("Gear");
    gear->set_datatype(a2l::DATA_TYPE_UBYTE);
    gear->set_conversion("CM_GEAR");
    a2l::CompuMethod* cm = module->add_compu_methods();
    cm->set_name("CM_GEAR");
    cm->set_conversion_type(a2l::CONVERSION_TYPE_TAB_VERB);
    cm->set_compu_tab_ref("VT_GEAR");
    a2l::CompuVtab* vtab = module->add_compu_vtabs();
    vtab->set_name("VT_GEAR");
    a2l::CompuVtabPair* neutral = vtab->add_pairs();
    neutral->set_in_val(0);
    neutral->set_out_val("NEUTRAL");
    a2l::CompuVtabPair* first = vtab->add_pairs();
    first->set_in_val(1);
    first->set_out_val("FIRST");

    const A2lLinks links;
    const A2lDetailPresenter presenter(doc, links);
    const QList<DetailSection> sections =
        presenter.buildDetails(pathFor(A2lEntityKind::Measurement, 0, 0));

    QCOMPARE(fieldIn(sections, QStringLiteral("Verbal Table VT_GEAR"), QStringLiteral("0")).value,
             QStringLiteral("NEUTRAL"));
    QCOMPARE(fieldIn(sections, QStringLiteral("Verbal Table VT_GEAR"), QStringLiteral("1")).value,
             QStringLiteral("FIRST"));
}

void TestA2lDetailPresenter::enumKeywordDropsThePrefix() {
    QCOMPARE(enumKeyword(a2l::BYTE_ORDER_MSB_LAST), QStringLiteral("MSB_LAST"));
    QCOMPARE(enumKeyword(a2l::DATA_TYPE_UWORD), QStringLiteral("UWORD"));
    QCOMPARE(enumKeyword(a2l::MEMORY_PRG_TYPE_CALIBRATION_VARIABLES), QStringLiteral("CALIBRATION_VARIABLES"));
    QCOMPARE(enumKeyword(a2l::BYTE_ORDER_UNSPECIFIED), QString());
}

QTEST_MAIN(TestA2lDetailPresenter)
#include "tst_a2ldetailpresenter.moc"
