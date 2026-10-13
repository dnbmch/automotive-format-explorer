#include "sessions/mdf4detailpresenter.h"

#include <QTest>

#include <initializer_list>

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
    channel->set_not_decodable_reason("structure parent not a fixed-length Byte array");

    mdf4::Conversion* conversion = channel->mutable_conversion();
    conversion->set_kind(mdf4::LINEAR);
    conversion->set_cc_type(1);
    conversion->add_params(0.25);
    conversion->add_params(-10.0);
    return document;
}

// Group 0 of makeDocument() gains, after EngineSpeed: a fixed-length UTF-8
// string, the integer size channel of an MLSD string, that MLSD string, and an
// MLSD string whose size channel the reader refused.
enum TextRow : int { FixedText = 1, SizeChannel, MlsdText, RefusedMlsd };

mdf4::File makeTextDocument() {
    mdf4::File document = makeDocument();
    mdf4::ChannelGroup* group = document.mutable_groups(0);

    mdf4::Channel* fixed = group->add_channels();
    fixed->set_name("Label");
    fixed->set_data_type(mdf4::STRING);
    fixed->set_raw_data_type(7);
    fixed->set_bit_count(64);
    fixed->set_decodable(true);
    fixed->set_textual(true);

    mdf4::Channel* size = group->add_channels();
    size->set_name("MessageLength");
    size->set_data_type(mdf4::UINT_LE);
    size->set_raw_data_type(0);
    size->set_bit_count(8);
    size->set_decodable(true);

    mdf4::Channel* mlsd = group->add_channels();
    mlsd->set_name("Message");
    mlsd->set_cn_type(5);
    mlsd->set_data_type(mdf4::STRING);
    mlsd->set_raw_data_type(6);
    mlsd->set_bit_count(256);
    mlsd->set_decodable(true);
    mlsd->set_textual(true);
    mlsd->set_size_channel(SizeChannel);

    mdf4::Channel* refused = group->add_channels();
    refused->set_name("Note");
    refused->set_cn_type(5);
    refused->set_data_type(mdf4::STRING);
    refused->set_raw_data_type(6);
    refused->set_bit_count(256);
    refused->set_decodable(false);
    refused->set_not_decodable_reason("MLSD size channel is not an integer channel of this group");
    return document;
}

// A decodable array channel of `type` over `sizes`, outer first, with an index
// axis per dimension.
mdf4::ChannelArray* addArray(mdf4::ChannelGroup* group, const char* name, quint32 type,
                             std::initializer_list<quint64> sizes) {
    mdf4::Channel* channel = group->add_channels();
    channel->set_name(name);
    channel->set_sample_count(group->cycle_count());
    channel->set_data_type(mdf4::UINT_LE);
    channel->set_bit_count(8);
    channel->set_decodable(true);
    mdf4::ChannelArray* array = channel->mutable_array();
    array->set_ca_type(type);
    quint64 elements = 1;
    for (quint64 size : sizes) {
        array->add_dimensions()->set_size(size);
        elements *= size;
    }
    array->set_element_count(elements);
    return array;
}

// Group 0 of makeDocument() gains, after EngineSpeed: a look-up whose axes are
// fixed points, an axis channel and the element index; an interval axis with
// left-closed and one with left-open intervals; and an array of nine dimensions
// with a type the standard does not define, its first dimension one fixed point.
// An unnamed second group holds the look-up's axis channel, a scaling axis.
enum ArrayRow : int { LookUp = 1, ClosedIntervals, OpenIntervals, NineDimensions };

mdf4::File makeArrayDocument() {
    mdf4::File document = makeDocument();
    mdf4::ChannelGroup* group = document.mutable_groups(0);

    mdf4::ChannelArray* lookUp = addArray(group, "KF4", 2, {6, 8, 2});
    for (int point = 0; point < 6; ++point) {
        lookUp->mutable_dimensions(0)->add_axis_values(point);
    }
    lookUp->mutable_dimensions(1)->set_axis_from_channel(true);
    lookUp->mutable_dimensions(1)->set_axis_group(1);
    lookUp->mutable_dimensions(1)->set_axis_channel(0);

    addArray(group, "nmot", 3, {143});
    addArray(group, "speed_bins", 3, {20})->set_left_open(true);

    mdf4::ChannelArray* deep = addArray(group, "Cube9", 9, {1, 2, 2, 2, 2, 2, 2, 2, 2});
    deep->mutable_dimensions(0)->add_axis_values(3.5);

    mdf4::ChannelGroup* axes = document.add_groups();
    axes->set_cycle_count(1);
    addArray(axes, "Curve1", 1, {8});
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
    void channelCardListsTextTables();
    void channelCardStatesValidityRule_data();
    void channelCardStatesValidityRule();
    void channelCardNamesStringType();
    void textChannelIsReadableNotPlottable();
    void byteChannelIsReadableNotPlottable();
    void mlsdChannelNamesItsSizeChannel();
    void arrayChannelIsReadableNotPlottable();
    void arrayCardNamesEachAxis();
    void intervalAxisStatesItsIntervals();
    void deepArrayCapsItsDimensions();
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
    QCOMPARE(fieldValue(details, QStringLiteral("Recording"), QStringLiteral("Invalidation Bytes")),
             QStringLiteral("4"));
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
             QStringLiteral("structure parent not a fixed-length Byte array"));
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

void TestMdf4DetailPresenter::channelCardListsTextTables() {
    mdf4::File document = makeDocument();
    mdf4::ChannelGroup* group = document.mutable_groups(0);

    mdf4::Conversion* ranged = group->add_channels()->mutable_conversion();
    ranged->set_kind(mdf4::VALUE_RANGE_TO_TEXT);
    ranged->set_cc_type(8);
    mdf4::RangeTextEntry* low = ranged->add_ranges();
    low->set_key_min(0.0);
    low->set_key_max(2.5);
    low->set_text("low");
    mdf4::RangeTextEntry* scaled = ranged->add_ranges();
    scaled->set_key_min(2.5);
    scaled->set_key_max(10.0);
    scaled->set_scale(true);
    ranged->set_default_text("out of range");

    mdf4::Conversion* keyed = group->add_channels()->mutable_conversion();
    keyed->set_kind(mdf4::TEXT_TO_VALUE);
    keyed->set_cc_type(9);
    mdf4::TextEntry* one = keyed->add_entries();
    one->set_value(1.0);
    one->set_text("one");
    keyed->add_params(1.0);
    keyed->add_params(-50.0);

    mdf4::Conversion* translated = group->add_channels()->mutable_conversion();
    translated->set_kind(mdf4::TEXT_TO_TEXT);
    translated->set_cc_type(10);
    mdf4::TextPair* pair = translated->add_translations();
    pair->set_key("one");
    pair->set_text("Eins");
    translated->set_default_text("No translation");

    mdf4::Conversion* bits = group->add_channels()->mutable_conversion();
    bits->set_kind(mdf4::BITFIELD_TEXT);
    bits->set_cc_type(11);
    bits->add_bitmasks(0xFF);
    bits->add_bitmask_cc_types(7);
    bits->add_bitmasks(0x8000000000000000ull);
    bits->add_bitmask_cc_types(255);

    mdf4::Conversion* status = group->add_channels()->mutable_conversion();
    status->set_kind(mdf4::VALUE_RANGE_TO_TEXT);
    status->set_cc_type(8);
    mdf4::RangeTextEntry* illegal = status->add_ranges();
    illegal->set_key_min(9.5);
    illegal->set_key_max(10.5);
    illegal->set_text("Illegal value");
    status->set_default_scale(true);

    const Mdf4DetailPresenter presenter(document);
    const QString conversion = QStringLiteral("Conversion");

    const QList<DetailSection> ranges = presenter.buildDetails(pathFor(Mdf4EntityKind::Channel, 0, 1));
    QCOMPARE(fieldValue(ranges, conversion, QStringLiteral("Kind")), QStringLiteral("Value range to text"));
    QCOMPARE(fieldValue(ranges, conversion, QStringLiteral("Range 0 to 2.5")), QStringLiteral("low"));
    QCOMPARE(fieldValue(ranges, conversion, QStringLiteral("Range 2.5 to 10")),
             QStringLiteral("Scale conversion"));
    QCOMPARE(fieldValue(ranges, conversion, QStringLiteral("Default Text")), QStringLiteral("out of range"));
    QVERIFY(fieldValue(ranges, conversion, QStringLiteral("Default")).isEmpty());

    const QList<DetailSection> values = presenter.buildDetails(pathFor(Mdf4EntityKind::Channel, 0, 2));
    QCOMPARE(fieldValue(values, conversion, QStringLiteral("Kind")), QStringLiteral("Text to value"));
    QCOMPARE(fieldValue(values, conversion, QStringLiteral("Value 1")), QStringLiteral("one"));
    QCOMPARE(fieldValue(values, conversion, QStringLiteral("Coefficients")), QStringLiteral("1, -50"));

    const QList<DetailSection> texts = presenter.buildDetails(pathFor(Mdf4EntityKind::Channel, 0, 3));
    QCOMPARE(fieldValue(texts, conversion, QStringLiteral("Kind")), QStringLiteral("Text to text"));
    QCOMPARE(fieldValue(texts, conversion, QStringLiteral("Text one")), QStringLiteral("Eins"));
    QCOMPARE(fieldValue(texts, conversion, QStringLiteral("Default Text")), QStringLiteral("No translation"));

    const QList<DetailSection> masks = presenter.buildDetails(pathFor(Mdf4EntityKind::Channel, 0, 4));
    QCOMPARE(fieldValue(masks, conversion, QStringLiteral("Kind")), QStringLiteral("Bitfield text table"));
    QCOMPARE(fieldValue(masks, conversion, QStringLiteral("Bitmask 1")),
             QStringLiteral("0xFF → value-to-text (7)"));
    QCOMPARE(fieldValue(masks, conversion, QStringLiteral("Bitmask 2")),
             QStringLiteral("0x8000000000000000 → no conversion"));

    const QList<DetailSection> rule = presenter.buildDetails(pathFor(Mdf4EntityKind::Channel, 0, 5));
    QCOMPARE(fieldValue(rule, conversion, QStringLiteral("Range 9.5 to 10.5")), QStringLiteral("Illegal value"));
    QCOMPARE(fieldValue(rule, conversion, QStringLiteral("Default")), QStringLiteral("Scale conversion"));
}

void TestMdf4DetailPresenter::channelCardStatesValidityRule_data() {
    QTest::addColumn<bool>("allInvalid");
    QTest::addColumn<bool>("invalidationBit");
    QTest::addColumn<quint32>("bitPos");
    QTest::addColumn<QString>("validity");

    QTest::newRow("no flags") << false << false << 0u << QStringLiteral("No invalidation");
    QTest::newRow("invalidation bit")
        << false << true << 9u
        << QStringLiteral("Invalidation bit 9 (Byte 1, bit 1 of the invalidation Bytes)");
    QTest::newRow("all invalid") << true << false << 0u << QStringLiteral("All values invalid");
    QTest::newRow("all invalid with bit")
        << true << true << 3u << QStringLiteral("All values invalid (invalidation bit 3)");
}

void TestMdf4DetailPresenter::channelCardStatesValidityRule() {
    QFETCH(bool, allInvalid);
    QFETCH(bool, invalidationBit);
    QFETCH(quint32, bitPos);
    QFETCH(QString, validity);

    mdf4::File document = makeDocument();
    mdf4::Channel* channel = document.mutable_groups(0)->mutable_channels(0);
    channel->set_all_invalid(allInvalid);
    channel->set_invalidation_bit(invalidationBit);
    channel->set_inval_bit_pos(bitPos);
    const Mdf4DetailPresenter presenter(document);

    const QList<DetailSection> details = presenter.buildDetails(pathFor(Mdf4EntityKind::Channel, 0, 0));
    QCOMPARE(fieldValue(details, QStringLiteral("Plot"), QStringLiteral("Validity")), validity);
}

void TestMdf4DetailPresenter::channelCardNamesStringType() {
    const mdf4::File document = makeTextDocument();
    const Mdf4DetailPresenter presenter(document);

    const QList<DetailSection> details = presenter.buildDetails(pathFor(Mdf4EntityKind::Channel, 0, FixedText));
    QCOMPARE(fieldValue(details, QStringLiteral("Bit Geometry"), QStringLiteral("Data Type")),
             QStringLiteral("String"));
    QCOMPARE(fieldValue(details, QStringLiteral("Bit Geometry"), QStringLiteral("Raw Data Type")),
             QStringLiteral("7"));
}

// A text channel is read, not plotted: its values are text, and no reason is
// given because nothing is unsupported.
void TestMdf4DetailPresenter::textChannelIsReadableNotPlottable() {
    const mdf4::File document = makeTextDocument();
    const Mdf4DetailPresenter presenter(document);
    const QString plot = QStringLiteral("Plot");

    const QList<DetailSection> fixed = presenter.buildDetails(pathFor(Mdf4EntityKind::Channel, 0, FixedText));
    QCOMPARE(fieldValue(fixed, plot, QStringLiteral("Values")), QStringLiteral("Text (UTF-8)"));
    QCOMPARE(fieldValue(fixed, plot, QStringLiteral("Plottable")), QStringLiteral("No"));
    QVERIFY(fieldValue(fixed, plot, QStringLiteral("Unsupported Reason")).isEmpty());
    QCOMPARE(fieldValue(fixed, plot, QStringLiteral("Validity")), QStringLiteral("No invalidation"));

    const QList<DetailSection> number = presenter.buildDetails(pathFor(Mdf4EntityKind::Channel, 0, SizeChannel));
    QCOMPARE(fieldValue(number, plot, QStringLiteral("Plottable")), QStringLiteral("Yes"));
    QVERIFY(fieldValue(number, plot, QStringLiteral("Values")).isEmpty());
}

// A Byte channel is readable, its values raw Bytes, and not plottable; its
// data type is named.
void TestMdf4DetailPresenter::byteChannelIsReadableNotPlottable() {
    mdf4::File document = makeDocument();
    mdf4::Channel* frame = document.mutable_groups(0)->add_channels();
    frame->set_name("CAN_DataFrame");
    frame->set_data_type(mdf4::BYTE_ARRAY);
    frame->set_raw_data_type(10);
    frame->set_bit_count(120);
    frame->set_decodable(true);
    frame->set_binary(true);
    const Mdf4DetailPresenter presenter(document);
    const QString plot = QStringLiteral("Plot");

    const QList<DetailSection> details = presenter.buildDetails(pathFor(Mdf4EntityKind::Channel, 0, 1));
    QCOMPARE(fieldValue(details, QStringLiteral("Bit Geometry"), QStringLiteral("Data Type")),
             QStringLiteral("Byte array"));
    QCOMPARE(fieldValue(details, plot, QStringLiteral("Values")), QStringLiteral("Raw Bytes"));
    QCOMPARE(fieldValue(details, plot, QStringLiteral("Plottable")), QStringLiteral("No"));
    QVERIFY(fieldValue(details, plot, QStringLiteral("Unsupported Reason")).isEmpty());
}

// A decodable MLSD channel names the channel of its group whose value is the
// count of valid Bytes; a refused one, or a fixed-length string, names none.
void TestMdf4DetailPresenter::mlsdChannelNamesItsSizeChannel() {
    mdf4::File document = makeTextDocument();
    const QString plot = QStringLiteral("Plot");
    {
        const Mdf4DetailPresenter presenter(document);
        const QList<DetailSection> mlsd = presenter.buildDetails(pathFor(Mdf4EntityKind::Channel, 0, MlsdText));
        QCOMPARE(fieldValue(mlsd, plot, QStringLiteral("Size Channel")), QStringLiteral("MessageLength"));
        QCOMPARE(fieldValue(mlsd, plot, QStringLiteral("Values")), QStringLiteral("Text (UTF-8)"));

        const QList<DetailSection> refused = presenter.buildDetails(pathFor(Mdf4EntityKind::Channel, 0, RefusedMlsd));
        QCOMPARE(fieldValue(refused, plot, QStringLiteral("Plottable")), QStringLiteral("No"));
        QVERIFY(fieldValue(refused, plot, QStringLiteral("Size Channel")).isEmpty());

        const QList<DetailSection> fixed = presenter.buildDetails(pathFor(Mdf4EntityKind::Channel, 0, FixedText));
        QVERIFY(fieldValue(fixed, plot, QStringLiteral("Size Channel")).isEmpty());
    }

    // An unnamed size channel is named as the tree titles it.
    document.mutable_groups(0)->mutable_channels(SizeChannel)->clear_name();
    const Mdf4DetailPresenter presenter(document);
    const QList<DetailSection> mlsd = presenter.buildDetails(pathFor(Mdf4EntityKind::Channel, 0, MlsdText));
    QCOMPARE(fieldValue(mlsd, plot, QStringLiteral("Size Channel")), QStringLiteral("Channel 3"));
}

// An array channel is read, not plotted: each sample is a whole array, and no
// reason is given because nothing is unsupported. Its sample count stays the
// group's cycle count.
void TestMdf4DetailPresenter::arrayChannelIsReadableNotPlottable() {
    mdf4::File document = makeArrayDocument();
    const QString plot = QStringLiteral("Plot");
    {
        const Mdf4DetailPresenter presenter(document);
        const QList<DetailSection> lookUp = presenter.buildDetails(pathFor(Mdf4EntityKind::Channel, 0, LookUp));
        QCOMPARE(fieldValue(lookUp, plot, QStringLiteral("Plottable")), QStringLiteral("No"));
        QCOMPARE(fieldValue(lookUp, plot, QStringLiteral("Values")),
                 QStringLiteral("Array of 96 elements per sample"));
        QVERIFY(fieldValue(lookUp, plot, QStringLiteral("Unsupported Reason")).isEmpty());
        QCOMPARE(fieldValue(lookUp, QStringLiteral("Samples"), QStringLiteral("Sample Count")),
                 QStringLiteral("1000000"));
    }

    // One element per sample is still an array.
    mdf4::ChannelArray* single = document.mutable_groups(0)->mutable_channels(ClosedIntervals)->mutable_array();
    single->mutable_dimensions(0)->set_size(1);
    single->set_element_count(1);
    const Mdf4DetailPresenter presenter(document);
    const QList<DetailSection> one = presenter.buildDetails(pathFor(Mdf4EntityKind::Channel, 0, ClosedIntervals));
    QCOMPARE(fieldValue(one, plot, QStringLiteral("Plottable")), QStringLiteral("No"));
    QCOMPARE(fieldValue(one, plot, QStringLiteral("Values")), QStringLiteral("Array of 1 element per sample"));
}

// The Array card states the type and shape, and per dimension its axis: fixed
// points, an axis channel named as the tree titles it and its group, or the
// element index. A scalar channel has no Array card.
void TestMdf4DetailPresenter::arrayCardNamesEachAxis() {
    mdf4::File document = makeArrayDocument();
    const QString array = QStringLiteral("Array");
    {
        const Mdf4DetailPresenter presenter(document);
        const QList<DetailSection> lookUp = presenter.buildDetails(pathFor(Mdf4EntityKind::Channel, 0, LookUp));
        QCOMPARE(fieldValue(lookUp, array, QStringLiteral("Array Type")), QStringLiteral("Look-up"));
        QCOMPARE(fieldValue(lookUp, array, QStringLiteral("Dimensions")), QStringLiteral("6 × 8 × 2"));
        QCOMPARE(fieldValue(lookUp, array, QStringLiteral("Elements per Sample")), QStringLiteral("96"));
        QCOMPARE(fieldValue(lookUp, array, QStringLiteral("Axis 1")), QStringLiteral("Fixed: 0 … 5 (6 points)"));
        QCOMPARE(fieldValue(lookUp, array, QStringLiteral("Axis 2")),
                 QStringLiteral("Channel Curve1 (Channel Group 2)"));
        QCOMPARE(fieldValue(lookUp, array, QStringLiteral("Axis 3")), QStringLiteral("Index"));
        QVERIFY(fieldValue(lookUp, array, QStringLiteral("Axis 4")).isEmpty());
        QVERIFY(fieldValue(lookUp, array, QStringLiteral("Intervals")).isEmpty());

        const QList<DetailSection> scalar = presenter.buildDetails(pathFor(Mdf4EntityKind::Channel, 0, 0));
        QVERIFY(fieldValue(scalar, array, QStringLiteral("Array Type")).isEmpty());
    }

    document.mutable_groups(1)->set_name("Axes");
    const Mdf4DetailPresenter presenter(document);
    const QList<DetailSection> lookUp = presenter.buildDetails(pathFor(Mdf4EntityKind::Channel, 0, LookUp));
    QCOMPARE(fieldValue(lookUp, array, QStringLiteral("Axis 2")), QStringLiteral("Channel Curve1 (Axes)"));
}

// An interval axis states which border each interval includes; the flag means
// nothing on any other type.
void TestMdf4DetailPresenter::intervalAxisStatesItsIntervals() {
    mdf4::File document = makeArrayDocument();
    const QString array = QStringLiteral("Array");
    {
        const Mdf4DetailPresenter presenter(document);
        const QList<DetailSection> closed =
            presenter.buildDetails(pathFor(Mdf4EntityKind::Channel, 0, ClosedIntervals));
        QCOMPARE(fieldValue(closed, array, QStringLiteral("Array Type")), QStringLiteral("Interval axis"));
        QCOMPARE(fieldValue(closed, array, QStringLiteral("Dimensions")), QStringLiteral("143"));
        QCOMPARE(fieldValue(closed, array, QStringLiteral("Intervals")), QStringLiteral("[a, b["));
        QCOMPARE(fieldValue(closed, array, QStringLiteral("Axis 1")), QStringLiteral("Index"));

        const QList<DetailSection> open = presenter.buildDetails(pathFor(Mdf4EntityKind::Channel, 0, OpenIntervals));
        QCOMPARE(fieldValue(open, array, QStringLiteral("Intervals")), QStringLiteral("]a, b]"));
    }

    document.mutable_groups(0)->mutable_channels(LookUp)->mutable_array()->set_left_open(true);
    const Mdf4DetailPresenter presenter(document);
    const QList<DetailSection> lookUp = presenter.buildDetails(pathFor(Mdf4EntityKind::Channel, 0, LookUp));
    QCOMPARE(fieldValue(lookUp, array, QStringLiteral("Array Type")), QStringLiteral("Look-up"));
    QVERIFY(fieldValue(lookUp, array, QStringLiteral("Intervals")).isEmpty());
}

// Past eight dimensions the shape names the first eight sizes and the count,
// and the card lists the axes of those eight. A type the standard does not
// define is named by its number.
void TestMdf4DetailPresenter::deepArrayCapsItsDimensions() {
    const mdf4::File document = makeArrayDocument();
    const Mdf4DetailPresenter presenter(document);
    const QString array = QStringLiteral("Array");

    const QList<DetailSection> deep = presenter.buildDetails(pathFor(Mdf4EntityKind::Channel, 0, NineDimensions));
    QCOMPARE(fieldValue(deep, array, QStringLiteral("Array Type")), QStringLiteral("Array type 9"));
    QCOMPARE(fieldValue(deep, array, QStringLiteral("Dimensions")),
             QStringLiteral("1 × 2 × 2 × 2 × 2 × 2 × 2 × 2 … (9 dimensions)"));
    QCOMPARE(fieldValue(deep, array, QStringLiteral("Elements per Sample")), QStringLiteral("256"));
    QCOMPARE(fieldValue(deep, array, QStringLiteral("Axis 1")), QStringLiteral("Fixed: 3.5 (1 point)"));
    QCOMPARE(fieldValue(deep, array, QStringLiteral("Axis 8")), QStringLiteral("Index"));
    QVERIFY(fieldValue(deep, array, QStringLiteral("Axis 9")).isEmpty());
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
