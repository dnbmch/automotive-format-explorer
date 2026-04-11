#include "sessions/ldfdocumentsession.h"
#include "models/signalmapmodel.h"

#include <QStringList>
#include <algorithm>
#include <unordered_map>

#undef signals

#include "core/proto_json_compat.h"

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

QString numberText(double value) {
    return QString::number(value, 'g', 12);
}

template<typename T>
QString numberText(T value) {
    return QString::number(static_cast<qlonglong>(value));
}

QString hexValue(quint32 value) {
    return QStringLiteral("0x%1").arg(value, 0, 16).toUpper();
}

QString hexAndDecimal(quint32 value) {
    return QStringLiteral("%1 (%2)").arg(hexValue(value)).arg(value);
}

QString joinStrings(const google::protobuf::RepeatedPtrField<std::string>& values) {
    QStringList items;
    for (const std::string& value : values) {
        items.push_back(text(value));
    }
    return items.join(QStringLiteral(", "));
}

QString joinNumbers(const google::protobuf::RepeatedField<google::protobuf::uint32>& values) {
    QStringList items;
    for (google::protobuf::uint32 value : values) {
        items.push_back(hexAndDecimal(value));
    }
    return items.join(QStringLiteral(", "));
}

void addField(QList<DetailField>& fields, const QString& key, const QString& value) {
    if (!value.isEmpty()) {
        fields.push_back(DetailField{key, value});
    }
}

template<typename T>
void addNumberField(QList<DetailField>& fields, const QString& key, T value) {
    fields.push_back(DetailField{key, numberText(value)});
}

template<typename T>
void addOptionalNumberField(QList<DetailField>& fields, const QString& key, T value) {
    if (value != 0) {
        addNumberField(fields, key, value);
    }
}

void pushSection(QList<DetailSection>& sections, const QString& title, QList<DetailField> fields) {
    if (!fields.isEmpty()) {
        sections.push_back(DetailSection{title, std::move(fields)});
    }
}

class LdfDetailPresenter final : public DetailPresenter {
public:
    explicit LdfDetailPresenter(const ldf::LdfFile& document)
        : document_(document) {
    }

    QList<DetailSection> buildDetails(const NodeBinding& binding) const override {
        if (!std::holds_alternative<LdfPath>(binding.payload)) {
            return {};
        }

        const LdfPath path = std::get<LdfPath>(binding.payload);
        switch (path.kind) {
        case LdfEntityKind::Overview:
            return overviewDetails();
        case LdfEntityKind::MasterNode:
            return nodeDetails(document_.master(), true);
        case LdfEntityKind::SlaveNode:
            return path.primaryIndex >= 0 && path.primaryIndex < document_.slaves_size()
                ? nodeDetails(document_.slaves(path.primaryIndex), false)
                : QList<DetailSection>{};
        case LdfEntityKind::Frame:
            return frameDetails(path);
        case LdfEntityKind::FrameSignal:
            return frameSignalDetails(path);
        case LdfEntityKind::Signal:
            return signalDetails(path);
        case LdfEntityKind::Encoding:
            return encodingDetails(path);
        case LdfEntityKind::ScheduleTable:
            return scheduleDetails(path);
        case LdfEntityKind::EventFrame:
            return eventFrameDetails(path);
        case LdfEntityKind::DiagnosticAddress:
            return diagnosticAddressDetails(path);
        case LdfEntityKind::SignalGroup:
            return signalGroupDetails(path);
        case LdfEntityKind::DiagnosticSignal:
            return diagnosticSignalDetails(path);
        case LdfEntityKind::DiagnosticFrame:
            return diagnosticFrameDetails(path);
        }

        return {};
    }

    QString buildRawJson(const NodeBinding& binding) const override {
        if (!std::holds_alternative<LdfPath>(binding.payload)) {
            return {};
        }

        const LdfPath path = std::get<LdfPath>(binding.payload);
        const google::protobuf::Message* msg = nullptr;
        int i = path.primaryIndex;
        int j = path.secondaryIndex;

        switch (path.kind) {
        case LdfEntityKind::Overview:          break;
        case LdfEntityKind::MasterNode:        if (document_.has_master()) msg = &document_.master(); break;
        case LdfEntityKind::SlaveNode:         if (i >= 0 && i < document_.slaves_size()) msg = &document_.slaves(i); break;
        case LdfEntityKind::Frame:             if (i >= 0 && i < document_.frames_size()) msg = &document_.frames(i); break;
        case LdfEntityKind::FrameSignal:       if (i >= 0 && i < document_.frames_size() && j >= 0 && j < document_.frames(i).signals_size()) msg = &document_.frames(i).signals(j); break;
        case LdfEntityKind::Signal:            if (i >= 0 && i < document_.signals_size()) msg = &document_.signals(i); break;
        case LdfEntityKind::Encoding:          if (i >= 0 && i < document_.signal_encoding_types_size()) msg = &document_.signal_encoding_types(i); break;
        case LdfEntityKind::ScheduleTable:     if (i >= 0 && i < document_.schedule_tables_size()) msg = &document_.schedule_tables(i); break;
        case LdfEntityKind::EventFrame:        if (i >= 0 && i < document_.event_triggered_frames_size()) msg = &document_.event_triggered_frames(i); break;
        case LdfEntityKind::DiagnosticAddress: if (i >= 0 && i < document_.diagnostic_addresses_size()) msg = &document_.diagnostic_addresses(i); break;
        case LdfEntityKind::SignalGroup:       if (i >= 0 && i < document_.signal_groups_size()) msg = &document_.signal_groups(i); break;
        case LdfEntityKind::DiagnosticSignal:  if (i >= 0 && i < document_.diagnostic_signals_size()) msg = &document_.diagnostic_signals(i); break;
        case LdfEntityKind::DiagnosticFrame:   if (i >= 0 && i < document_.diagnostic_frames_size()) msg = &document_.diagnostic_frames(i); break;
        }

        if (!msg) {
            return {};
        }

        proto_json::PrintOptions opts;
        opts.add_whitespace = true;
        std::string json;
        auto status = proto_json::MessageToJsonString(*msg, &json, opts);
        if (!status.ok()) {
            return {};
        }
        return text(json);
    }

private:
    const ldf::Signal* findSignal(const std::string& name) const {
        for (int i = 0; i < document_.signals_size(); ++i) {
            const auto& signal = document_.signals(i);
            if (signal.name() == name) {
                return &signal;
            }
        }
        return nullptr;
    }

    QList<DetailSection> overviewDetails() const {
        QList<DetailSection> sections;
        pushSection(sections,
                    QStringLiteral("Header"),
                    {
                        DetailField{QStringLiteral("LIN Protocol"), text(document_.lin_protocol_version())},
                        DetailField{QStringLiteral("Language Version"), text(document_.lin_language_version())},
                        DetailField{QStringLiteral("Speed (kbps)"), numberText(document_.lin_speed_kbps())},
                        DetailField{QStringLiteral("Channel"), text(document_.channel_name())},
                        DetailField{QStringLiteral("LDF Revision"), text(document_.ldf_file_revision())},
                        DetailField{QStringLiteral("Big Endian Signals"), boolText(document_.big_endian_signals())},
                    });

        pushSection(sections,
                    QStringLiteral("Contents"),
                    {
                        DetailField{QStringLiteral("Master Node"), document_.has_master() ? text(document_.master().name()) : QStringLiteral("Not defined")},
                        DetailField{QStringLiteral("Slave Nodes"), numberText(document_.slaves_size())},
                        DetailField{QStringLiteral("Signals"), numberText(document_.signals_size())},
                        DetailField{QStringLiteral("Frames"), numberText(document_.frames_size())},
                        DetailField{QStringLiteral("Signal Encodings"), numberText(document_.signal_encoding_types_size())},
                        DetailField{QStringLiteral("Schedule Tables"), numberText(document_.schedule_tables_size())},
                        DetailField{QStringLiteral("Event Frames"), numberText(document_.event_triggered_frames_size())},
                        DetailField{QStringLiteral("Diagnostic Addresses"), numberText(document_.diagnostic_addresses_size())},
                        DetailField{QStringLiteral("Signal Groups"), numberText(document_.signal_groups_size())},
                        DetailField{QStringLiteral("Diagnostic Signals"), numberText(document_.diagnostic_signals_size())},
                        DetailField{QStringLiteral("Diagnostic Frames"), numberText(document_.diagnostic_frames_size())},
                    });
        return sections;
    }

    QList<DetailSection> nodeDetails(const ldf::Node& node, bool isMaster) const {
        QList<DetailSection> sections;
        QList<DetailField> identity;
        addField(identity, QStringLiteral("Name"), text(node.name()));
        addField(identity, QStringLiteral("Role"), text(ldf::NodeRole_Name(node.role())));
        pushSection(sections, QStringLiteral("Identity"), std::move(identity));

        QList<DetailField> timing;
        addOptionalNumberField(timing, QStringLiteral("Timebase (ms)"), node.timebase_ms());
        addOptionalNumberField(timing, QStringLiteral("Jitter (ms)"), node.jitter_ms());
        addOptionalNumberField(timing, QStringLiteral("Max Frame Bits"), node.max_frame_bits());
        addOptionalNumberField(timing, QStringLiteral("Duty Cycle (%)"), node.duty_cycle_pct());
        pushSection(sections, isMaster ? QStringLiteral("Timing") : QStringLiteral("Bus Timing"), std::move(timing));

        if (node.has_attributes()) {
            const auto& attrs = node.attributes();
            QList<DetailField> attributes;
            addField(attributes, QStringLiteral("LIN Protocol"), text(attrs.lin_protocol()));
            addField(attributes, QStringLiteral("Configured NAD"), hexAndDecimal(attrs.configured_nad()));
            if (attrs.initial_nad() != 0) {
                addField(attributes, QStringLiteral("Initial NAD"), hexAndDecimal(attrs.initial_nad()));
            }
            addField(attributes, QStringLiteral("Product ID"), joinNumbers(attrs.product_id()));
            addField(attributes, QStringLiteral("Response Error"), text(attrs.response_error()));
            addField(attributes, QStringLiteral("Fault State Signals"), joinStrings(attrs.fault_state_signals()));
            addOptionalNumberField(attributes, QStringLiteral("P2 Min (ms)"), attrs.p2_min_ms());
            addOptionalNumberField(attributes, QStringLiteral("ST Min (ms)"), attrs.st_min_ms());
            addOptionalNumberField(attributes, QStringLiteral("N_As Timeout (ms)"), attrs.n_as_timeout_ms());
            addOptionalNumberField(attributes, QStringLiteral("N_Cr Timeout (ms)"), attrs.n_cr_timeout_ms());
            addOptionalNumberField(attributes, QStringLiteral("Response Tolerance (%)"), attrs.response_tolerance_pct());
            addOptionalNumberField(attributes, QStringLiteral("Wakeup Time (ms)"), attrs.wakeup_time_ms());
            addOptionalNumberField(attributes, QStringLiteral("Power On Time (ms)"), attrs.poweron_time_ms());
            pushSection(sections, QStringLiteral("Attributes"), std::move(attributes));
        }

        appendNodeCrossReferences(sections, node.name());
        return sections;
    }

    QList<DetailSection> frameDetails(const LdfPath& path) const {
        QList<DetailSection> sections;
        if (path.primaryIndex < 0 || path.primaryIndex >= document_.frames_size()) {
            return sections;
        }

        const auto& frame = document_.frames(path.primaryIndex);
        pushSection(sections,
                    QStringLiteral("Identity"),
                    {
                        DetailField{QStringLiteral("Name"), text(frame.name())},
                        DetailField{QStringLiteral("Frame ID"), hexAndDecimal(frame.id())},
                        DetailField{QStringLiteral("Publisher"), text(frame.publisher())},
                        DetailField{QStringLiteral("Length"), frame.length() == 0 ? QStringLiteral("Unspecified") : QStringLiteral("%1 bytes").arg(frame.length())},
                        DetailField{QStringLiteral("Signals"), numberText(frame.signals_size())},
                    });

        QList<DetailField> signals;
        for (const auto& signal : frame.signals()) {
            addField(signals, text(signal.signal_name()), QStringLiteral("Start bit %1").arg(signal.start_bit()));
        }
        pushSection(sections, QStringLiteral("Signals"), std::move(signals));
        return sections;
    }

    QList<DetailSection> frameSignalDetails(const LdfPath& path) const {
        QList<DetailSection> sections;
        if (path.primaryIndex < 0 || path.primaryIndex >= document_.frames_size()) {
            return sections;
        }

        const auto& frame = document_.frames(path.primaryIndex);
        if (path.secondaryIndex < 0 || path.secondaryIndex >= frame.signals_size()) {
            return sections;
        }

        const auto& frameSignal = frame.signals(path.secondaryIndex);
        QList<DetailField> placement;
        addField(placement, QStringLiteral("Signal"), text(frameSignal.signal_name()));
        addField(placement, QStringLiteral("Frame"), text(frame.name()));
        addField(placement, QStringLiteral("Frame ID"), hexAndDecimal(frame.id()));
        addNumberField(placement, QStringLiteral("Start Bit"), frameSignal.start_bit());
        pushSection(sections, QStringLiteral("Placement"), std::move(placement));

        if (const ldf::Signal* signal = findSignal(frameSignal.signal_name())) {
            QList<DetailField> signalFields;
            addNumberField(signalFields, QStringLiteral("Bit Length"), signal->bit_length());
            addNumberField(signalFields, QStringLiteral("Init Value"), signal->init_value());
            addField(signalFields, QStringLiteral("Publisher"), text(signal->publisher()));
            addField(signalFields, QStringLiteral("Subscribers"), joinStrings(signal->subscribers()));
            if (signal->has_encoding()) {
                addField(signalFields, QStringLiteral("Encoding"), text(signal->encoding().encoding_name()));
            }
            pushSection(sections, QStringLiteral("Signal"), std::move(signalFields));
        }

        return sections;
    }

    QList<DetailSection> signalDetails(const LdfPath& path) const {
        QList<DetailSection> sections;
        if (path.primaryIndex < 0 || path.primaryIndex >= document_.signals_size()) {
            return sections;
        }

        const auto& signal = document_.signals(path.primaryIndex);
        pushSection(sections,
                    QStringLiteral("Identity"),
                    {
                        DetailField{QStringLiteral("Name"), text(signal.name())},
                        DetailField{QStringLiteral("Bit Length"), numberText(signal.bit_length())},
                        DetailField{QStringLiteral("Init Value"), numberText(signal.init_value())},
                        DetailField{QStringLiteral("Publisher"), text(signal.publisher())},
                        DetailField{QStringLiteral("Subscribers"), joinStrings(signal.subscribers())},
                    });

        QList<DetailField> mapping;
        addField(mapping, QStringLiteral("Frame"), text(signal.frame_name()));
        if (signal.frame_id() != 0 || !signal.frame_name().empty()) {
            addField(mapping, QStringLiteral("Frame ID"), hexAndDecimal(signal.frame_id()));
            addNumberField(mapping, QStringLiteral("Start Bit"), signal.start_bit());
        }
        if (signal.has_encoding()) {
            addField(mapping, QStringLiteral("Encoding"), text(signal.encoding().encoding_name()));
            addNumberField(mapping, QStringLiteral("Physical Values"), signal.encoding().physical_values_size());
            addNumberField(mapping, QStringLiteral("Logical Values"), signal.encoding().logical_values_size());
        }
        pushSection(sections, QStringLiteral("Mapping"), std::move(mapping));
        return sections;
    }

    QList<DetailSection> encodingDetails(const LdfPath& path) const {
        QList<DetailSection> sections;
        if (path.primaryIndex < 0 || path.primaryIndex >= document_.signal_encoding_types_size()) {
            return sections;
        }

        const auto& encoding = document_.signal_encoding_types(path.primaryIndex);
        pushSection(sections,
                    QStringLiteral("Encoding"),
                    {
                        DetailField{QStringLiteral("Name"), text(encoding.name())},
                        DetailField{QStringLiteral("Physical Values"), numberText(encoding.physical_values_size())},
                        DetailField{QStringLiteral("Logical Values"), numberText(encoding.logical_values_size())},
                    });

        appendEncodingCrossReferences(sections, encoding.name());
        return sections;
    }

    QList<DetailSection> scheduleDetails(const LdfPath& path) const {
        QList<DetailSection> sections;
        if (path.primaryIndex < 0 || path.primaryIndex >= document_.schedule_tables_size()) {
            return sections;
        }

        const auto& schedule = document_.schedule_tables(path.primaryIndex);
        pushSection(sections,
                    QStringLiteral("Schedule"),
                    {
                        DetailField{QStringLiteral("Name"), text(schedule.name())},
                        DetailField{QStringLiteral("Entries"), numberText(schedule.entries_size())},
                    });

        QList<DetailField> entries;
        for (const auto& entry : schedule.entries()) {
            addField(entries, text(entry.command()), QStringLiteral("%1 ms").arg(numberText(entry.delay_ms())));
        }
        pushSection(sections, QStringLiteral("Entries"), std::move(entries));
        return sections;
    }

    QList<DetailSection> eventFrameDetails(const LdfPath& path) const {
        QList<DetailSection> sections;
        if (path.primaryIndex < 0 || path.primaryIndex >= document_.event_triggered_frames_size()) {
            return sections;
        }

        const auto& frame = document_.event_triggered_frames(path.primaryIndex);
        pushSection(sections,
                    QStringLiteral("Event Frame"),
                    {
                        DetailField{QStringLiteral("Name"), text(frame.name())},
                        DetailField{QStringLiteral("Frame ID"), hexAndDecimal(frame.id())},
                        DetailField{QStringLiteral("Collision Resolver"), text(frame.collision_resolver())},
                        DetailField{QStringLiteral("Associated Frames"), joinStrings(frame.associated_frames())},
                    });
        return sections;
    }

    QList<DetailSection> diagnosticAddressDetails(const LdfPath& path) const {
        QList<DetailSection> sections;
        if (path.primaryIndex < 0 || path.primaryIndex >= document_.diagnostic_addresses_size()) {
            return sections;
        }

        const auto& address = document_.diagnostic_addresses(path.primaryIndex);
        pushSection(sections,
                    QStringLiteral("Diagnostic Address"),
                    {
                        DetailField{QStringLiteral("Node"), text(address.node_name())},
                        DetailField{QStringLiteral("NAD"), hexAndDecimal(address.nad())},
                    });
        return sections;
    }

    QList<DetailSection> signalGroupDetails(const LdfPath& path) const {
        QList<DetailSection> sections;
        if (path.primaryIndex < 0 || path.primaryIndex >= document_.signal_groups_size()) {
            return sections;
        }

        const auto& group = document_.signal_groups(path.primaryIndex);
        pushSection(sections,
                    QStringLiteral("Signal Group"),
                    {
                        DetailField{QStringLiteral("Name"), text(group.name())},
                        DetailField{QStringLiteral("Group Size"), QStringLiteral("%1 bits").arg(group.group_size())},
                        DetailField{QStringLiteral("Signals"), numberText(group.signals_size())},
                    });
        return sections;
    }

    QList<DetailSection> diagnosticSignalDetails(const LdfPath& path) const {
        QList<DetailSection> sections;
        if (path.primaryIndex < 0 || path.primaryIndex >= document_.diagnostic_signals_size()) {
            return sections;
        }

        const auto& signal = document_.diagnostic_signals(path.primaryIndex);
        pushSection(sections,
                    QStringLiteral("Diagnostic Signal"),
                    {
                        DetailField{QStringLiteral("Name"), text(signal.name())},
                        DetailField{QStringLiteral("Bit Length"), numberText(signal.bit_length())},
                        DetailField{QStringLiteral("Init Value"), numberText(signal.init_value())},
                    });
        return sections;
    }

    QList<DetailSection> diagnosticFrameDetails(const LdfPath& path) const {
        QList<DetailSection> sections;
        if (path.primaryIndex < 0 || path.primaryIndex >= document_.diagnostic_frames_size()) {
            return sections;
        }

        const auto& frame = document_.diagnostic_frames(path.primaryIndex);
        pushSection(sections,
                    QStringLiteral("Diagnostic Frame"),
                    {
                        DetailField{QStringLiteral("Name"), text(frame.name())},
                        DetailField{QStringLiteral("Frame ID"), hexAndDecimal(frame.id())},
                        DetailField{QStringLiteral("Signals"), numberText(frame.signals_size())},
                    });
        return sections;
    }

    void appendEncodingCrossReferences(QList<DetailSection>& sections, const std::string& encodingName) const {
        QStringList signalRefs;
        for (int i = 0; i < document_.signals_size(); ++i) {
            const auto& signal = document_.signals(i);
            if (signal.has_encoding() && signal.encoding().encoding_name() == encodingName) {
                signalRefs.push_back(text(signal.name()));
            }
        }

        QList<DetailField> fields;
        if (!signalRefs.isEmpty()) {
            addField(fields,
                     QStringLiteral("Signals (%1)").arg(signalRefs.size()),
                     signalRefs.join(QStringLiteral(", ")));
        }
        pushSection(sections, QStringLiteral("Used By"), std::move(fields));
    }

    void appendNodeCrossReferences(QList<DetailSection>& sections, const std::string& nodeName) const {
        QStringList publishedFrames;
        QStringList publishedSignals;
        QStringList subscribedSignals;

        for (int i = 0; i < document_.frames_size(); ++i) {
            const auto& frame = document_.frames(i);
            if (frame.publisher() == nodeName) {
                publishedFrames.push_back(text(frame.name()));
            }
        }

        for (int i = 0; i < document_.signals_size(); ++i) {
            const auto& signal = document_.signals(i);
            if (signal.publisher() == nodeName) {
                publishedSignals.push_back(text(signal.name()));
            }
            for (const auto& sub : signal.subscribers()) {
                if (sub == nodeName) {
                    subscribedSignals.push_back(text(signal.name()));
                    break;
                }
            }
        }

        QList<DetailField> fields;
        if (!publishedFrames.isEmpty()) {
            addField(fields,
                     QStringLiteral("Publishes Frames (%1)").arg(publishedFrames.size()),
                     publishedFrames.join(QStringLiteral(", ")));
        }
        if (!publishedSignals.isEmpty()) {
            addField(fields,
                     QStringLiteral("Publishes Signals (%1)").arg(publishedSignals.size()),
                     publishedSignals.join(QStringLiteral(", ")));
        }
        if (!subscribedSignals.isEmpty()) {
            addField(fields,
                     QStringLiteral("Subscribes Signals (%1)").arg(subscribedSignals.size()),
                     subscribedSignals.join(QStringLiteral(", ")));
        }
        pushSection(sections, QStringLiteral("Referenced By"), std::move(fields));
    }

    const ldf::LdfFile& document_;
};

} // namespace

LdfDocumentSession::LdfDocumentSession(QString displayName,
                                       QString sourcePath,
                                       ldf::LdfFile document,
                                       QList<DiagnosticMessage> diagnostics)
    : AdapterSessionBase(FormatId::LDF,
                         QStringLiteral("LDF"),
                         std::move(displayName),
                         std::move(sourcePath),
                         std::move(diagnostics)),
      document_(std::move(document)) {
    setDetailPresenter(std::make_unique<LdfDetailPresenter>(document_));
    buildTree();
    buildSignalMap();
}

LdfDocumentSession::~LdfDocumentSession() = default;

QUrl LdfDocumentSession::centerPanelSource() const {
    if (signalMapModel_ && signalMapModel_->messageCount() > 0) {
        return QUrl(QStringLiteral("qrc:/qt/qml/ExplorerApp/qml/components/SignalMapView.qml"));
    }
    return {};
}

QAbstractListModel* LdfDocumentSession::centerPanelModel() {
    return signalMapModel_.get();
}

void LdfDocumentSession::moveModelsToThread(QThread* thread) {
    AdapterSessionBase::moveModelsToThread(thread);
    if (signalMapModel_)
        signalMapModel_->moveToThread(thread);
}

void LdfDocumentSession::buildTree() {
    auto root = std::make_unique<TreeItem>();
    root->title = displayName();
    root->semanticKind = SemanticKind::Root;

    appendNode(root.get(),
               QStringLiteral("Overview"),
               QStringLiteral("LIN %1  %2 kbps")
                   .arg(text(document_.lin_protocol_version()))
                   .arg(numberText(document_.lin_speed_kbps())),
               QStringLiteral("overview"),
               SemanticKind::Entity,
               NodeBinding{SemanticKind::Entity, LdfPath{LdfEntityKind::Overview, 0, -1, -1}, true});

    if (document_.has_master() || document_.slaves_size() > 0) {
        TreeItem* nodes = appendNode(root.get(),
                                     QStringLiteral("Nodes"),
                                     QString::number((document_.has_master() ? 1 : 0) + document_.slaves_size()),
                                     QStringLiteral("nodes"),
                                     SemanticKind::Section);

        if (document_.has_master()) {
            appendNode(nodes,
                       text(document_.master().name()),
                       QStringLiteral("master"),
                       QStringLiteral("master"),
                       SemanticKind::Entity,
                       NodeBinding{SemanticKind::Entity, LdfPath{LdfEntityKind::MasterNode, 0, -1, -1}, true});
        }

        for (int i = 0; i < document_.slaves_size(); ++i) {
            const auto& slave = document_.slaves(i);
            const QString subtitle = slave.has_attributes()
                ? hexValue(slave.attributes().configured_nad())
                : QStringLiteral("slave");
            appendNode(nodes,
                       text(slave.name()),
                       subtitle,
                       QStringLiteral("slave"),
                       SemanticKind::Entity,
                       NodeBinding{SemanticKind::Entity, LdfPath{LdfEntityKind::SlaveNode, i, -1, -1}, true});
        }
    }

    if (document_.signals_size() > 0) {
        TreeItem* signals = appendNode(root.get(), QStringLiteral("Signals"), QString::number(document_.signals_size()), QStringLiteral("signals"), SemanticKind::Section);
        for (int i = 0; i < document_.signals_size(); ++i) {
            const auto& signal = document_.signals(i);
            appendNode(signals,
                       text(signal.name()),
                       QStringLiteral("%1 bits").arg(signal.bit_length()),
                       QStringLiteral("signal"),
                       SemanticKind::Entity,
                       NodeBinding{SemanticKind::Entity, LdfPath{LdfEntityKind::Signal, i, -1, -1}, true});
        }
    }

    if (document_.frames_size() > 0) {
        TreeItem* frames = appendNode(root.get(), QStringLiteral("Frames"), QString::number(document_.frames_size()), QStringLiteral("frames"), SemanticKind::Section);
        for (int i = 0; i < document_.frames_size(); ++i) {
            const auto& frame = document_.frames(i);
            TreeItem* frameItem = appendNode(frames,
                                             text(frame.name()),
                                             QStringLiteral("%1  %2B").arg(hexValue(frame.id())).arg(frame.length()),
                                             QStringLiteral("frame"),
                                             SemanticKind::Entity,
                                             NodeBinding{SemanticKind::Entity, LdfPath{LdfEntityKind::Frame, i, -1, -1}, true});
            treeNodeKeys_[{static_cast<int>(LdfEntityKind::Frame), i, -1}] = frameItem->nodeKey;

            for (int j = 0; j < frame.signals_size(); ++j) {
                const auto& signal = frame.signals(j);
                TreeItem* sigItem = appendNode(frameItem,
                           text(signal.signal_name()),
                           QStringLiteral("@%1").arg(signal.start_bit()),
                           QStringLiteral("framesignal"),
                           SemanticKind::Entity,
                           NodeBinding{SemanticKind::Entity, LdfPath{LdfEntityKind::FrameSignal, i, j, -1}, true});
                treeNodeKeys_[{static_cast<int>(LdfEntityKind::FrameSignal), i, j}] = sigItem->nodeKey;
            }
        }
    }

    if (document_.signal_encoding_types_size() > 0) {
        TreeItem* encodings = appendNode(root.get(), QStringLiteral("Signal Encodings"), QString::number(document_.signal_encoding_types_size()), QStringLiteral("encodings"), SemanticKind::Section);
        for (int i = 0; i < document_.signal_encoding_types_size(); ++i) {
            const auto& encoding = document_.signal_encoding_types(i);
            appendNode(encodings,
                       text(encoding.name()),
                       QStringLiteral("P%1 / L%2").arg(encoding.physical_values_size()).arg(encoding.logical_values_size()),
                       QStringLiteral("encoding"),
                       SemanticKind::Entity,
                       NodeBinding{SemanticKind::Entity, LdfPath{LdfEntityKind::Encoding, i, -1, -1}, true});
        }
    }

    if (document_.schedule_tables_size() > 0) {
        TreeItem* schedules = appendNode(root.get(), QStringLiteral("Schedule Tables"), QString::number(document_.schedule_tables_size()), QStringLiteral("schedules"), SemanticKind::Section);
        for (int i = 0; i < document_.schedule_tables_size(); ++i) {
            const auto& schedule = document_.schedule_tables(i);
            TreeItem* scheduleItem = appendNode(schedules,
                                                text(schedule.name()),
                                                QStringLiteral("%1 entries").arg(schedule.entries_size()),
                                                QStringLiteral("schedule"),
                                                SemanticKind::Entity,
                                                NodeBinding{SemanticKind::Entity, LdfPath{LdfEntityKind::ScheduleTable, i, -1, -1}, true});

            for (const auto& entry : schedule.entries()) {
                appendNode(scheduleItem,
                           text(entry.command()),
                           QStringLiteral("%1 ms").arg(numberText(entry.delay_ms())),
                           QStringLiteral("scheduleentry"),
                           SemanticKind::Attribute);
            }
        }
    }

    if (document_.event_triggered_frames_size() > 0) {
        TreeItem* events = appendNode(root.get(), QStringLiteral("Event Triggered Frames"), QString::number(document_.event_triggered_frames_size()), QStringLiteral("events"), SemanticKind::Section);
        for (int i = 0; i < document_.event_triggered_frames_size(); ++i) {
            const auto& frame = document_.event_triggered_frames(i);
            appendNode(events,
                       text(frame.name()),
                       hexValue(frame.id()),
                       QStringLiteral("eventframe"),
                       SemanticKind::Entity,
                       NodeBinding{SemanticKind::Entity, LdfPath{LdfEntityKind::EventFrame, i, -1, -1}, true});
        }
    }

    if (document_.signal_groups_size() > 0) {
        TreeItem* groups = appendNode(root.get(), QStringLiteral("Signal Groups"), QString::number(document_.signal_groups_size()), QStringLiteral("groups"), SemanticKind::Section);
        for (int i = 0; i < document_.signal_groups_size(); ++i) {
            const auto& group = document_.signal_groups(i);
            appendNode(groups,
                       text(group.name()),
                       QStringLiteral("%1 bits").arg(group.group_size()),
                       QStringLiteral("group"),
                       SemanticKind::Entity,
                       NodeBinding{SemanticKind::Entity, LdfPath{LdfEntityKind::SignalGroup, i, -1, -1}, true});
        }
    }

    if (document_.diagnostic_addresses_size() > 0 ||
        document_.diagnostic_signals_size() > 0 ||
        document_.diagnostic_frames_size() > 0) {
        TreeItem* diagnostics = appendNode(root.get(), QStringLiteral("Diagnostics"), QString(), QStringLiteral("diagnostics"), SemanticKind::Section);

        if (document_.diagnostic_addresses_size() > 0) {
            TreeItem* addresses = appendNode(diagnostics, QStringLiteral("Addresses"), QString::number(document_.diagnostic_addresses_size()), QStringLiteral("diag-addresses"), SemanticKind::Section);
            for (int i = 0; i < document_.diagnostic_addresses_size(); ++i) {
                const auto& address = document_.diagnostic_addresses(i);
                appendNode(addresses,
                           text(address.node_name()),
                           hexValue(address.nad()),
                           QStringLiteral("diag-address"),
                           SemanticKind::Diagnostic,
                           NodeBinding{SemanticKind::Diagnostic, LdfPath{LdfEntityKind::DiagnosticAddress, i, -1, -1}, true});
            }
        }

        if (document_.diagnostic_signals_size() > 0) {
            TreeItem* signals = appendNode(diagnostics, QStringLiteral("Signals"), QString::number(document_.diagnostic_signals_size()), QStringLiteral("diag-signals"), SemanticKind::Section);
            for (int i = 0; i < document_.diagnostic_signals_size(); ++i) {
                const auto& signal = document_.diagnostic_signals(i);
                appendNode(signals,
                           text(signal.name()),
                           QStringLiteral("%1 bits").arg(signal.bit_length()),
                           QStringLiteral("diag-signal"),
                           SemanticKind::Diagnostic,
                           NodeBinding{SemanticKind::Diagnostic, LdfPath{LdfEntityKind::DiagnosticSignal, i, -1, -1}, true});
            }
        }

        if (document_.diagnostic_frames_size() > 0) {
            TreeItem* frames = appendNode(diagnostics, QStringLiteral("Frames"), QString::number(document_.diagnostic_frames_size()), QStringLiteral("diag-frames"), SemanticKind::Section);
            for (int i = 0; i < document_.diagnostic_frames_size(); ++i) {
                const auto& frame = document_.diagnostic_frames(i);
                appendNode(frames,
                           text(frame.name()),
                           hexValue(frame.id()),
                           QStringLiteral("diag-frame"),
                           SemanticKind::Diagnostic,
                           NodeBinding{SemanticKind::Diagnostic, LdfPath{LdfEntityKind::DiagnosticFrame, i, -1, -1}, true});
            }
        }
    }

    setRootItem(std::move(root));
}

void LdfDocumentSession::buildSignalMap() {
    signalMapModel_ = std::make_unique<SignalMapModel>();

    // Build signal lookup by name for enrichment.
    std::unordered_map<std::string, const ldf::Signal*> signalLookup;
    for (int i = 0; i < document_.signals_size(); ++i) {
        signalLookup[document_.signals(i).name()] = &document_.signals(i);
    }

    for (int i = 0; i < document_.frames_size(); ++i) {
        const auto& frame = document_.frames(i);

        MessageEntry entry;
        entry.name = text(frame.name());
        entry.id = frame.id();
        entry.dlc = static_cast<int>(frame.length());
        entry.isExtendedId = false;
        entry.sender = text(frame.publisher());

        auto keyIt = treeNodeKeys_.find({static_cast<int>(LdfEntityKind::Frame), i, -1});
        if (keyIt != treeNodeKeys_.end()) entry.nodeKey = keyIt->second;

        for (int j = 0; j < frame.signals_size(); ++j) {
            const auto& frameSig = frame.signals(j);

            SignalEntry sig;
            sig.name = text(frameSig.signal_name());
            sig.startBit = static_cast<int>(frameSig.start_bit());
            sig.bigEndian = false; // LIN is always LE
            sig.colorIndex = j % 8;

            // Enrich from top-level signal definition.
            auto it = signalLookup.find(frameSig.signal_name());
            if (it != signalLookup.end()) {
                const ldf::Signal* fullSig = it->second;
                sig.bitLength = static_cast<int>(fullSig->bit_length());
                sig.sender = text(fullSig->publisher());

                if (fullSig->has_encoding()) {
                    const auto& enc = fullSig->encoding();
                    if (enc.physical_values_size() > 0) {
                        const auto& pv = enc.physical_values(0);
                        sig.factor = pv.factor();
                        sig.offset = pv.offset();
                        sig.minimum = static_cast<double>(pv.min_raw());
                        sig.maximum = static_cast<double>(pv.max_raw());
                        sig.unit = text(pv.unit());
                    }
                }
            }

            // Fallback: if bitLength is still 0, set to 1.
            if (sig.bitLength == 0) sig.bitLength = 1;

            auto sigKeyIt = treeNodeKeys_.find({static_cast<int>(LdfEntityKind::FrameSignal), i, j});
            if (sigKeyIt != treeNodeKeys_.end()) sig.nodeKey = sigKeyIt->second;

            entry.signalEntries.push_back(std::move(sig));
        }

        // Derive length from signals when unspecified.
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

        signalMapModel_->addMessage(std::move(entry));
    }

    signalMapModel_->finalize();
}
