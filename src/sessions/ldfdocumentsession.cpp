#include "sessions/ldfdocumentsession.h"
#include "sessions/ldfdetailpresenter.h"
#include "models/signalmapmodel.h"

#include <QStringList>
#include <algorithm>
#include <unordered_map>

#undef signals  // ldf proto's repeated `signals` field vs Qt's `signals` keyword macro

namespace {

QString text(const std::string& value) {
    auto utf8 = QString::fromUtf8(value.data(), static_cast<int>(value.size()));
    if (utf8.contains(QChar::ReplacementCharacter)) {
        return QString::fromLatin1(value.data(), static_cast<int>(value.size()));
    }
    return utf8;
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
      _document(std::move(document)) {
    setDetailPresenter(std::make_unique<LdfDetailPresenter>(_document));
    buildTree();
    buildSignalMap();
}

LdfDocumentSession::~LdfDocumentSession() = default;

QUrl LdfDocumentSession::centerPanelSource() const {
    if (_signal_map_model && _signal_map_model->messageCount() > 0) {
        return QUrl(QStringLiteral("qrc:/qt/qml/ExplorerApp/qml/components/SignalMapView.qml"));
    }
    return {};
}

QAbstractListModel* LdfDocumentSession::centerPanelModel() {
    return _signal_map_model.get();
}

void LdfDocumentSession::moveModelsToThread(QThread* thread) {
    AdapterSessionBase::moveModelsToThread(thread);
    if (_signal_map_model)
        _signal_map_model->moveToThread(thread);
}

void LdfDocumentSession::buildTree() {
    auto root = std::make_unique<TreeItem>();
    root->title = displayName();
    root->semanticKind = SemanticKind::Root;

    appendNode(root.get(),
               QStringLiteral("Overview"),
               QStringLiteral("LIN %1  %2 kbps")
                   .arg(text(_document.lin_protocol_version()))
                   .arg(numberText(_document.lin_speed_kbps())),
               QStringLiteral("overview"),
               SemanticKind::Entity,
               NodeBinding{SemanticKind::Entity, LdfPath{LdfEntityKind::Overview, 0, -1, -1}, true});

    if (_document.has_master() || _document.slaves_size() > 0) {
        TreeItem* nodes = appendNode(root.get(),
                                     QStringLiteral("Nodes"),
                                     QString::number((_document.has_master() ? 1 : 0) + _document.slaves_size()),
                                     QStringLiteral("nodes"),
                                     SemanticKind::Section);

        if (_document.has_master()) {
            appendNode(nodes,
                       text(_document.master().name()),
                       QStringLiteral("master"),
                       QStringLiteral("master"),
                       SemanticKind::Entity,
                       NodeBinding{SemanticKind::Entity, LdfPath{LdfEntityKind::MasterNode, 0, -1, -1}, true});
        }

        for (int i = 0; i < _document.slaves_size(); ++i) {
            const auto& slave = _document.slaves(i);
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

    if (_document.signals_size() > 0) {
        TreeItem* signals = appendNode(root.get(), QStringLiteral("Signals"), QString::number(_document.signals_size()), QStringLiteral("signals"), SemanticKind::Section);
        for (int i = 0; i < _document.signals_size(); ++i) {
            const auto& signal = _document.signals(i);
            TreeItem* sigNode = appendNode(signals,
                       text(signal.name()),
                       QStringLiteral("%1 bits").arg(signal.bit_length()),
                       QStringLiteral("signal"),
                       SemanticKind::Entity,
                       NodeBinding{SemanticKind::Entity, LdfPath{LdfEntityKind::Signal, i, -1, -1}, true});
            if (sigNode->nodeKey) {
                _tree_node_keys[{static_cast<int>(LdfEntityKind::Signal), i, -1}] = sigNode->nodeKey;
            }
        }
    }

    if (_document.frames_size() > 0) {
        TreeItem* frames = appendNode(root.get(), QStringLiteral("Frames"), QString::number(_document.frames_size()), QStringLiteral("frames"), SemanticKind::Section);
        for (int i = 0; i < _document.frames_size(); ++i) {
            const auto& frame = _document.frames(i);
            TreeItem* frameItem = appendNode(frames,
                                             text(frame.name()),
                                             QStringLiteral("%1  %2B").arg(hexValue(frame.id())).arg(frame.length()),
                                             QStringLiteral("frame"),
                                             SemanticKind::Entity,
                                             NodeBinding{SemanticKind::Entity, LdfPath{LdfEntityKind::Frame, i, -1, -1}, true});
            _tree_node_keys[{static_cast<int>(LdfEntityKind::Frame), i, -1}] = frameItem->nodeKey;

            for (int j = 0; j < frame.signals_size(); ++j) {
                const auto& signal = frame.signals(j);
                TreeItem* sigItem = appendNode(frameItem,
                           text(signal.signal_name()),
                           QStringLiteral("@%1").arg(signal.start_bit()),
                           QStringLiteral("framesignal"),
                           SemanticKind::Entity,
                           NodeBinding{SemanticKind::Entity, LdfPath{LdfEntityKind::FrameSignal, i, j, -1}, true});
                _tree_node_keys[{static_cast<int>(LdfEntityKind::FrameSignal), i, j}] = sigItem->nodeKey;
            }
        }
    }

    if (_document.signal_encoding_types_size() > 0) {
        TreeItem* encodings = appendNode(root.get(), QStringLiteral("Signal Encodings"), QString::number(_document.signal_encoding_types_size()), QStringLiteral("encodings"), SemanticKind::Section);
        for (int i = 0; i < _document.signal_encoding_types_size(); ++i) {
            const auto& encoding = _document.signal_encoding_types(i);
            appendNode(encodings,
                       text(encoding.name()),
                       QStringLiteral("P%1 / L%2").arg(encoding.physical_values_size()).arg(encoding.logical_values_size()),
                       QStringLiteral("encoding"),
                       SemanticKind::Entity,
                       NodeBinding{SemanticKind::Entity, LdfPath{LdfEntityKind::Encoding, i, -1, -1}, true});
        }
    }

    if (_document.schedule_tables_size() > 0) {
        TreeItem* schedules = appendNode(root.get(), QStringLiteral("Schedule Tables"), QString::number(_document.schedule_tables_size()), QStringLiteral("schedules"), SemanticKind::Section);
        for (int i = 0; i < _document.schedule_tables_size(); ++i) {
            const auto& schedule = _document.schedule_tables(i);
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

    if (_document.event_triggered_frames_size() > 0) {
        TreeItem* events = appendNode(root.get(), QStringLiteral("Event Triggered Frames"), QString::number(_document.event_triggered_frames_size()), QStringLiteral("events"), SemanticKind::Section);
        for (int i = 0; i < _document.event_triggered_frames_size(); ++i) {
            const auto& frame = _document.event_triggered_frames(i);
            appendNode(events,
                       text(frame.name()),
                       hexValue(frame.id()),
                       QStringLiteral("eventframe"),
                       SemanticKind::Entity,
                       NodeBinding{SemanticKind::Entity, LdfPath{LdfEntityKind::EventFrame, i, -1, -1}, true});
        }
    }

    if (_document.signal_groups_size() > 0) {
        TreeItem* groups = appendNode(root.get(), QStringLiteral("Signal Groups"), QString::number(_document.signal_groups_size()), QStringLiteral("groups"), SemanticKind::Section);
        for (int i = 0; i < _document.signal_groups_size(); ++i) {
            const auto& group = _document.signal_groups(i);
            appendNode(groups,
                       text(group.name()),
                       QStringLiteral("%1 bits").arg(group.group_size()),
                       QStringLiteral("group"),
                       SemanticKind::Entity,
                       NodeBinding{SemanticKind::Entity, LdfPath{LdfEntityKind::SignalGroup, i, -1, -1}, true});
        }
    }

    if (_document.diagnostic_addresses_size() > 0 ||
        _document.diagnostic_signals_size() > 0 ||
        _document.diagnostic_frames_size() > 0) {
        TreeItem* diagnostics = appendNode(root.get(), QStringLiteral("Diagnostics"), QString(), QStringLiteral("diagnostics"), SemanticKind::Section);

        if (_document.diagnostic_addresses_size() > 0) {
            TreeItem* addresses = appendNode(diagnostics, QStringLiteral("Addresses"), QString::number(_document.diagnostic_addresses_size()), QStringLiteral("diag-addresses"), SemanticKind::Section);
            for (int i = 0; i < _document.diagnostic_addresses_size(); ++i) {
                const auto& address = _document.diagnostic_addresses(i);
                appendNode(addresses,
                           text(address.node_name()),
                           hexValue(address.nad()),
                           QStringLiteral("diag-address"),
                           SemanticKind::Diagnostic,
                           NodeBinding{SemanticKind::Diagnostic, LdfPath{LdfEntityKind::DiagnosticAddress, i, -1, -1}, true});
            }
        }

        if (_document.diagnostic_signals_size() > 0) {
            TreeItem* signals = appendNode(diagnostics, QStringLiteral("Signals"), QString::number(_document.diagnostic_signals_size()), QStringLiteral("diag-signals"), SemanticKind::Section);
            for (int i = 0; i < _document.diagnostic_signals_size(); ++i) {
                const auto& signal = _document.diagnostic_signals(i);
                appendNode(signals,
                           text(signal.name()),
                           QStringLiteral("%1 bits").arg(signal.bit_length()),
                           QStringLiteral("diag-signal"),
                           SemanticKind::Diagnostic,
                           NodeBinding{SemanticKind::Diagnostic, LdfPath{LdfEntityKind::DiagnosticSignal, i, -1, -1}, true});
            }
        }

        if (_document.diagnostic_frames_size() > 0) {
            TreeItem* frames = appendNode(diagnostics, QStringLiteral("Frames"), QString::number(_document.diagnostic_frames_size()), QStringLiteral("diag-frames"), SemanticKind::Section);
            for (int i = 0; i < _document.diagnostic_frames_size(); ++i) {
                const auto& frame = _document.diagnostic_frames(i);
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
    _signal_map_model = std::make_unique<SignalMapModel>();

    // Build signal lookup by name for enrichment.
    std::unordered_map<std::string, const ldf::Signal*> signalLookup;
    for (int i = 0; i < _document.signals_size(); ++i) {
        signalLookup[_document.signals(i).name()] = &_document.signals(i);
    }

    for (int i = 0; i < _document.frames_size(); ++i) {
        const auto& frame = _document.frames(i);

        MessageEntry entry;
        entry.name = text(frame.name());
        entry.id = frame.id();
        entry.dlc = static_cast<int>(frame.length());
        entry.isExtendedId = false;
        entry.sender = text(frame.publisher());

        auto keyIt = _tree_node_keys.find({static_cast<int>(LdfEntityKind::Frame), i, -1});
        if (keyIt != _tree_node_keys.end()) entry.nodeKey = keyIt->second;

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

            auto sigKeyIt = _tree_node_keys.find({static_cast<int>(LdfEntityKind::FrameSignal), i, j});
            if (sigKeyIt != _tree_node_keys.end()) sig.nodeKey = sigKeyIt->second;

            // Also store standalone Signal key so clicking a Signal in
            // the tree navigates to it in the signal map.
            for (int si = 0; si < _document.signals_size(); ++si) {
                if (_document.signals(si).name() == frameSig.signal_name()) {
                    auto standaloneSigKey = _tree_node_keys.find({static_cast<int>(LdfEntityKind::Signal), si, -1});
                    if (standaloneSigKey != _tree_node_keys.end()) sig.signalNodeKey = standaloneSigKey->second;
                    break;
                }
            }

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

        _signal_map_model->addMessage(std::move(entry));
    }

    _signal_map_model->finalize();
}
