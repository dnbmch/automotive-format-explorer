#pragma once

#include "core/formatid.h"
#include "core/treeitem.h"

#include <QHash>

#include <variant>

enum class A2lEntityKind {
    Module,
    Measurement,
    Characteristic,
    AxisPts,
    CompuMethod,
    RecordLayout,
    Unit,
    Function,
    Group,
    XcpSummary,
    CcpSummary,
    TypedefItem,
    Instance,
    VariantCoding
};

enum class DbcEntityKind {
    Node,
    Message,
    Signal,
    ValueTable,
    AttributeDefinition,
    AttributeDefault,
    AttributeValue,
    EnvironmentVariable,
    SignalGroup
};

enum class LdfEntityKind {
    Overview,
    MasterNode,
    SlaveNode,
    Frame,
    FrameSignal,
    Signal,
    Encoding,
    ScheduleTable,
    EventFrame,
    DiagnosticAddress,
    SignalGroup,
    DiagnosticSignal,
    DiagnosticFrame
};

struct A2lPath {
    A2lEntityKind kind = A2lEntityKind::Module;
    int primaryIndex = -1;
    int secondaryIndex = -1;
    int tertiaryIndex = -1;
};

struct DbcPath {
    DbcEntityKind kind = DbcEntityKind::Message;
    int primaryIndex = -1;
    int secondaryIndex = -1;
    int tertiaryIndex = -1;
};

struct LdfPath {
    LdfEntityKind kind = LdfEntityKind::Frame;
    int primaryIndex = -1;
    int secondaryIndex = -1;
    int tertiaryIndex = -1;
};

struct NodeRef {
    FormatId format = FormatId::Unknown;
    quint64 key = 0;

    bool isValid() const { return key != 0; }
};

using NodePayload = std::variant<A2lPath, DbcPath, LdfPath>;

struct NodeBinding {
    SemanticKind semanticKind = SemanticKind::Entity;
    NodePayload payload = DbcPath{};
    bool selectable = true;
};

class NodeRegistry {
public:
    NodeRef registerBinding(FormatId format, NodeBinding binding);
    const NodeBinding* resolve(NodeRef ref) const;
    void clear();

private:
    quint64 _next_key = 1;
    QHash<quint64, NodeBinding> _bindings;
};
