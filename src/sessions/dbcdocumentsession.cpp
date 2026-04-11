#include "sessions/dbcdocumentsession.h"

#include <QStringList>
#include <type_traits>

#undef signals

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

class DbcDetailPresenter final : public DetailPresenter {
public:
    explicit DbcDetailPresenter(const dbc::DbcFile& document)
        : document_(document) {
    }

    QList<DetailSection> buildDetails(const NodeBinding& binding) const override {
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

private:
    QList<DetailSection> nodeDetails(const DbcPath& path) const {
        QList<DetailSection> sections;
        if (path.primaryIndex < 0 || path.primaryIndex >= document_.nodes_size()) {
            return sections;
        }

        const auto& node = document_.nodes(path.primaryIndex);
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
        return sections;
    }

    QList<DetailSection> messageDetails(const DbcPath& path) const {
        QList<DetailSection> sections;
        if (path.primaryIndex < 0 || path.primaryIndex >= document_.messages_size()) {
            return sections;
        }

        const auto& message = document_.messages(path.primaryIndex);
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
        return sections;
    }

    QList<DetailSection> signalDetails(const DbcPath& path) const {
        QList<DetailSection> sections;
        if (path.primaryIndex < 0 || path.primaryIndex >= document_.messages_size()) {
            return sections;
        }

        const auto& message = document_.messages(path.primaryIndex);
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

        QList<DetailField> attributes;
        for (const auto& attribute : signal.attributes()) {
            addField(attributes,
                     text(attribute.name()),
                     text(attribute.value()));
        }
        pushSection(sections, QStringLiteral("Attributes"), std::move(attributes));
        return sections;
    }

    QList<DetailSection> valueTableDetails(const DbcPath& path) const {
        QList<DetailSection> sections;
        if (path.primaryIndex < 0 || path.primaryIndex >= document_.value_tables_size()) {
            return sections;
        }

        const auto& table = document_.value_tables(path.primaryIndex);
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

    QList<DetailSection> attributeDefinitionDetails(const DbcPath& path) const {
        QList<DetailSection> sections;
        if (path.primaryIndex < 0 || path.primaryIndex >= document_.attribute_definitions_size()) {
            return sections;
        }

        const auto& definition = document_.attribute_definitions(path.primaryIndex);
        QList<DetailField> fields;
        addField(fields, QStringLiteral("Name"), text(definition.name()));
        addField(fields,
                 QStringLiteral("Scope"),
                 text(dbc::AttributeScope_Name(definition.scope())));
        addField(fields,
                 QStringLiteral("Value Type"),
                 text(dbc::AttributeValueType_Name(definition.value_type())));
        addField(fields,
                 QStringLiteral("Enum Values"),
                 joinStrings(definition.enum_values()));
        pushSection(sections, QStringLiteral("Definition"), std::move(fields));
        return sections;
    }

    QList<DetailSection> attributeDefaultDetails(const DbcPath& path) const {
        QList<DetailSection> sections;
        if (path.primaryIndex < 0 || path.primaryIndex >= document_.attribute_defaults_size()) {
            return sections;
        }

        const auto& value = document_.attribute_defaults(path.primaryIndex);
        pushSection(sections,
                    QStringLiteral("Default"),
                    {
                        DetailField{QStringLiteral("Name"), text(value.name())},
                        DetailField{QStringLiteral("Value"), text(value.value())},
                    });
        return sections;
    }

    QList<DetailSection> attributeValueDetails(const DbcPath& path) const {
        QList<DetailSection> sections;
        if (path.primaryIndex < 0 || path.primaryIndex >= document_.attribute_values_size()) {
            return sections;
        }

        const auto& value = document_.attribute_values(path.primaryIndex);
        QList<DetailField> fields;
        addField(fields, QStringLiteral("Name"), text(value.name()));
        addField(fields, QStringLiteral("Value"), text(value.value()));
        addField(fields,
                 QStringLiteral("Scope"),
                 text(dbc::AttributeScope_Name(value.scope())));
        pushSection(sections, QStringLiteral("Attribute"), std::move(fields));
        return sections;
    }

    QList<DetailSection> environmentVariableDetails(const DbcPath& path) const {
        QList<DetailSection> sections;
        if (path.primaryIndex < 0 || path.primaryIndex >= document_.environment_variables_size()) {
            return sections;
        }

        const auto& envVar = document_.environment_variables(path.primaryIndex);
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

    QList<DetailSection> signalGroupDetails(const DbcPath& path) const {
        QList<DetailSection> sections;
        if (path.primaryIndex < 0 || path.primaryIndex >= document_.signal_groups_size()) {
            return sections;
        }

        const auto& group = document_.signal_groups(path.primaryIndex);
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

    const dbc::DbcFile& document_;
};

} // namespace

DbcDocumentSession::DbcDocumentSession(QString displayName,
                                       QString sourcePath,
                                       dbc::DbcFile document,
                                       QList<DiagnosticMessage> diagnostics)
    : AdapterSessionBase(FormatId::DBC,
                         QStringLiteral("DBC"),
                         std::move(displayName),
                         std::move(sourcePath),
                         std::move(diagnostics)),
      document_(std::move(document)) {
    setDetailPresenter(std::make_unique<DbcDetailPresenter>(document_));
    buildTree();
}

void DbcDocumentSession::buildTree() {
    auto root = std::make_unique<TreeItem>();
    root->title = displayName();
    root->semanticKind = SemanticKind::Root;

    if (document_.nodes_size() > 0) {
        TreeItem* section = appendNode(root.get(), QStringLiteral("Nodes"), QString::number(document_.nodes_size()), QStringLiteral("nodes"), SemanticKind::Section);
        for (int i = 0; i < document_.nodes_size(); ++i) {
            const auto& node = document_.nodes(i);
            appendNode(section,
                       text(node.name()),
                       node.comment().empty() ? QString() : QStringLiteral("commented"),
                       QStringLiteral("node"),
                       SemanticKind::Entity,
                       NodeBinding{SemanticKind::Entity, DbcPath{DbcEntityKind::Node, i, -1, -1}, true});
        }
    }

    if (document_.messages_size() > 0) {
        TreeItem* section = appendNode(root.get(), QStringLiteral("Messages"), QString::number(document_.messages_size()), QStringLiteral("messages"), SemanticKind::Section);
        for (int i = 0; i < document_.messages_size(); ++i) {
            const auto& message = document_.messages(i);
            TreeItem* messageItem = appendNode(section,
                                               text(message.name()),
                                               QStringLiteral("%1  DLC=%2").arg(hexId(message.id())).arg(message.dlc()),
                                               QStringLiteral("message"),
                                               SemanticKind::Entity,
                                               NodeBinding{SemanticKind::Entity, DbcPath{DbcEntityKind::Message, i, -1, -1}, true});

            for (int j = 0; j < message.signals_size(); ++j) {
                const auto& signal = message.signals(j);
                appendNode(messageItem,
                           text(signal.name()),
                           QStringLiteral("[%1|%2]").arg(signal.start_bit()).arg(signal.bit_length()),
                           QStringLiteral("signal"),
                           SemanticKind::Entity,
                           NodeBinding{SemanticKind::Entity, DbcPath{DbcEntityKind::Signal, i, j, -1}, true});
            }
        }
    }

    if (document_.value_tables_size() > 0) {
        TreeItem* section = appendNode(root.get(), QStringLiteral("Value Tables"), QString::number(document_.value_tables_size()), QStringLiteral("valuetables"), SemanticKind::Section);
        for (int i = 0; i < document_.value_tables_size(); ++i) {
            const auto& table = document_.value_tables(i);
            appendNode(section,
                       text(table.name()),
                       QString::number(table.entries_size()),
                       QStringLiteral("valuetable"),
                       SemanticKind::Entity,
                       NodeBinding{SemanticKind::Entity, DbcPath{DbcEntityKind::ValueTable, i, -1, -1}, true});
        }
    }

    if (document_.environment_variables_size() > 0) {
        TreeItem* section = appendNode(root.get(), QStringLiteral("Environment Variables"), QString::number(document_.environment_variables_size()), QStringLiteral("envvars"), SemanticKind::Section);
        for (int i = 0; i < document_.environment_variables_size(); ++i) {
            const auto& envVar = document_.environment_variables(i);
            appendNode(section,
                       text(envVar.name()),
                       text(dbc::EnvironmentVariableType_Name(envVar.var_type())),
                       QStringLiteral("envvar"),
                       SemanticKind::Entity,
                       NodeBinding{SemanticKind::Entity, DbcPath{DbcEntityKind::EnvironmentVariable, i, -1, -1}, true});
        }
    }

    if (document_.signal_groups_size() > 0) {
        TreeItem* section = appendNode(root.get(), QStringLiteral("Signal Groups"), QString::number(document_.signal_groups_size()), QStringLiteral("signalgroups"), SemanticKind::Section);
        for (int i = 0; i < document_.signal_groups_size(); ++i) {
            const auto& group = document_.signal_groups(i);
            appendNode(section,
                       text(group.name()),
                       hexId(group.message_id()),
                       QStringLiteral("signalgroup"),
                       SemanticKind::Entity,
                       NodeBinding{SemanticKind::Entity, DbcPath{DbcEntityKind::SignalGroup, i, -1, -1}, true});
        }
    }

    if (document_.attribute_definitions_size() > 0 || document_.attribute_defaults_size() > 0 || document_.attribute_values_size() > 0) {
        TreeItem* attributes = appendNode(root.get(), QStringLiteral("Attributes"), QString(), QStringLiteral("attributes"), SemanticKind::Section);

        if (document_.attribute_definitions_size() > 0) {
            TreeItem* defs = appendNode(attributes, QStringLiteral("Definitions"), QString::number(document_.attribute_definitions_size()), QStringLiteral("attrdefs"), SemanticKind::Section);
            for (int i = 0; i < document_.attribute_definitions_size(); ++i) {
                const auto& definition = document_.attribute_definitions(i);
                appendNode(defs,
                           text(definition.name()),
                           text(dbc::AttributeScope_Name(definition.scope())),
                           QStringLiteral("attrdef"),
                           SemanticKind::Entity,
                           NodeBinding{SemanticKind::Entity, DbcPath{DbcEntityKind::AttributeDefinition, i, -1, -1}, true});
            }
        }

        if (document_.attribute_defaults_size() > 0) {
            TreeItem* defaults = appendNode(attributes, QStringLiteral("Defaults"), QString::number(document_.attribute_defaults_size()), QStringLiteral("attrdefaults"), SemanticKind::Section);
            for (int i = 0; i < document_.attribute_defaults_size(); ++i) {
                const auto& value = document_.attribute_defaults(i);
                appendNode(defaults,
                           text(value.name()),
                           text(value.value()),
                           QStringLiteral("attrdefault"),
                           SemanticKind::Entity,
                           NodeBinding{SemanticKind::Entity, DbcPath{DbcEntityKind::AttributeDefault, i, -1, -1}, true});
            }
        }

        if (document_.attribute_values_size() > 0) {
            TreeItem* values = appendNode(attributes, QStringLiteral("Global Values"), QString::number(document_.attribute_values_size()), QStringLiteral("attrvalues"), SemanticKind::Section);
            for (int i = 0; i < document_.attribute_values_size(); ++i) {
                const auto& value = document_.attribute_values(i);
                appendNode(values,
                           text(value.name()),
                           text(value.value()),
                           QStringLiteral("attrvalue"),
                           SemanticKind::Entity,
                           NodeBinding{SemanticKind::Entity, DbcPath{DbcEntityKind::AttributeValue, i, -1, -1}, true});
            }
        }
    }

    setRootItem(std::move(root));
}
