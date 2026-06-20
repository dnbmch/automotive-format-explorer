#include "sessions/dbcdocumentsession.h"
#include "sessions/dbcdetailpresenter.h"
#include "models/signalmapmodel.h"

#include <QStringList>
#include <algorithm>

#undef signals  // dbc proto's repeated `signals` field vs Qt's `signals` keyword macro

namespace {

QString text(const std::string& value) {
    auto utf8 = QString::fromUtf8(value.data(), static_cast<int>(value.size()));
    if (utf8.contains(QChar::ReplacementCharacter)) {
        return QString::fromLatin1(value.data(), static_cast<int>(value.size()));
    }
    return utf8;
}

QString hexId(quint32 id) {
    return QStringLiteral("0x%1").arg(id, 0, 16).toUpper();
}

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
      _document(std::move(document)) {
    setDetailPresenter(std::make_unique<DbcDetailPresenter>(_document));
    buildTree();
    buildSignalMap();
}

DbcDocumentSession::~DbcDocumentSession() = default;

QUrl DbcDocumentSession::centerPanelSource() const {
    if (_signal_map_model && _signal_map_model->messageCount() > 0) {
        return QUrl(QStringLiteral("qrc:/qt/qml/ExplorerApp/qml/components/SignalMapView.qml"));
    }
    return {};
}

QAbstractListModel* DbcDocumentSession::centerPanelModel() {
    return _signal_map_model.get();
}

void DbcDocumentSession::moveModelsToThread(QThread* thread) {
    AdapterSessionBase::moveModelsToThread(thread);
    if (_signal_map_model)
        _signal_map_model->moveToThread(thread);
}

void DbcDocumentSession::buildTree() {
    auto root = std::make_unique<TreeItem>();
    root->title = displayName();
    root->semanticKind = SemanticKind::Root;

    if (_document.nodes_size() > 0) {
        TreeItem* section = appendNode(root.get(), QStringLiteral("Nodes"), QString::number(_document.nodes_size()), QStringLiteral("nodes"), SemanticKind::Section);
        for (int i = 0; i < _document.nodes_size(); ++i) {
            const auto& node = _document.nodes(i);
            appendNode(section,
                       text(node.name()),
                       node.comment().empty() ? QString() : QStringLiteral("commented"),
                       QStringLiteral("node"),
                       SemanticKind::Entity,
                       NodeBinding{SemanticKind::Entity, DbcPath{DbcEntityKind::Node, i, -1, -1}, true});
        }
    }

    if (_document.messages_size() > 0) {
        TreeItem* section = appendNode(root.get(), QStringLiteral("Messages"), QString::number(_document.messages_size()), QStringLiteral("messages"), SemanticKind::Section);
        for (int i = 0; i < _document.messages_size(); ++i) {
            const auto& message = _document.messages(i);
            TreeItem* messageItem = appendNode(section,
                                               text(message.name()),
                                               QStringLiteral("%1  DLC=%2").arg(hexId(message.id())).arg(message.dlc()),
                                               QStringLiteral("message"),
                                               SemanticKind::Entity,
                                               NodeBinding{SemanticKind::Entity, DbcPath{DbcEntityKind::Message, i, -1, -1}, true});
            _tree_node_keys[{static_cast<int>(DbcEntityKind::Message), i, -1}] = messageItem->nodeKey;

            for (int j = 0; j < message.signals_size(); ++j) {
                const auto& signal = message.signals(j);
                TreeItem* sigItem = appendNode(messageItem,
                           text(signal.name()),
                           QStringLiteral("[%1|%2]").arg(signal.start_bit()).arg(signal.bit_length()),
                           QStringLiteral("signal"),
                           SemanticKind::Entity,
                           NodeBinding{SemanticKind::Entity, DbcPath{DbcEntityKind::Signal, i, j, -1}, true});
                _tree_node_keys[{static_cast<int>(DbcEntityKind::Signal), i, j}] = sigItem->nodeKey;
            }
        }
    }

    if (_document.value_tables_size() > 0) {
        TreeItem* section = appendNode(root.get(), QStringLiteral("Value Tables"), QString::number(_document.value_tables_size()), QStringLiteral("valuetables"), SemanticKind::Section);
        for (int i = 0; i < _document.value_tables_size(); ++i) {
            const auto& table = _document.value_tables(i);
            appendNode(section,
                       text(table.name()),
                       QString::number(table.entries_size()),
                       QStringLiteral("valuetable"),
                       SemanticKind::Entity,
                       NodeBinding{SemanticKind::Entity, DbcPath{DbcEntityKind::ValueTable, i, -1, -1}, true});
        }
    }

    if (_document.environment_variables_size() > 0) {
        TreeItem* section = appendNode(root.get(), QStringLiteral("Environment Variables"), QString::number(_document.environment_variables_size()), QStringLiteral("envvars"), SemanticKind::Section);
        for (int i = 0; i < _document.environment_variables_size(); ++i) {
            const auto& envVar = _document.environment_variables(i);
            appendNode(section,
                       text(envVar.name()),
                       text(dbc::EnvironmentVariableType_Name(envVar.var_type())),
                       QStringLiteral("envvar"),
                       SemanticKind::Entity,
                       NodeBinding{SemanticKind::Entity, DbcPath{DbcEntityKind::EnvironmentVariable, i, -1, -1}, true});
        }
    }

    if (_document.signal_groups_size() > 0) {
        TreeItem* section = appendNode(root.get(), QStringLiteral("Signal Groups"), QString::number(_document.signal_groups_size()), QStringLiteral("signalgroups"), SemanticKind::Section);
        for (int i = 0; i < _document.signal_groups_size(); ++i) {
            const auto& group = _document.signal_groups(i);
            appendNode(section,
                       text(group.name()),
                       hexId(group.message_id()),
                       QStringLiteral("signalgroup"),
                       SemanticKind::Entity,
                       NodeBinding{SemanticKind::Entity, DbcPath{DbcEntityKind::SignalGroup, i, -1, -1}, true});
        }
    }

    if (_document.attribute_definitions_size() > 0 || _document.attribute_defaults_size() > 0 || _document.attribute_values_size() > 0) {
        TreeItem* attributes = appendNode(root.get(), QStringLiteral("Attributes"), QString(), QStringLiteral("attributes"), SemanticKind::Section);

        if (_document.attribute_definitions_size() > 0) {
            TreeItem* defs = appendNode(attributes, QStringLiteral("Definitions"), QString::number(_document.attribute_definitions_size()), QStringLiteral("attrdefs"), SemanticKind::Section);
            for (int i = 0; i < _document.attribute_definitions_size(); ++i) {
                const auto& definition = _document.attribute_definitions(i);
                appendNode(defs,
                           text(definition.name()),
                           text(dbc::AttributeScope_Name(definition.scope())),
                           QStringLiteral("attrdef"),
                           SemanticKind::Entity,
                           NodeBinding{SemanticKind::Entity, DbcPath{DbcEntityKind::AttributeDefinition, i, -1, -1}, true});
            }
        }

        if (_document.attribute_defaults_size() > 0) {
            TreeItem* defaults = appendNode(attributes, QStringLiteral("Defaults"), QString::number(_document.attribute_defaults_size()), QStringLiteral("attrdefaults"), SemanticKind::Section);
            for (int i = 0; i < _document.attribute_defaults_size(); ++i) {
                const auto& value = _document.attribute_defaults(i);
                appendNode(defaults,
                           text(value.name()),
                           text(value.value()),
                           QStringLiteral("attrdefault"),
                           SemanticKind::Entity,
                           NodeBinding{SemanticKind::Entity, DbcPath{DbcEntityKind::AttributeDefault, i, -1, -1}, true});
            }
        }

        if (_document.attribute_values_size() > 0) {
            TreeItem* values = appendNode(attributes, QStringLiteral("Global Values"), QString::number(_document.attribute_values_size()), QStringLiteral("attrvalues"), SemanticKind::Section);
            for (int i = 0; i < _document.attribute_values_size(); ++i) {
                const auto& value = _document.attribute_values(i);
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

void DbcDocumentSession::buildSignalMap() {
    _signal_map_model = std::make_unique<SignalMapModel>();

    for (int i = 0; i < _document.messages_size(); ++i) {
        const auto& message = _document.messages(i);

        MessageEntry entry;
        entry.name = text(message.name());
        entry.id = message.id();
        entry.dlc = static_cast<int>(message.dlc());
        entry.isExtendedId = message.is_extended_id();
        entry.sender = text(message.sender());

        auto keyIt = _tree_node_keys.find({static_cast<int>(DbcEntityKind::Message), i, -1});
        if (keyIt != _tree_node_keys.end()) entry.nodeKey = keyIt->second;

        for (int j = 0; j < message.signals_size(); ++j) {
            const auto& signal = message.signals(j);

            SignalEntry sig;
            sig.name = text(signal.name());
            sig.startBit = static_cast<int>(signal.start_bit());
            sig.bitLength = std::max(1, static_cast<int>(signal.bit_length()));
            sig.bigEndian = (signal.byte_order() == dbc::BYTE_ORDER_BIG_ENDIAN);
            sig.isSigned = signal.is_signed();
            sig.factor = signal.factor();
            sig.offset = signal.offset();
            sig.minimum = signal.min();
            sig.maximum = signal.max();
            sig.unit = text(signal.unit());
            sig.colorIndex = j % 8;
            sig.multiplexType = static_cast<int>(signal.multiplex_type());
            sig.multiplexValue = static_cast<int>(signal.multiplex_value());

            for (const auto& r : signal.receivers()) {
                sig.receivers.push_back(text(r));
            }

            auto sigKeyIt = _tree_node_keys.find({static_cast<int>(DbcEntityKind::Signal), i, j});
            if (sigKeyIt != _tree_node_keys.end()) sig.nodeKey = sigKeyIt->second;

            entry.signalEntries.push_back(std::move(sig));
        }

        // Derive DLC from signals when unspecified (DLC=0).
        // Uses SignalMapModel::bitPositions() to avoid duplicating BE walk logic.
        if (entry.dlc == 0 && !entry.signalEntries.empty()) {
            int maxByteUsed = -1;
            for (const auto& sig : entry.signalEntries) {
                auto positions = SignalMapModel::bitPositions(sig);
                for (int pos : positions) {
                    maxByteUsed = std::max(maxByteUsed, pos / 8);
                }
            }
            if (maxByteUsed >= 0) entry.dlc = maxByteUsed + 1;
        }

        _signal_map_model->addMessage(std::move(entry));
    }

    _signal_map_model->finalize();
}
