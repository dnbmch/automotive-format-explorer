#pragma once

#include "sessions/presentertext.h"

#pragma push_macro("signals")
#undef signals
#include "dbc/dbc.pb.h"
#pragma pop_macro("signals")

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

// A DBC entity a tree row shows: its kind, its index in the document's list
// and, for a signal, its index in the message.
struct DbcPath {
    DbcEntityKind kind = DbcEntityKind::Message;
    int primaryIndex = -1;
    int secondaryIndex = -1;
};

class DbcDetailPresenter final {
public:
    explicit DbcDetailPresenter(const dbc::DbcFile& document)
        : _document(document) {
    }

    QList<DetailSection> buildDetails(const DbcPath& path) const;
    QString buildRawJson(const DbcPath& path) const;

private:
    QList<DetailSection> nodeDetails(const DbcPath& path) const;
    QList<DetailSection> messageDetails(const DbcPath& path) const;
    QList<DetailSection> signalDetails(const DbcPath& path) const;
    QList<DetailSection> valueTableDetails(const DbcPath& path) const;
    QList<DetailSection> attributeDefinitionDetails(const DbcPath& path) const;
    QList<DetailSection> attributeDefaultDetails(const DbcPath& path) const;
    QList<DetailSection> attributeValueDetails(const DbcPath& path) const;
    QList<DetailSection> environmentVariableDetails(const DbcPath& path) const;
    QList<DetailSection> signalGroupDetails(const DbcPath& path) const;
    void appendNodeCrossReferences(QList<DetailSection>& sections, const std::string& nodeName) const;

    const dbc::DbcFile& _document;
};
