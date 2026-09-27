#pragma once

#include "sessions/adaptersessionbase.h"
#include "sessions/ldfdetailpresenter.h"

#pragma push_macro("signals")
#undef signals
#include "ldf/ldf.pb.h"
#pragma pop_macro("signals")

#include <memory>
#include <map>
#include <tuple>
#include <unordered_map>

class SignalMapModel;

class LdfDocumentSession final : public AdapterSessionBase {
public:
    LdfDocumentSession(QString displayName,
                       QString sourcePath,
                       ldf::LdfFile document,
                       QList<DiagnosticMessage> diagnostics = {});
    ~LdfDocumentSession() override;

    QUrl centerPanelSource() const override;
    QAbstractListModel* centerPanelModel() override;
    void selectNode(quint64 key) override;
    void moveModelsToThread(QThread* thread) override;

private:
    void buildTree();
    void buildSignalMap();
    // Appends a row that shows the entity at `path`.
    TreeItem* appendEntity(TreeItem* parent, const QString& title, const QString& subtitle,
                           const QString& iconKey, SemanticKind semanticKind, LdfPath path);

    ldf::LdfFile _document;
    LdfDetailPresenter _presenter;
    // The entity each entity row shows, by the row's key.
    std::unordered_map<quint64, LdfPath> _paths;
    std::unique_ptr<SignalMapModel> _signal_map_model;

    // Maps (entityKind, frameIndex, signalIndex) -> tree nodeKey.
    using EntityKey = std::tuple<int, int, int>;
    std::map<EntityKey, quint64> _tree_node_keys;
};
