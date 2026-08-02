#include "sessions/mdf4detailpresenter.h"

#include <QTest>

namespace {

NodeBinding bindingFor(Mdf4EntityKind kind, int group = -1, int channel = -1) {
    return NodeBinding{SemanticKind::Entity, Mdf4Path{kind, group, channel}, true};
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
    void selectedEntityYieldsRawJson();
};

void TestMdf4DetailPresenter::fileCardSummarizesDocument() {
    const mdf4::File document = makeDocument();
    const Mdf4DetailPresenter presenter(document);

    const QList<DetailSection> details = presenter.buildDetails(
        bindingFor(Mdf4EntityKind::File));

    QCOMPARE(fieldValue(details, QStringLiteral("File"), QStringLiteral("Version")),
             QStringLiteral("4.20"));
    QCOMPARE(fieldValue(details, QStringLiteral("Contents"), QStringLiteral("Channel Groups")),
             QStringLiteral("1"));
}

void TestMdf4DetailPresenter::groupCardDescribesStorage() {
    const mdf4::File document = makeDocument();
    const Mdf4DetailPresenter presenter(document);

    const QList<DetailSection> details = presenter.buildDetails(
        bindingFor(Mdf4EntityKind::ChannelGroup, 0));

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
        bindingFor(Mdf4EntityKind::Channel, 0, 0));

    QCOMPARE(fieldValue(details, QStringLiteral("Bit Geometry"), QStringLiteral("Bit Count")),
             QStringLiteral("16"));
    QCOMPARE(fieldValue(details, QStringLiteral("Conversion"), QStringLiteral("Coefficients")),
             QStringLiteral("0.25, -10"));
    QCOMPARE(fieldValue(details, QStringLiteral("Plot"), QStringLiteral("Plottable")),
             QStringLiteral("No"));
    QCOMPARE(fieldValue(details, QStringLiteral("Plot"), QStringLiteral("Unsupported Reason")),
             QStringLiteral("invalidation bits are not supported"));
}

void TestMdf4DetailPresenter::selectedEntityYieldsRawJson() {
    const mdf4::File document = makeDocument();
    const Mdf4DetailPresenter presenter(document);

    const QString json = presenter.buildRawJson(
        bindingFor(Mdf4EntityKind::Channel, 0, 0));

    QVERIFY(json.contains(QStringLiteral("EngineSpeed")));
    QVERIFY(json.contains(QStringLiteral("notDecodableReason")));
    QVERIFY(presenter.buildRawJson(bindingFor(Mdf4EntityKind::Channel, 5, 0)).isEmpty());
}

QTEST_MAIN(TestMdf4DetailPresenter)
#include "tst_mdf4detailpresenter.moc"
