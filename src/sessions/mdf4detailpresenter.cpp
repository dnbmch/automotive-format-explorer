#include "sessions/mdf4detailpresenter.h"

#include <QStringList>

#include <algorithm>

namespace {

QString integerText(quint64 value) {
    return QString::number(value);
}

QString realText(double value) {
    return QString::number(value, 'g', 17);
}

QString dataTypeText(mdf4::DataType type) {
    switch (type) {
    case mdf4::UINT_LE:
        return QStringLiteral("Unsigned integer (little-endian)");
    case mdf4::SINT_LE:
        return QStringLiteral("Signed integer (little-endian)");
    case mdf4::FLOAT_LE:
        return QStringLiteral("IEEE floating point (little-endian)");
    case mdf4::UINT_BE:
        return QStringLiteral("Unsigned integer (big-endian)");
    case mdf4::SINT_BE:
        return QStringLiteral("Signed integer (big-endian)");
    case mdf4::FLOAT_BE:
        return QStringLiteral("IEEE floating point (big-endian)");
    case mdf4::STRING:
        return QStringLiteral("String");
    case mdf4::BYTE_ARRAY:
        return QStringLiteral("Byte array");
    case mdf4::MIME_SAMPLE:
        return QStringLiteral("MIME sample");
    case mdf4::MIME_STREAM:
        return QStringLiteral("MIME stream");
    case mdf4::DATA_TYPE_OTHER:
        return QStringLiteral("Other / unsupported");
    case mdf4::DATA_TYPE_UNSPECIFIED:
        return QStringLiteral("Unspecified");
    }
    return QStringLiteral("Unknown (%1)").arg(static_cast<int>(type));
}

QString conversionKindText(mdf4::ConversionKind kind) {
    switch (kind) {
    case mdf4::IDENTITY:
        return QStringLiteral("Identity");
    case mdf4::LINEAR:
        return QStringLiteral("Linear");
    case mdf4::RATIONAL:
        return QStringLiteral("Rational");
    case mdf4::TAB_INTP:
        return QStringLiteral("Value table (interpolated)");
    case mdf4::TAB_NOINTP:
        return QStringLiteral("Value table (nearest)");
    case mdf4::VALUE_TO_TEXT:
        return QStringLiteral("Value to text");
    case mdf4::ALGEBRAIC:
        return QStringLiteral("Algebraic formula");
    case mdf4::TAB_RANGE:
        return QStringLiteral("Value range table");
    case mdf4::VALUE_RANGE_TO_TEXT:
        return QStringLiteral("Value range to text");
    case mdf4::TEXT_TO_VALUE:
        return QStringLiteral("Text to value");
    case mdf4::TEXT_TO_TEXT:
        return QStringLiteral("Text to text");
    case mdf4::BITFIELD_TEXT:
        return QStringLiteral("Bitfield text table");
    case mdf4::CONVERSION_OTHER:
        return QStringLiteral("Other / unsupported");
    case mdf4::CONVERSION_KIND_UNSPECIFIED:
        return QStringLiteral("Unspecified");
    }
    return QStringLiteral("Unknown (%1)").arg(static_cast<int>(kind));
}

// The family a bitfield text table entry's link names, by its raw cc_type;
// 255 stands for a NIL link or one naming no readable conversion.
QString linkedConversionText(quint32 ccType) {
    switch (ccType) {
    case 7:
        return QStringLiteral("value-to-text (7)");
    case 8:
        return QStringLiteral("value-range-to-text (8)");
    case 255:
        return QStringLiteral("no conversion");
    default:
        return QStringLiteral("conversion type %1").arg(ccType);
    }
}

QString storageText(mdf4::StorageLayout storage) {
    switch (storage) {
    case mdf4::STORAGE_NONE:
        return QStringLiteral("No data block");
    case mdf4::ROW_DT:
        return QStringLiteral("Row records (DT/DZ)");
    case mdf4::COLUMN_DV:
        return QStringLiteral("Column values (DV/DZ)");
    case mdf4::COLUMN_LD:
        return QStringLiteral("Column fragments (LD)");
    case mdf4::STORAGE_OTHER:
        return QStringLiteral("Other / unsupported");
    case mdf4::STORAGE_UNSPECIFIED:
        return QStringLiteral("Unspecified");
    }
    return QStringLiteral("Unknown (%1)").arg(static_cast<int>(storage));
}

QString syncTypeText(quint32 type) {
    switch (type) {
    case 0:
        return QStringLiteral("None");
    case 1:
        return QStringLiteral("Time");
    case 2:
        return QStringLiteral("Angle");
    case 3:
        return QStringLiteral("Distance");
    case 4:
        return QStringLiteral("Index");
    default:
        return QStringLiteral("Type %1").arg(type);
    }
}

// A source's bus type, si_bus_type as written (ASAM MDF 4.2.0 BS p.81).
QString busTypeText(quint32 type) {
    switch (type) {
    case 0:
        return QStringLiteral("None");
    case 1:
        return QStringLiteral("Other");
    case 2:
        return QStringLiteral("CAN");
    case 3:
        return QStringLiteral("LIN");
    case 4:
        return QStringLiteral("MOST");
    case 5:
        return QStringLiteral("FlexRay");
    case 6:
        return QStringLiteral("K-Line");
    case 7:
        return QStringLiteral("Ethernet");
    case 8:
        return QStringLiteral("USB");
    default:
        return type >= 128 ? QStringLiteral("Vendor-defined (%1)").arg(type)
                           : QStringLiteral("Reserved (%1)").arg(type);
    }
}

QString masterTypeText(const mdf4::Channel& channel) {
    if (!channel.is_master()) {
        return QStringLiteral("Not a master");
    }

    const QString sync = syncTypeText(channel.sync_type()).toLower();
    switch (channel.cn_type()) {
    case 2:
        return QStringLiteral("Stored %1 master").arg(sync);
    case 3:
        return QStringLiteral("Virtual %1 master").arg(sync);
    default:
        return QStringLiteral("%1 master (channel type %2)")
            .arg(sync, QString::number(channel.cn_type()));
    }
}

// The channel's invalidation flags as the reader evaluated them; the bit sits in
// Byte pos >> 3 of a record's invalidation Bytes, at bit pos & 7.
QString validityText(const mdf4::Channel& channel) {
    const quint32 position = channel.inval_bit_pos();
    if (channel.all_invalid()) {
        return channel.invalidation_bit()
                   ? QStringLiteral("All values invalid (invalidation bit %1)").arg(position)
                   : QStringLiteral("All values invalid");
    }
    if (channel.invalidation_bit()) {
        return QStringLiteral("Invalidation bit %1 (Byte %2, bit %3 of the invalidation Bytes)")
            .arg(position)
            .arg(position >> 3)
            .arg(position & 7);
    }
    return QStringLiteral("No invalidation");
}

QString parametersText(const mdf4::Conversion& conversion) {
    QStringList parameters;
    for (double value : conversion.params()) {
        parameters.push_back(realText(value));
    }
    return parameters.join(QStringLiteral(", "));
}

void appendComment(QList<DetailSection>& sections, const std::string& comment) {
    if (!comment.empty()) {
        pushSection(sections,
                    QStringLiteral("Comment"),
                    {DetailField{QStringLiteral("Text"), text(comment)}});
    }
}

// One dimension's axis: an axis channel, named as the tree titles it and its
// group, fixed physical points, or the zero-based element index.
QString axisText(const mdf4::File& document, const mdf4::ArrayDimension& dimension) {
    if (dimension.axis_from_channel()) {
        const int groupIndex = static_cast<int>(dimension.axis_group());
        const int channelIndex = static_cast<int>(dimension.axis_channel());
        const mdf4::ChannelGroup& group = document.groups(groupIndex);
        const mdf4::Channel& axis = group.channels(channelIndex);
        return QStringLiteral("Channel %1 (%2)")
            .arg(axis.name().empty() ? QStringLiteral("Channel %1").arg(channelIndex + 1)
                                     : text(axis.name()),
                 group.name().empty() ? QStringLiteral("Channel Group %1").arg(groupIndex + 1)
                                      : text(group.name()));
    }
    const int points = dimension.axis_values_size();
    if (points == 1) {
        return QStringLiteral("Fixed: %1 (1 point)").arg(realText(dimension.axis_values(0)));
    }
    if (points > 1) {
        return QStringLiteral("Fixed: %1 … %2 (%3 points)")
            .arg(realText(dimension.axis_values(0)), realText(dimension.axis_values(points - 1)))
            .arg(points);
    }
    return QStringLiteral("Index");
}

} // namespace

QString arrayTypeText(quint32 caType) {
    switch (caType) {
    case 0:
        return QStringLiteral("Array");
    case 1:
        return QStringLiteral("Scaling axis");
    case 2:
        return QStringLiteral("Look-up");
    case 3:
        return QStringLiteral("Interval axis");
    case 4:
        return QStringLiteral("Classification result");
    default:
        return QStringLiteral("Array type %1").arg(caType);
    }
}

QString arrayShapeText(const mdf4::ChannelArray& array) {
    const int dimensions = array.dimensions_size();
    QStringList sizes;
    for (int index = 0; index < std::min(dimensions, kArrayDimensionsShown); ++index) {
        sizes.push_back(integerText(array.dimensions(index).size()));
    }
    QString shape = sizes.join(QStringLiteral(" × "));
    if (dimensions > kArrayDimensionsShown) {
        shape += QStringLiteral(" … (%1 dimensions)").arg(dimensions);
    }
    return shape;
}

QList<DetailSection> Mdf4DetailPresenter::buildDetails(const Mdf4Path& path) const {
    switch (path.kind) {
    case Mdf4EntityKind::File:
        return fileDetails();
    case Mdf4EntityKind::ChannelGroup:
        return groupDetails(path);
    case Mdf4EntityKind::Channel:
        return channelDetails(path);
    }
    return {};
}

QString Mdf4DetailPresenter::buildRawJson(const Mdf4Path& path) const {
    const google::protobuf::Message* message = nullptr;
    switch (path.kind) {
    case Mdf4EntityKind::File:
        message = &_document;
        break;
    case Mdf4EntityKind::ChannelGroup:
        if (path.groupIndex >= 0 && path.groupIndex < _document.groups_size()) {
            message = &_document.groups(path.groupIndex);
        }
        break;
    case Mdf4EntityKind::Channel:
        if (path.groupIndex >= 0 && path.groupIndex < _document.groups_size()) {
            const mdf4::ChannelGroup& group = _document.groups(path.groupIndex);
            if (path.channelIndex >= 0 && path.channelIndex < group.channels_size()) {
                message = &group.channels(path.channelIndex);
            }
        }
        break;
    }
    return message ? messageToJsonText(*message) : QString{};
}

QList<DetailSection> Mdf4DetailPresenter::fileDetails() const {
    QList<DetailSection> sections;

    QList<DetailField> header;
    addField(header, QStringLiteral("Version"), text(_document.version()));
    header.push_back({QStringLiteral("Version Number"), integerText(_document.version_num())});
    header.push_back({QStringLiteral("Finalized"), boolText(_document.finalized())});
    header.push_back({QStringLiteral("Start Time (ns)"), integerText(_document.start_time_ns())});
    pushSection(sections, QStringLiteral("File"), std::move(header));

    pushSection(sections,
                QStringLiteral("Contents"),
                {
                    {QStringLiteral("Channel Groups"), integerText(_document.groups_size())},
                    {QStringLiteral("History Entries"), integerText(_document.history_size())},
                    {QStringLiteral("Diagnostics"), integerText(_document.diagnostics_size())},
                });

    appendComment(sections, _document.comment());

    for (int index = 0; index < _document.history_size(); ++index) {
        const mdf4::FileHistory& history = _document.history(index);
        QList<DetailField> fields;
        fields.push_back({QStringLiteral("Time (ns)"), integerText(history.time_ns())});
        addField(fields, QStringLiteral("Comment"), text(history.comment()));
        pushSection(sections,
                    QStringLiteral("History %1").arg(index + 1),
                    std::move(fields));
    }

    return sections;
}

QList<DetailSection> Mdf4DetailPresenter::groupDetails(const Mdf4Path& path) const {
    QList<DetailSection> sections;
    if (path.groupIndex < 0 || path.groupIndex >= _document.groups_size()) {
        return sections;
    }

    const mdf4::ChannelGroup& group = _document.groups(path.groupIndex);
    QList<DetailField> identity;
    addField(identity, QStringLiteral("Name"), text(group.name()));
    addField(identity, QStringLiteral("Source"), text(group.source_name()));
    identity.push_back({QStringLiteral("Record ID"), integerText(group.record_id())});
    identity.push_back({QStringLiteral("Channels"), integerText(group.channels_size())});
    pushSection(sections, QStringLiteral("Channel Group"), std::move(identity));

    const quint64 recordSize = static_cast<quint64>(group.data_bytes()) + group.inval_bytes();
    pushSection(sections,
                QStringLiteral("Recording"),
                {
                    {QStringLiteral("Storage Layout"), storageText(group.storage())},
                    {QStringLiteral("Compressed"), boolText(group.compressed())},
                    {QStringLiteral("Cycle Count"), integerText(group.cycle_count())},
                    {QStringLiteral("Record Size"), QStringLiteral("%1 bytes").arg(recordSize)},
                    {QStringLiteral("Data Bytes"), integerText(group.data_bytes())},
                    {QStringLiteral("Invalidation Bytes"), integerText(group.inval_bytes())},
                });

    // A bus log's group: its bus-event flags as written, the separator of its
    // composed names and its acquisition source's bus.
    if (group.bus_event() || group.source_bus_type() != 0 || !group.path_separator().empty()) {
        pushSection(sections,
                    QStringLiteral("Bus"),
                    {
                        {QStringLiteral("Bus Events"), boolText(group.bus_event())},
                        {QStringLiteral("Plain Bus Events"), boolText(group.plain_bus_event())},
                        {QStringLiteral("Path Separator"), group.path_separator().empty()
                                                               ? QStringLiteral("none")
                                                               : text(group.path_separator())},
                        {QStringLiteral("Source Bus"), busTypeText(group.source_bus_type())},
                    });
    }

    QList<DetailField> master;
    master.push_back({QStringLiteral("Remote Master"), boolText(group.remote_master())});
    if (group.remote_master()) {
        master.push_back({QStringLiteral("Master Group"), integerText(group.master_group())});
        master.push_back({QStringLiteral("Master Resolved"), boolText(group.master_resolved())});
    }
    pushSection(sections, QStringLiteral("Master"), std::move(master));

    appendComment(sections, group.comment());
    return sections;
}

QList<DetailSection> Mdf4DetailPresenter::channelDetails(const Mdf4Path& path) const {
    QList<DetailSection> sections;
    if (path.groupIndex < 0 || path.groupIndex >= _document.groups_size()) {
        return sections;
    }

    const mdf4::ChannelGroup& group = _document.groups(path.groupIndex);
    if (path.channelIndex < 0 || path.channelIndex >= group.channels_size()) {
        return sections;
    }

    const mdf4::Channel& channel = group.channels(path.channelIndex);
    QList<DetailField> identity;
    addField(identity, QStringLiteral("Name"), text(channel.name()));
    addField(identity, QStringLiteral("Unit"), text(channel.unit()));
    addField(identity, QStringLiteral("Source"), text(channel.source_name()));
    addField(identity, QStringLiteral("Source Path"), text(channel.source_path()));
    // A compact structure's member names its parent; its own Name stays whole.
    if (channel.has_member_of()) {
        const int parentIndex = static_cast<int>(channel.member_of());
        const mdf4::Channel& parent = group.channels(parentIndex);
        identity.push_back({QStringLiteral("Member Of"),
                            parent.name().empty() ? QStringLiteral("Channel %1").arg(parentIndex + 1)
                                                  : text(parent.name())});
    }
    pushSection(sections, QStringLiteral("Channel"), std::move(identity));

    pushSection(sections,
                QStringLiteral("Samples"),
                {
                    {QStringLiteral("Sample Count"), integerText(channel.sample_count())},
                    {QStringLiteral("Master Type"), masterTypeText(channel)},
                    {QStringLiteral("Sync Type"), syncTypeText(channel.sync_type())},
                    {QStringLiteral("Channel Type"), integerText(channel.cn_type())},
                });

    // A bus log's channel: its bus-event flag and its source's bus.
    if (channel.bus_event() || channel.source_bus_type() != 0) {
        pushSection(sections,
                    QStringLiteral("Bus"),
                    {
                        {QStringLiteral("Bus Event"), boolText(channel.bus_event())},
                        {QStringLiteral("Source Bus"), busTypeText(channel.source_bus_type())},
                    });
    }

    pushSection(sections,
                QStringLiteral("Bit Geometry"),
                {
                    {QStringLiteral("Data Type"), dataTypeText(channel.data_type())},
                    {QStringLiteral("Raw Data Type"), integerText(channel.raw_data_type())},
                    {QStringLiteral("Byte Offset"), integerText(channel.byte_offset())},
                    {QStringLiteral("Bit Offset"), integerText(channel.bit_offset())},
                    {QStringLiteral("Bit Count"), integerText(channel.bit_count())},
                });

    // The geometry above is element 0's, the template of every element; an
    // interval axis's left_open flag says which border an interval includes.
    if (channel.has_array()) {
        const mdf4::ChannelArray& array = channel.array();
        QList<DetailField> arrayFields{
            {QStringLiteral("Array Type"), arrayTypeText(array.ca_type())},
            {QStringLiteral("Dimensions"), arrayShapeText(array)},
            {QStringLiteral("Elements per Sample"), integerText(array.element_count())},
        };
        if (array.ca_type() == 3) {
            arrayFields.push_back({QStringLiteral("Intervals"),
                                   array.left_open() ? QStringLiteral("]a, b]") : QStringLiteral("[a, b[")});
        }
        for (int index = 0; index < std::min(array.dimensions_size(), kArrayDimensionsShown); ++index) {
            arrayFields.push_back({QStringLiteral("Axis %1").arg(index + 1),
                                   axisText(_document, array.dimensions(index))});
        }
        pushSection(sections, QStringLiteral("Array"), std::move(arrayFields));
    }

    const mdf4::Conversion& conversion = channel.conversion();
    QList<DetailField> conversionFields;
    conversionFields.push_back({QStringLiteral("Kind"), conversionKindText(conversion.kind())});
    conversionFields.push_back({QStringLiteral("CC Type"), integerText(conversion.cc_type())});
    addField(conversionFields, QStringLiteral("Coefficients"), parametersText(conversion));
    addField(conversionFields, QStringLiteral("Formula"), text(conversion.formula()));
    addField(conversionFields, QStringLiteral("Unit"), text(conversion.unit()));
    addField(conversionFields, QStringLiteral("Default Text"), text(conversion.default_text()));
    if (conversion.default_scale()) {
        conversionFields.push_back({QStringLiteral("Default"), QStringLiteral("Scale conversion")});
    }
    for (const mdf4::TextEntry& entry : conversion.entries()) {
        conversionFields.push_back({
            QStringLiteral("Value %1").arg(realText(entry.value())),
            text(entry.text()),
        });
    }
    for (const mdf4::RangeTextEntry& range : conversion.ranges()) {
        conversionFields.push_back({
            QStringLiteral("Range %1 to %2").arg(realText(range.key_min()), realText(range.key_max())),
            range.scale() ? QStringLiteral("Scale conversion") : text(range.text()),
        });
    }
    for (const mdf4::TextPair& pair : conversion.translations()) {
        conversionFields.push_back({QStringLiteral("Text %1").arg(text(pair.key())), text(pair.text())});
    }
    for (int index = 0; index < conversion.bitmasks_size(); ++index) {
        conversionFields.push_back({
            QStringLiteral("Bitmask %1").arg(index + 1),
            QStringLiteral("0x%1 → %2")
                .arg(QString::number(conversion.bitmasks(index), 16).toUpper(),
                     linkedConversionText(conversion.bitmask_cc_types(index))),
        });
    }
    pushSection(sections, QStringLiteral("Conversion"), std::move(conversionFields));

    // A text channel is read, not plotted, and so is an array channel, whose
    // sample is a whole array; an MLSD channel's valid Byte count is the value of
    // the size channel of its group.
    QList<DetailField> plot;
    plot.push_back({QStringLiteral("Plottable"),
                    boolText(channel.decodable() && !channel.textual() && !channel.binary() &&
                             !channel.has_array())});
    if (!channel.decodable()) {
        addField(plot,
                 QStringLiteral("Unsupported Reason"),
                 text(channel.not_decodable_reason()));
    }
    if (channel.textual()) {
        plot.push_back({QStringLiteral("Values"), QStringLiteral("Text (UTF-8)")});
    }
    if (channel.binary()) {
        plot.push_back({QStringLiteral("Values"), QStringLiteral("Raw Bytes")});
    }
    if (channel.has_array()) {
        const quint64 elements = channel.array().element_count();
        plot.push_back({QStringLiteral("Values"),
                        elements == 1 ? QStringLiteral("Array of 1 element per sample")
                                      : QStringLiteral("Array of %1 elements per sample").arg(elements)});
    }
    if (channel.decodable() && channel.cn_type() == 5) {
        const int sizeIndex = static_cast<int>(channel.size_channel());
        const mdf4::Channel& size = group.channels(sizeIndex);
        plot.push_back({QStringLiteral("Size Channel"),
                        size.name().empty() ? QStringLiteral("Channel %1").arg(sizeIndex + 1)
                                            : text(size.name())});
    }
    plot.push_back({QStringLiteral("Validity"), validityText(channel)});
    pushSection(sections, QStringLiteral("Plot"), std::move(plot));

    appendComment(sections, channel.comment());
    return sections;
}
