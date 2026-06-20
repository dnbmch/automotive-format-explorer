#include "sessions/dbcdetailpresenter.h"

#include <QStringList>
#include <type_traits>

#include <google/protobuf/util/json_util.h>

#undef signals  // dbc proto's repeated `signals` field vs Qt's `signals` keyword macro

namespace {

QString text(const std::string& value) {
    auto utf8 = QString::fromUtf8(value.data(), static_cast<int>(value.size()));
    if (utf8.contains(QChar::ReplacementCharacter)) {
        return QString::fromLatin1(value.data(), static_cast<int>(value.size()));
    }
    return utf8;
}

QString boolText(bool value) {
    return value ? QStringLiteral("Yes") : QStringLiteral("No");
}

QString joinStrings(const google::protobuf::RepeatedPtrField<std::string>& values) {
    QStringList items;
    for (const std::string& value : values) {
        items.push_back(text(value));
    }
    return items.join(QStringLiteral(", "));
}

QString hexId(quint32 id) {
    return QStringLiteral("0x%1").arg(id, 0, 16).toUpper();
}

void addField(QList<DetailField>& fields, const QString& key, const QString& value) {
    if (!value.isEmpty()) {
        fields.push_back(DetailField{key, value});
    }
}

template<typename T>
void addNumberField(QList<DetailField>& fields, const QString& key, T value) {
    if constexpr (std::is_floating_point_v<T>)
        fields.push_back(DetailField{key, QString::number(static_cast<double>(value))});
    else
        fields.push_back(DetailField{key, QString::number(static_cast<qlonglong>(value))});
}

void pushSection(QList<DetailSection>& sections, const QString& title, QList<DetailField> fields) {
    if (!fields.isEmpty()) {
        sections.push_back(DetailSection{title, std::move(fields)});
    }
}

} // namespace

QList<DetailSection> DbcDetailPresenter::buildDetails(const NodeBinding& binding) const {
    if (!std::holds_alternative<DbcPath>(binding.payload)) {
        return {};
    }

    const DbcPath path = std::get<DbcPath>(binding.payload);
    switch (path.kind) {
    case DbcEntityKind::Node:
        return nodeDetails(path);
    case DbcEntityKind::Message:
        return messageDetails(path);
    case DbcEntityKind::Signal:
        return signalDetails(path);
    case DbcEntityKind::ValueTable:
        return valueTableDetails(path);
    case DbcEntityKind::AttributeDefinition:
        return attributeDefinitionDetails(path);
    case DbcEntityKind::AttributeDefault:
        return attributeDefaultDetails(path);
    case DbcEntityKind::AttributeValue:
        return attributeValueDetails(path);
    case DbcEntityKind::EnvironmentVariable:
        return environmentVariableDetails(path);
    case DbcEntityKind::SignalGroup:
        return signalGroupDetails(path);
    }

    return {};
}

QString DbcDetailPresenter::buildRawJson(const NodeBinding& binding) const {
    if (!std::holds_alternative<DbcPath>(binding.payload)) {
        return {};
    }

    const DbcPath path = std::get<DbcPath>(binding.payload);
    const google::protobuf::Message* msg = nullptr;
    int i = path.primaryIndex;
    int j = path.secondaryIndex;

    switch (path.kind) {
    case DbcEntityKind::Node:                if (i >= 0 && i < _document.nodes_size()) msg = &_document.nodes(i); break;
    case DbcEntityKind::Message:             if (i >= 0 && i < _document.messages_size()) msg = &_document.messages(i); break;
    case DbcEntityKind::Signal:              if (i >= 0 && i < _document.messages_size() && j >= 0 && j < _document.messages(i).signals_size()) msg = &_document.messages(i).signals(j); break;
    case DbcEntityKind::ValueTable:          if (i >= 0 && i < _document.value_tables_size()) msg = &_document.value_tables(i); break;
    case DbcEntityKind::AttributeDefinition: if (i >= 0 && i < _document.attribute_definitions_size()) msg = &_document.attribute_definitions(i); break;
    case DbcEntityKind::AttributeDefault:    if (i >= 0 && i < _document.attribute_defaults_size()) msg = &_document.attribute_defaults(i); break;
    case DbcEntityKind::AttributeValue:      if (i >= 0 && i < _document.attribute_values_size()) msg = &_document.attribute_values(i); break;
    case DbcEntityKind::EnvironmentVariable: if (i >= 0 && i < _document.environment_variables_size()) msg = &_document.environment_variables(i); break;
    case DbcEntityKind::SignalGroup:         if (i >= 0 && i < _document.signal_groups_size()) msg = &_document.signal_groups(i); break;
    }

    if (!msg) {
        return {};
    }

    google::protobuf::util::JsonPrintOptions opts;
    opts.add_whitespace = true;
    std::string json;
    auto status = google::protobuf::util::MessageToJsonString(*msg, &json, opts);
    if (!status.ok()) {
        return {};
    }
    return text(json);
}

QList<DetailSection> DbcDetailPresenter::nodeDetails(const DbcPath& path) const {
    QList<DetailSection> sections;
    if (path.primaryIndex < 0 || path.primaryIndex >= _document.nodes_size()) {
        return sections;
    }

    const auto& node = _document.nodes(path.primaryIndex);
    QList<DetailField> identity;
    addField(identity, QStringLiteral("Name"), text(node.name()));
    addNumberField(identity, QStringLiteral("Attributes"), node.attributes_size());
    pushSection(sections, QStringLiteral("Identity"), std::move(identity));

    if (!node.comment().empty()) {
        pushSection(sections,
                    QStringLiteral("Comment"),
                    {DetailField{QStringLiteral("Text"), text(node.comment())}});
    }

    QList<DetailField> attributes;
    for (const auto& attribute : node.attributes()) {
        addField(attributes,
                 text(attribute.name()),
                 text(attribute.value()));
    }
    pushSection(sections, QStringLiteral("Attributes"), std::move(attributes));

    appendNodeCrossReferences(sections, node.name());
    return sections;
}

QList<DetailSection> DbcDetailPresenter::messageDetails(const DbcPath& path) const {
    QList<DetailSection> sections;
    if (path.primaryIndex < 0 || path.primaryIndex >= _document.messages_size()) {
        return sections;
    }

    const auto& message = _document.messages(path.primaryIndex);
    QList<DetailField> identity;
    addField(identity, QStringLiteral("Name"), text(message.name()));
    addField(identity, QStringLiteral("CAN ID"), hexId(message.id()));
    addField(identity, QStringLiteral("Extended ID"), boolText(message.is_extended_id()));
    addNumberField(identity, QStringLiteral("DLC"), message.dlc());
    addField(identity, QStringLiteral("Sender"), text(message.sender()));
    addNumberField(identity, QStringLiteral("Signals"), message.signals_size());
    pushSection(sections, QStringLiteral("Identity"), std::move(identity));

    QList<DetailField> routing;
    addField(routing, QStringLiteral("Transmitters"), joinStrings(message.transmitters()));
    pushSection(sections, QStringLiteral("Routing"), std::move(routing));

    if (!message.comment().empty()) {
        pushSection(sections,
                    QStringLiteral("Comment"),
                    {DetailField{QStringLiteral("Text"), text(message.comment())}});
    }

    QList<DetailField> attributes;
    for (const auto& attribute : message.attributes()) {
        addField(attributes,
                 text(attribute.name()),
                 text(attribute.value()));
    }
    pushSection(sections, QStringLiteral("Attributes"), std::move(attributes));

    for (int s = 0; s < message.signals_size(); ++s) {
        const auto& sig = message.signals(s);
        QList<DetailField> sigFields;
        addField(sigFields, QStringLiteral("Layout"),
                 QStringLiteral("bit %1, %2 bits, %3")
                     .arg(sig.start_bit())
                     .arg(sig.bit_length())
                     .arg(sig.byte_order() == dbc::BYTE_ORDER_BIG_ENDIAN
                          ? QStringLiteral("big-endian") : QStringLiteral("little-endian")));
        if (sig.factor() != 1.0 || sig.offset() != 0.0) {
            addField(sigFields, QStringLiteral("Scaling"),
                     QStringLiteral("factor=%1  offset=%2")
                         .arg(QString::number(sig.factor()), QString::number(sig.offset())));
        }
        if (sig.min() != 0.0 || sig.max() != 0.0) {
            addField(sigFields, QStringLiteral("Range"),
                     QStringLiteral("[%1 .. %2]")
                         .arg(QString::number(sig.min()), QString::number(sig.max())));
        }
        addField(sigFields, QStringLiteral("Unit"), text(sig.unit()));
        addField(sigFields, QStringLiteral("Receivers"), joinStrings(sig.receivers()));
        addField(sigFields, QStringLiteral("Signed"), sig.is_signed() ? QStringLiteral("Yes") : QStringLiteral("No"));
        if (sig.value_type() != dbc::VALUE_TYPE_INTEGER) {
            addField(sigFields, QStringLiteral("Value Type"), text(dbc::ValueType_Name(sig.value_type())));
        }
        if (sig.multiplex_type() != dbc::MULTIPLEX_NONE) {
            QString muxText = text(dbc::MultiplexType_Name(sig.multiplex_type()));
            if (sig.multiplex_type() == dbc::MULTIPLEX_MULTIPLEXED ||
                sig.multiplex_type() == dbc::MULTIPLEX_MULTIPLEXED_AND_MULTIPLEXOR) {
                muxText += QStringLiteral(" (m%1)").arg(sig.multiplex_value());
            }
            addField(sigFields, QStringLiteral("Multiplex"), muxText);
        }
        if (sig.value_descriptions_size() > 0) {
            QStringList vds;
            for (const auto& vd : sig.value_descriptions()) {
                vds.push_back(QStringLiteral("%1=%2")
                    .arg(QString::number(static_cast<qlonglong>(vd.value())), text(vd.description())));
            }
            addField(sigFields, QStringLiteral("Values"), vds.join(QStringLiteral(", ")));
        }
        if (!sig.comment().empty()) {
            addField(sigFields, QStringLiteral("Comment"), text(sig.comment()));
        }
        for (const auto& attr : sig.attributes()) {
            addField(sigFields, text(attr.name()), text(attr.value()));
        }
        if (sig.extended_mux_values_size() > 0) {
            QStringList muxEntries;
            for (const auto& emv : sig.extended_mux_values()) {
                QStringList ranges;
                for (const auto& r : emv.ranges()) {
                    ranges.push_back(QStringLiteral("%1-%2").arg(r.from()).arg(r.to()));
                }
                muxEntries.push_back(QStringLiteral("%1: %2")
                    .arg(text(emv.mux_signal_name()), ranges.join(QStringLiteral(", "))));
            }
            addField(sigFields, QStringLiteral("Extended Mux"), muxEntries.join(QStringLiteral("; ")));
        }
        pushSection(sections, text(sig.name()), std::move(sigFields));
    }
    return sections;
}

QList<DetailSection> DbcDetailPresenter::signalDetails(const DbcPath& path) const {
    QList<DetailSection> sections;
    if (path.primaryIndex < 0 || path.primaryIndex >= _document.messages_size()) {
        return sections;
    }

    const auto& message = _document.messages(path.primaryIndex);
    if (path.secondaryIndex < 0 || path.secondaryIndex >= message.signals_size()) {
        return sections;
    }

    const auto& signal = message.signals(path.secondaryIndex);
    QList<DetailField> identity;
    addField(identity, QStringLiteral("Signal"), text(signal.name()));
    addField(identity, QStringLiteral("Message"), text(message.name()));
    pushSection(sections, QStringLiteral("Identity"), std::move(identity));

    QList<DetailField> layout;
    addNumberField(layout, QStringLiteral("Start Bit"), signal.start_bit());
    addNumberField(layout, QStringLiteral("Bit Length"), signal.bit_length());
    addField(layout,
             QStringLiteral("Byte Order"),
             text(dbc::ByteOrder_Name(signal.byte_order())));
    addField(layout, QStringLiteral("Signed"), boolText(signal.is_signed()));
    pushSection(sections, QStringLiteral("Layout"), std::move(layout));

    QList<DetailField> scaling;
    addNumberField(scaling, QStringLiteral("Factor"), signal.factor());
    addNumberField(scaling, QStringLiteral("Offset"), signal.offset());
    addNumberField(scaling, QStringLiteral("Min"), signal.min());
    addNumberField(scaling, QStringLiteral("Max"), signal.max());
    addField(scaling, QStringLiteral("Unit"), text(signal.unit()));
    addField(scaling,
             QStringLiteral("Value Type"),
             text(dbc::ValueType_Name(signal.value_type())));
    pushSection(sections, QStringLiteral("Scaling"), std::move(scaling));

    QList<DetailField> multiplexing;
    addField(multiplexing,
             QStringLiteral("Mode"),
             text(dbc::MultiplexType_Name(signal.multiplex_type())));
    if (signal.multiplex_type() == dbc::MULTIPLEX_MULTIPLEXED ||
        signal.multiplex_type() == dbc::MULTIPLEX_MULTIPLEXED_AND_MULTIPLEXOR) {
        addNumberField(multiplexing, QStringLiteral("Value"), signal.multiplex_value());
    }
    pushSection(sections, QStringLiteral("Multiplexing"), std::move(multiplexing));

    QList<DetailField> routing;
    addField(routing, QStringLiteral("Receivers"), joinStrings(signal.receivers()));
    pushSection(sections, QStringLiteral("Routing"), std::move(routing));

    if (!signal.comment().empty()) {
        pushSection(sections,
                    QStringLiteral("Comment"),
                    {DetailField{QStringLiteral("Text"), text(signal.comment())}});
    }

    QList<DetailField> values;
    for (const auto& entry : signal.value_descriptions()) {
        addField(values,
                 QString::number(entry.value()),
                 text(entry.description()));
    }
    pushSection(sections, QStringLiteral("Value Descriptions"), std::move(values));

    if (signal.extended_mux_values_size() > 0) {
        QList<DetailField> extMux;
        for (const auto& emv : signal.extended_mux_values()) {
            QStringList ranges;
            for (const auto& r : emv.ranges()) {
                ranges << QStringLiteral("%1-%2").arg(r.from()).arg(r.to());
            }
            addField(extMux,
                     text(emv.mux_signal_name()),
                     ranges.join(QStringLiteral(", ")));
        }
        pushSection(sections, QStringLiteral("Extended Multiplexing"), std::move(extMux));
    }

    QList<DetailField> attributes;
    for (const auto& attribute : signal.attributes()) {
        addField(attributes,
                 text(attribute.name()),
                 text(attribute.value()));
    }
    pushSection(sections, QStringLiteral("Attributes"), std::move(attributes));
    return sections;
}

QList<DetailSection> DbcDetailPresenter::valueTableDetails(const DbcPath& path) const {
    QList<DetailSection> sections;
    if (path.primaryIndex < 0 || path.primaryIndex >= _document.value_tables_size()) {
        return sections;
    }

    const auto& table = _document.value_tables(path.primaryIndex);
    pushSection(sections,
                QStringLiteral("Identity"),
                {
                    DetailField{QStringLiteral("Name"), text(table.name())},
                    DetailField{QStringLiteral("Entries"), QString::number(table.entries_size())},
                });

    QList<DetailField> entries;
    for (const auto& entry : table.entries()) {
        addField(entries,
                 QString::number(entry.value()),
                 text(entry.description()));
    }
    pushSection(sections, QStringLiteral("Entries"), std::move(entries));
    return sections;
}

QList<DetailSection> DbcDetailPresenter::attributeDefinitionDetails(const DbcPath& path) const {
    QList<DetailSection> sections;
    if (path.primaryIndex < 0 || path.primaryIndex >= _document.attribute_definitions_size()) {
        return sections;
    }

    const auto& definition = _document.attribute_definitions(path.primaryIndex);
    QList<DetailField> fields;
    addField(fields, QStringLiteral("Name"), text(definition.name()));
    addField(fields,
             QStringLiteral("Scope"),
             text(dbc::AttributeScope_Name(definition.scope())));
    addField(fields,
             QStringLiteral("Value Type"),
             text(dbc::AttributeValueType_Name(definition.value_type())));
    addNumberField(fields, QStringLiteral("Int Min"), definition.int_min());
    addNumberField(fields, QStringLiteral("Int Max"), definition.int_max());
    addNumberField(fields, QStringLiteral("Float Min"), definition.float_min());
    addNumberField(fields, QStringLiteral("Float Max"), definition.float_max());
    addField(fields,
             QStringLiteral("Enum Values"),
             joinStrings(definition.enum_values()));
    pushSection(sections, QStringLiteral("Definition"), std::move(fields));
    return sections;
}

QList<DetailSection> DbcDetailPresenter::attributeDefaultDetails(const DbcPath& path) const {
    QList<DetailSection> sections;
    if (path.primaryIndex < 0 || path.primaryIndex >= _document.attribute_defaults_size()) {
        return sections;
    }

    const auto& value = _document.attribute_defaults(path.primaryIndex);
    pushSection(sections,
                QStringLiteral("Default"),
                {
                    DetailField{QStringLiteral("Name"), text(value.name())},
                    DetailField{QStringLiteral("Value"), text(value.value())},
                });
    return sections;
}

QList<DetailSection> DbcDetailPresenter::attributeValueDetails(const DbcPath& path) const {
    QList<DetailSection> sections;
    if (path.primaryIndex < 0 || path.primaryIndex >= _document.attribute_values_size()) {
        return sections;
    }

    const auto& value = _document.attribute_values(path.primaryIndex);
    QList<DetailField> fields;
    addField(fields, QStringLiteral("Name"), text(value.name()));
    addField(fields, QStringLiteral("Value"), text(value.value()));
    addField(fields,
             QStringLiteral("Scope"),
             text(dbc::AttributeScope_Name(value.scope())));
    pushSection(sections, QStringLiteral("Attribute"), std::move(fields));
    return sections;
}

QList<DetailSection> DbcDetailPresenter::environmentVariableDetails(const DbcPath& path) const {
    QList<DetailSection> sections;
    if (path.primaryIndex < 0 || path.primaryIndex >= _document.environment_variables_size()) {
        return sections;
    }

    const auto& envVar = _document.environment_variables(path.primaryIndex);
    QList<DetailField> identity;
    addField(identity, QStringLiteral("Name"), text(envVar.name()));
    addField(identity,
             QStringLiteral("Type"),
             text(dbc::EnvironmentVariableType_Name(envVar.var_type())));
    addField(identity, QStringLiteral("Unit"), text(envVar.unit()));
    pushSection(sections, QStringLiteral("Identity"), std::move(identity));

    QList<DetailField> range;
    addNumberField(range, QStringLiteral("Min"), envVar.min());
    addNumberField(range, QStringLiteral("Max"), envVar.max());
    addNumberField(range, QStringLiteral("Initial"), envVar.initial_value());
    addNumberField(range, QStringLiteral("Data Size"), envVar.data_size());
    pushSection(sections, QStringLiteral("Range"), std::move(range));

    QList<DetailField> access;
    addField(access, QStringLiteral("Access Type"), text(envVar.access_type()));
    addField(access, QStringLiteral("Access Nodes"), joinStrings(envVar.access_nodes()));
    pushSection(sections, QStringLiteral("Access"), std::move(access));

    if (!envVar.comment().empty()) {
        pushSection(sections,
                    QStringLiteral("Comment"),
                    {DetailField{QStringLiteral("Text"), text(envVar.comment())}});
    }
    return sections;
}

QList<DetailSection> DbcDetailPresenter::signalGroupDetails(const DbcPath& path) const {
    QList<DetailSection> sections;
    if (path.primaryIndex < 0 || path.primaryIndex >= _document.signal_groups_size()) {
        return sections;
    }

    const auto& group = _document.signal_groups(path.primaryIndex);
    QList<DetailField> identity;
    addField(identity, QStringLiteral("Name"), text(group.name()));
    addField(identity, QStringLiteral("Message ID"), hexId(group.message_id()));
    addNumberField(identity, QStringLiteral("Repetitions"), group.repetitions());
    pushSection(sections, QStringLiteral("Identity"), std::move(identity));

    QList<DetailField> signals;
    for (const std::string& signalName : group.signal_names()) {
        addField(signals, text(signalName), QStringLiteral("Included"));
    }
    pushSection(sections, QStringLiteral("Signals"), std::move(signals));
    return sections;
}

void DbcDetailPresenter::appendNodeCrossReferences(QList<DetailSection>& sections, const std::string& nodeName) const {
    QStringList sentMessages;
    QStringList receivedSignals;

    for (int i = 0; i < _document.messages_size(); ++i) {
        const auto& msg = _document.messages(i);
        if (msg.sender() == nodeName) {
            sentMessages.push_back(text(msg.name()));
        }
        for (int j = 0; j < msg.signals_size(); ++j) {
            const auto& sig = msg.signals(j);
            for (const auto& rx : sig.receivers()) {
                if (rx == nodeName) {
                    receivedSignals.push_back(
                        QStringLiteral("%1.%2").arg(text(msg.name()), text(sig.name())));
                    break;
                }
            }
        }
    }

    QList<DetailField> fields;
    if (!sentMessages.isEmpty()) {
        addField(fields,
                 QStringLiteral("Sends (%1)").arg(sentMessages.size()),
                 sentMessages.join(QStringLiteral(", ")));
    }
    if (!receivedSignals.isEmpty()) {
        addField(fields,
                 QStringLiteral("Receives (%1)").arg(receivedSignals.size()),
                 receivedSignals.join(QStringLiteral(", ")));
    }
    pushSection(sections, QStringLiteral("Referenced By"), std::move(fields));
}
