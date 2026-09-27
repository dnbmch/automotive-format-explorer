#pragma once

#include "sessions/adaptersessionbase.h"
#include "sessions/dbcdetailpresenter.h"

#pragma push_macro("signals")
#undef signals
#include "dbc/dbc.pb.h"
#pragma pop_macro("signals")

#include <memory>
#include <map>
#include <tuple>
#include <unordered_map>

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
    void selectNode(quint64 key) override;
    void moveModelsToThread(QThread* thread) override;

private:
    void buildTree();
    void buildSignalMap();
    // Appends a row that shows the entity at `path`.
    TreeItem* appendEntity(TreeItem* parent, const QString& title, const QString& subtitle,
                           const QString& iconKey, DbcPath path);

    dbc::DbcFile _document;
    DbcDetailPresenter _presenter;
    // The entity each entity row shows, by the row's key.
    std::unordered_map<quint64, DbcPath> _paths;
    std::unique_ptr<SignalMapModel> _signal_map_model;

    // Maps (entityKind, messageIndex, signalIndex) -> tree nodeKey.
    using EntityKey = std::tuple<int, int, int>;
    std::map<EntityKey, quint64> _tree_node_keys;
};
