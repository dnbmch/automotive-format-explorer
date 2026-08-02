#include "sessions/mdf4detailpresenter.h"

#include <QStringList>

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
    case mdf4::CONVERSION_OTHER:
        return QStringLiteral("Other / unsupported");
    case mdf4::CONVERSION_KIND_UNSPECIFIED:
        return QStringLiteral("Unspecified");
    }
    return QStringLiteral("Unknown (%1)").arg(static_cast<int>(kind));
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

} // namespace

QList<DetailSection> Mdf4DetailPresenter::buildDetails(const NodeBinding& binding) const {
    if (!std::holds_alternative<Mdf4Path>(binding.payload)) {
        return {};
    }

    const Mdf4Path path = std::get<Mdf4Path>(binding.payload);
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

QString Mdf4DetailPresenter::buildRawJson(const NodeBinding& binding) const {
    if (!std::holds_alternative<Mdf4Path>(binding.payload)) {
        return {};
    }

    const Mdf4Path path = std::get<Mdf4Path>(binding.payload);
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
    pushSection(sections, QStringLiteral("Channel"), std::move(identity));

    pushSection(sections,
                QStringLiteral("Samples"),
                {
                    {QStringLiteral("Sample Count"), integerText(channel.sample_count())},
                    {QStringLiteral("Master Type"), masterTypeText(channel)},
                    {QStringLiteral("Sync Type"), syncTypeText(channel.sync_type())},
                    {QStringLiteral("Channel Type"), integerText(channel.cn_type())},
                });

    pushSection(sections,
                QStringLiteral("Bit Geometry"),
                {
                    {QStringLiteral("Data Type"), dataTypeText(channel.data_type())},
                    {QStringLiteral("Raw Data Type"), integerText(channel.raw_data_type())},
                    {QStringLiteral("Byte Offset"), integerText(channel.byte_offset())},
                    {QStringLiteral("Bit Offset"), integerText(channel.bit_offset())},
                    {QStringLiteral("Bit Count"), integerText(channel.bit_count())},
                });

    const mdf4::Conversion& conversion = channel.conversion();
    QList<DetailField> conversionFields;
    conversionFields.push_back({QStringLiteral("Kind"), conversionKindText(conversion.kind())});
    conversionFields.push_back({QStringLiteral("CC Type"), integerText(conversion.cc_type())});
    addField(conversionFields, QStringLiteral("Coefficients"), parametersText(conversion));
    addField(conversionFields, QStringLiteral("Unit"), text(conversion.unit()));
    addField(conversionFields, QStringLiteral("Default Text"), text(conversion.default_text()));
    for (const mdf4::TextEntry& entry : conversion.entries()) {
        conversionFields.push_back({
            QStringLiteral("Value %1").arg(realText(entry.value())),
            text(entry.text()),
        });
    }
    pushSection(sections, QStringLiteral("Conversion"), std::move(conversionFields));

    QList<DetailField> plot;
    plot.push_back({QStringLiteral("Plottable"), boolText(channel.decodable())});
    if (!channel.decodable()) {
        addField(plot,
                 QStringLiteral("Unsupported Reason"),
                 text(channel.not_decodable_reason()));
    }
    pushSection(sections, QStringLiteral("Plot"), std::move(plot));

    appendComment(sections, channel.comment());
    return sections;
}
