#pragma once

#include "sessions/adaptersessionbase.h"

#pragma push_macro("signals")
#undef signals
#include "dbc/dbc.pb.h"
#pragma pop_macro("signals")

#include <memory>
#include <map>
#include <tuple>

class SignalMapModel;

class DbcDocumentSession final : public AdapterSessionBase {
public:
    DbcDocumentSession(QString displayName,
                       QString sourcePath,
                       dbc::DbcFile document,
                       QList<DiagnosticMessage> diagnostics = {});
    ~DbcDocumentSession() override;

    QUrl centerPanelSource() const override;
    QAbstractListModel* centerPanelModel() override;
    void moveModelsToThread(QThread* thread) override;

private:
    void buildTree();
    void buildSignalMap();

    dbc::DbcFile document_;
    std::unique_ptr<SignalMapModel> signalMapModel_;

    // Maps (entityKind, messageIndex, signalIndex) -> tree nodeKey.
    using EntityKey = std::tuple<int, int, int>;
    std::map<EntityKey, quint64> treeNodeKeys_;
};
