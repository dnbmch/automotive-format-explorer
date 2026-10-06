#include "sessions/mdf4detailpresenter.h"

#include <QTest>

namespace {

Mdf4Path pathFor(Mdf4EntityKind kind, int group = -1, int channel = -1) {
    return Mdf4Path{kind, group, channel};
}

QString fieldValue(const QList<DetailSection>& sections,
                   const QString& sectionTitle,
                   const QString& fieldKey) {
    for (const DetailSection& section : sections) {
        if (section.title != sectionTitle) {
            continue;
        }
        for (const DetailField& field : section.fields) {
            if (field.key == fieldKey) {
                return field.value;
            }
        }
    }
    return {};
}

mdf4::File makeDocument() {
    mdf4::File document;
    document.set_version("4.20");
    document.set_version_num(420);
    document.set_finalized(true);

    mdf4::ChannelGroup* group = document.add_groups();
    group->set_name("Powertrain");
    group->set_cycle_count(1'000'000);
    group->set_data_bytes(12);
    group->set_inval_bytes(4);
    group->set_storage(mdf4::COLUMN_LD);
    group->set_compressed(true);

    mdf4::Channel* channel = group->add_channels();
    channel->set_name("EngineSpeed");
    channel->set_unit("rpm");
    channel->set_data_type(mdf4::UINT_LE);
    channel->set_raw_data_type(0);
    channel->set_byte_offset(2);
    channel->set_bit_offset(1);
    channel->set_bit_count(16);
    channel->set_sample_count(1'000'000);
    channel->set_cn_type(0);
    channel->set_decodable(false);
    channel->set_not_decodable_reason("invalidation bits are not supported");

    mdf4::Conversion* conversion = channel->mutable_conversion();
    conversion->set_kind(mdf4::LINEAR);
    conversion->set_cc_type(1);
    conversion->add_params(0.25);
    conversion->add_params(-10.0);
    return document;
}

} // namespace

class TestMdf4DetailPresenter : public QObject {
    Q_OBJECT

private slots:
    void fileCardSummarizesDocument();
    void groupCardDescribesStorage();
    void channelCardExplainsPlotSupport();
    void channelCardNamesBigEndianTypesAndFormulaConversions();
    void selectedEntityYieldsRawJson();
};

void TestMdf4DetailPresenter::fileCardSummarizesDocument() {
    const mdf4::File document = makeDocument();
    const Mdf4DetailPresenter presenter(document);

    const QList<DetailSection> details = presenter.buildDetails(
        pathFor(Mdf4EntityKind::File));

    QCOMPARE(fieldValue(details, QStringLiteral("File"), QStringLiteral("Version")),
             QStringLiteral("4.20"));
    QCOMPARE(fieldValue(details, QStringLiteral("Contents"), QStringLiteral("Channel Groups")),
             QStringLiteral("1"));
}

void TestMdf4DetailPresenter::groupCardDescribesStorage() {
    const mdf4::File document = makeDocument();
    const Mdf4DetailPresenter presenter(document);

    const QList<DetailSection> details = presenter.buildDetails(
        pathFor(Mdf4EntityKind::ChannelGroup, 0));

    QCOMPARE(fieldValue(details, QStringLiteral("Recording"), QStringLiteral("Storage Layout")),
             QStringLiteral("Column fragments (LD)"));
    QCOMPARE(fieldValue(details, QStringLiteral("Recording"), QStringLiteral("Record Size")),
             QStringLiteral("16 bytes"));
    QCOMPARE(fieldValue(details, QStringLiteral("Recording"), QStringLiteral("Cycle Count")),
             QStringLiteral("1000000"));
}

void TestMdf4DetailPresenter::channelCardExplainsPlotSupport() {
    const mdf4::File document = makeDocument();
    const Mdf4DetailPresenter presenter(document);

    const QList<DetailSection> details = presenter.buildDetails(
        pathFor(Mdf4EntityKind::Channel, 0, 0));

    QCOMPARE(fieldValue(details, QStringLiteral("Bit Geometry"), QStringLiteral("Bit Count")),
             QStringLiteral("16"));
    QCOMPARE(fieldValue(details, QStringLiteral("Conversion"), QStringLiteral("Coefficients")),
             QStringLiteral("0.25, -10"));
    QCOMPARE(fieldValue(details, QStringLiteral("Plot"), QStringLiteral("Plottable")),
             QStringLiteral("No"));
    QCOMPARE(fieldValue(details, QStringLiteral("Plot"), QStringLiteral("Unsupported Reason")),
             QStringLiteral("invalidation bits are not supported"));
}

void TestMdf4DetailPresenter::channelCardNamesBigEndianTypesAndFormulaConversions() {
    mdf4::File document = makeDocument();
    mdf4::ChannelGroup* group = document.mutable_groups(0);

    mdf4::Channel* algebraic = group->add_channels();
    algebraic->set_name("OilTemperature");
    algebraic->set_data_type(mdf4::FLOAT_BE);
    mdf4::Conversion* formula = algebraic->mutable_conversion();
    formula->set_kind(mdf4::ALGEBRAIC);
    formula->set_cc_type(3);
    formula->set_formula("X * 0.5 - 40");

    mdf4::Channel* ranged = group->add_channels();
    ranged->set_name("GearState");
    ranged->set_data_type(mdf4::SINT_BE);
    mdf4::Conversion* ranges = ranged->mutable_conversion();
    ranges->set_kind(mdf4::TAB_RANGE);
    ranges->set_cc_type(6);

    const Mdf4DetailPresenter presenter(document);

    const QList<DetailSection> algebraicDetails = presenter.buildDetails(
        pathFor(Mdf4EntityKind::Channel, 0, 1));
    QCOMPARE(fieldValue(algebraicDetails, QStringLiteral("Bit Geometry"), QStringLiteral("Data Type")),
             QStringLiteral("IEEE floating point (big-endian)"));
    QCOMPARE(fieldValue(algebraicDetails, QStringLiteral("Conversion"), QStringLiteral("Kind")),
             QStringLiteral("Algebraic formula"));
    QCOMPARE(fieldValue(algebraicDetails, QStringLiteral("Conversion"), QStringLiteral("Formula")),
             QStringLiteral("X * 0.5 - 40"));

    const QList<DetailSection> rangedDetails = presenter.buildDetails(
        pathFor(Mdf4EntityKind::Channel, 0, 2));
    QCOMPARE(fieldValue(rangedDetails, QStringLiteral("Bit Geometry"), QStringLiteral("Data Type")),
             QStringLiteral("Signed integer (big-endian)"));
    QCOMPARE(fieldValue(rangedDetails, QStringLiteral("Conversion"), QStringLiteral("Kind")),
             QStringLiteral("Value range table"));
    QVERIFY(fieldValue(rangedDetails, QStringLiteral("Conversion"), QStringLiteral("Formula")).isEmpty());
}

void TestMdf4DetailPresenter::selectedEntityYieldsRawJson() {
    const mdf4::File document = makeDocument();
    const Mdf4DetailPresenter presenter(document);

    const QString json = presenter.buildRawJson(
        pathFor(Mdf4EntityKind::Channel, 0, 0));

    QVERIFY(json.contains(QStringLiteral("EngineSpeed")));
    QVERIFY(json.contains(QStringLiteral("notDecodableReason")));
    QVERIFY(presenter.buildRawJson(pathFor(Mdf4EntityKind::Channel, 5, 0)).isEmpty());
}

QTEST_MAIN(TestMdf4DetailPresenter)
#include "tst_mdf4detailpresenter.moc"
