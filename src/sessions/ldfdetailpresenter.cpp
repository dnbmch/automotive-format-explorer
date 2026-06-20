#include "sessions/ldfdetailpresenter.h"

#include <QStringList>

#include <google/protobuf/util/json_util.h>

#undef signals  // ldf proto's repeated `signals` field vs Qt's `signals` keyword macro

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

} // namespace

QList<DetailSection> LdfDetailPresenter::buildDetails(const NodeBinding& binding) const {
    if (!std::holds_alternative<LdfPath>(binding.payload)) {
        return {};
    }

    const LdfPath path = std::get<LdfPath>(binding.payload);
    switch (path.kind) {
    case LdfEntityKind::Overview:
        return overviewDetails();
    case LdfEntityKind::MasterNode:
        return nodeDetails(_document.master(), true);
    case LdfEntityKind::SlaveNode:
        return path.primaryIndex >= 0 && path.primaryIndex < _document.slaves_size()
            ? nodeDetails(_document.slaves(path.primaryIndex), false)
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

QString LdfDetailPresenter::buildRawJson(const NodeBinding& binding) const {
    if (!std::holds_alternative<LdfPath>(binding.payload)) {
        return {};
    }

    const LdfPath path = std::get<LdfPath>(binding.payload);
    const google::protobuf::Message* msg = nullptr;
    int i = path.primaryIndex;
    int j = path.secondaryIndex;

    switch (path.kind) {
    case LdfEntityKind::Overview:          break;
    case LdfEntityKind::MasterNode:        if (_document.has_master()) msg = &_document.master(); break;
    case LdfEntityKind::SlaveNode:         if (i >= 0 && i < _document.slaves_size()) msg = &_document.slaves(i); break;
    case LdfEntityKind::Frame:             if (i >= 0 && i < _document.frames_size()) msg = &_document.frames(i); break;
    case LdfEntityKind::FrameSignal:       if (i >= 0 && i < _document.frames_size() && j >= 0 && j < _document.frames(i).signals_size()) msg = &_document.frames(i).signals(j); break;
    case LdfEntityKind::Signal:            if (i >= 0 && i < _document.signals_size()) msg = &_document.signals(i); break;
    case LdfEntityKind::Encoding:          if (i >= 0 && i < _document.signal_encoding_types_size()) msg = &_document.signal_encoding_types(i); break;
    case LdfEntityKind::ScheduleTable:     if (i >= 0 && i < _document.schedule_tables_size()) msg = &_document.schedule_tables(i); break;
    case LdfEntityKind::EventFrame:        if (i >= 0 && i < _document.event_triggered_frames_size()) msg = &_document.event_triggered_frames(i); break;
    case LdfEntityKind::DiagnosticAddress: if (i >= 0 && i < _document.diagnostic_addresses_size()) msg = &_document.diagnostic_addresses(i); break;
    case LdfEntityKind::SignalGroup:       if (i >= 0 && i < _document.signal_groups_size()) msg = &_document.signal_groups(i); break;
    case LdfEntityKind::DiagnosticSignal:  if (i >= 0 && i < _document.diagnostic_signals_size()) msg = &_document.diagnostic_signals(i); break;
    case LdfEntityKind::DiagnosticFrame:   if (i >= 0 && i < _document.diagnostic_frames_size()) msg = &_document.diagnostic_frames(i); break;
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

const ldf::Signal* LdfDetailPresenter::findSignal(const std::string& name) const {
    for (int i = 0; i < _document.signals_size(); ++i) {
        const auto& signal = _document.signals(i);
        if (signal.name() == name) {
            return &signal;
        }
    }
    return nullptr;
}

QList<DetailSection> LdfDetailPresenter::overviewDetails() const {
    QList<DetailSection> sections;
    pushSection(sections,
                QStringLiteral("Header"),
                {
                    DetailField{QStringLiteral("LIN Protocol"), text(_document.lin_protocol_version())},
                    DetailField{QStringLiteral("Language Version"), text(_document.lin_language_version())},
                    DetailField{QStringLiteral("Speed (kbps)"), numberText(_document.lin_speed_kbps())},
                    DetailField{QStringLiteral("Channel"), text(_document.channel_name())},
                    DetailField{QStringLiteral("LDF Revision"), text(_document.ldf_file_revision())},
                    DetailField{QStringLiteral("Big Endian Signals"), boolText(_document.big_endian_signals())},
                });

    pushSection(sections,
                QStringLiteral("Contents"),
                {
                    DetailField{QStringLiteral("Master Node"), _document.has_master() ? text(_document.master().name()) : QStringLiteral("Not defined")},
                    DetailField{QStringLiteral("Slave Nodes"), numberText(_document.slaves_size())},
                    DetailField{QStringLiteral("Signals"), numberText(_document.signals_size())},
                    DetailField{QStringLiteral("Frames"), numberText(_document.frames_size())},
                    DetailField{QStringLiteral("Signal Encodings"), numberText(_document.signal_encoding_types_size())},
                    DetailField{QStringLiteral("Schedule Tables"), numberText(_document.schedule_tables_size())},
                    DetailField{QStringLiteral("Event Frames"), numberText(_document.event_triggered_frames_size())},
                    DetailField{QStringLiteral("Diagnostic Addresses"), numberText(_document.diagnostic_addresses_size())},
                    DetailField{QStringLiteral("Signal Groups"), numberText(_document.signal_groups_size())},
                    DetailField{QStringLiteral("Diagnostic Signals"), numberText(_document.diagnostic_signals_size())},
                    DetailField{QStringLiteral("Diagnostic Frames"), numberText(_document.diagnostic_frames_size())},
                });
    return sections;
}

QList<DetailSection> LdfDetailPresenter::nodeDetails(const ldf::Node& node, bool isMaster) const {
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

QList<DetailSection> LdfDetailPresenter::frameDetails(const LdfPath& path) const {
    QList<DetailSection> sections;
    if (path.primaryIndex < 0 || path.primaryIndex >= _document.frames_size()) {
        return sections;
    }

    const auto& frame = _document.frames(path.primaryIndex);
    pushSection(sections,
                QStringLiteral("Identity"),
                {
                    DetailField{QStringLiteral("Name"), text(frame.name())},
                    DetailField{QStringLiteral("Frame ID"), hexAndDecimal(frame.id())},
                    DetailField{QStringLiteral("Publisher"), text(frame.publisher())},
                    DetailField{QStringLiteral("Length"), frame.length() == 0 ? QStringLiteral("Unspecified") : QStringLiteral("%1 bytes").arg(frame.length())},
                    DetailField{QStringLiteral("Signals"), numberText(frame.signals_size())},
                });

    for (int s = 0; s < frame.signals_size(); ++s) {
        const auto& fs = frame.signals(s);
        QList<DetailField> sigFields;
        addNumberField(sigFields, QStringLiteral("Start Bit"), fs.start_bit());

        if (const ldf::Signal* sig = findSignal(fs.signal_name())) {
            addNumberField(sigFields, QStringLiteral("Bit Length"), sig->bit_length());
            addNumberField(sigFields, QStringLiteral("Init Value"), sig->init_value());
            addField(sigFields, QStringLiteral("Publisher"), text(sig->publisher()));
            if (sig->has_encoding()) {
                addField(sigFields, QStringLiteral("Encoding"), text(sig->encoding().encoding_name()));
                for (const auto& pv : sig->encoding().physical_values()) {
                    QString range = QStringLiteral("[%1..%2]").arg(pv.min_raw()).arg(pv.max_raw());
                    QString desc = QStringLiteral("f=%1  o=%2").arg(numberText(pv.factor()), numberText(pv.offset()));
                    if (!pv.unit().empty()) desc += QStringLiteral("  %1").arg(text(pv.unit()));
                    addField(sigFields, range, desc);
                }
                for (const auto& lv : sig->encoding().logical_values()) {
                    addField(sigFields, numberText(lv.value()), text(lv.description()));
                }
            }
            addField(sigFields, QStringLiteral("Subscribers"), joinStrings(sig->subscribers()));
        }
        pushSection(sections, text(fs.signal_name()), std::move(sigFields));
    }
    return sections;
}

QList<DetailSection> LdfDetailPresenter::frameSignalDetails(const LdfPath& path) const {
    QList<DetailSection> sections;
    if (path.primaryIndex < 0 || path.primaryIndex >= _document.frames_size()) {
        return sections;
    }

    const auto& frame = _document.frames(path.primaryIndex);
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

        if (signal->has_encoding()) {
            appendEncodingValues(sections, signal->encoding());
        }
    }

    return sections;
}

QList<DetailSection> LdfDetailPresenter::signalDetails(const LdfPath& path) const {
    QList<DetailSection> sections;
    if (path.primaryIndex < 0 || path.primaryIndex >= _document.signals_size()) {
        return sections;
    }

    const auto& signal = _document.signals(path.primaryIndex);
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
    }
    pushSection(sections, QStringLiteral("Mapping"), std::move(mapping));

    if (signal.has_encoding()) {
        appendEncodingValues(sections, signal.encoding());
    }
    return sections;
}

QList<DetailSection> LdfDetailPresenter::encodingDetails(const LdfPath& path) const {
    QList<DetailSection> sections;
    if (path.primaryIndex < 0 || path.primaryIndex >= _document.signal_encoding_types_size()) {
        return sections;
    }

    const auto& encoding = _document.signal_encoding_types(path.primaryIndex);
    pushSection(sections,
                QStringLiteral("Encoding"),
                {
                    DetailField{QStringLiteral("Name"), text(encoding.name())},
                });

    // Reuse the shared helper for physical/logical values.
    ldf::SignalEncoding asEncoding;
    asEncoding.set_encoding_name(encoding.name());
    for (const auto& pv : encoding.physical_values()) {
        *asEncoding.add_physical_values() = pv;
    }
    for (const auto& lv : encoding.logical_values()) {
        *asEncoding.add_logical_values() = lv;
    }
    appendEncodingValues(sections, asEncoding);

    appendEncodingCrossReferences(sections, encoding.name());
    return sections;
}

QList<DetailSection> LdfDetailPresenter::scheduleDetails(const LdfPath& path) const {
    QList<DetailSection> sections;
    if (path.primaryIndex < 0 || path.primaryIndex >= _document.schedule_tables_size()) {
        return sections;
    }

    const auto& schedule = _document.schedule_tables(path.primaryIndex);
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

QList<DetailSection> LdfDetailPresenter::eventFrameDetails(const LdfPath& path) const {
    QList<DetailSection> sections;
    if (path.primaryIndex < 0 || path.primaryIndex >= _document.event_triggered_frames_size()) {
        return sections;
    }

    const auto& frame = _document.event_triggered_frames(path.primaryIndex);
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

QList<DetailSection> LdfDetailPresenter::diagnosticAddressDetails(const LdfPath& path) const {
    QList<DetailSection> sections;
    if (path.primaryIndex < 0 || path.primaryIndex >= _document.diagnostic_addresses_size()) {
        return sections;
    }

    const auto& address = _document.diagnostic_addresses(path.primaryIndex);
    pushSection(sections,
                QStringLiteral("Diagnostic Address"),
                {
                    DetailField{QStringLiteral("Node"), text(address.node_name())},
                    DetailField{QStringLiteral("NAD"), hexAndDecimal(address.nad())},
                });
    return sections;
}

QList<DetailSection> LdfDetailPresenter::signalGroupDetails(const LdfPath& path) const {
    QList<DetailSection> sections;
    if (path.primaryIndex < 0 || path.primaryIndex >= _document.signal_groups_size()) {
        return sections;
    }

    const auto& group = _document.signal_groups(path.primaryIndex);
    pushSection(sections,
                QStringLiteral("Signal Group"),
                {
                    DetailField{QStringLiteral("Name"), text(group.name())},
                    DetailField{QStringLiteral("Group Size"), QStringLiteral("%1 bits").arg(group.group_size())},
                    DetailField{QStringLiteral("Signals"), numberText(group.signals_size())},
                });
    return sections;
}

QList<DetailSection> LdfDetailPresenter::diagnosticSignalDetails(const LdfPath& path) const {
    QList<DetailSection> sections;
    if (path.primaryIndex < 0 || path.primaryIndex >= _document.diagnostic_signals_size()) {
        return sections;
    }

    const auto& signal = _document.diagnostic_signals(path.primaryIndex);
    pushSection(sections,
                QStringLiteral("Diagnostic Signal"),
                {
                    DetailField{QStringLiteral("Name"), text(signal.name())},
                    DetailField{QStringLiteral("Bit Length"), numberText(signal.bit_length())},
                    DetailField{QStringLiteral("Init Value"), numberText(signal.init_value())},
                });
    return sections;
}

QList<DetailSection> LdfDetailPresenter::diagnosticFrameDetails(const LdfPath& path) const {
    QList<DetailSection> sections;
    if (path.primaryIndex < 0 || path.primaryIndex >= _document.diagnostic_frames_size()) {
        return sections;
    }

    const auto& frame = _document.diagnostic_frames(path.primaryIndex);
    pushSection(sections,
                QStringLiteral("Diagnostic Frame"),
                {
                    DetailField{QStringLiteral("Name"), text(frame.name())},
                    DetailField{QStringLiteral("Frame ID"), hexAndDecimal(frame.id())},
                    DetailField{QStringLiteral("Signals"), numberText(frame.signals_size())},
                });
    return sections;
}

void LdfDetailPresenter::appendEncodingValues(QList<DetailSection>& sections, const ldf::SignalEncoding& enc) const {
    if (enc.physical_values_size() > 0) {
        QList<DetailField> fields;
        for (const auto& pv : enc.physical_values()) {
            QString range = QStringLiteral("[%1 .. %2]").arg(pv.min_raw()).arg(pv.max_raw());
            QString desc = QStringLiteral("factor=%1  offset=%2")
                .arg(numberText(pv.factor()), numberText(pv.offset()));
            if (!pv.unit().empty()) {
                desc += QStringLiteral("  unit=%1").arg(text(pv.unit()));
            }
            fields.push_back(DetailField{range, desc});
        }
        pushSection(sections, QStringLiteral("Physical Values"), std::move(fields));
    }

    if (enc.logical_values_size() > 0) {
        QList<DetailField> fields;
        for (const auto& lv : enc.logical_values()) {
            fields.push_back(DetailField{
                numberText(lv.value()),
                text(lv.description()),
            });
        }
        pushSection(sections, QStringLiteral("Logical Values"), std::move(fields));
    }
}

void LdfDetailPresenter::appendEncodingCrossReferences(QList<DetailSection>& sections, const std::string& encodingName) const {
    QStringList signalRefs;
    for (int i = 0; i < _document.signals_size(); ++i) {
        const auto& signal = _document.signals(i);
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

void LdfDetailPresenter::appendNodeCrossReferences(QList<DetailSection>& sections, const std::string& nodeName) const {
    QStringList publishedFrames;
    QStringList publishedSignals;
    QStringList subscribedSignals;

    for (int i = 0; i < _document.frames_size(); ++i) {
        const auto& frame = _document.frames(i);
        if (frame.publisher() == nodeName) {
            publishedFrames.push_back(text(frame.name()));
        }
    }

    for (int i = 0; i < _document.signals_size(); ++i) {
        const auto& signal = _document.signals(i);
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
