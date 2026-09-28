#pragma once

#include "sessions/a2ldetailpresenter.h"
#include "sessions/adaptersessionbase.h"

#pragma push_macro("signals")
#undef signals
#include "a2l/a2l.pb.h"
#pragma pop_macro("signals")

#include <tuple>
#include <map>
#include <unordered_map>

class MemoryMapModel;

class A2lDocumentSession final : public AdapterSessionBase {
public:
    A2lDocumentSession(QString displayName,
                       QString sourcePath,
                       a2l::A2lFile document,
                       QList<DiagnosticMessage> diagnostics = {});
    ~A2lDocumentSession() override;

    QUrl centerPanelSource() const override;
    QAbstractListModel* centerPanelModel() override;
    void selectNode(quint64 key) override;
    void moveModelsToThread(QThread* thread) override;

private:
    void buildTree();
    void buildMemoryMap();
    // Appends a row that shows the entity at `path`.
    TreeItem* appendEntity(TreeItem* parent, const QString& title, const QString& subtitle,
                           const QString& iconKey, A2lPath path);

    a2l::A2lFile _document;
    A2lDetailPresenter _presenter;
    // The entity each entity row shows, by the row's key.
    std::unordered_map<quint64, A2lPath> _paths;
    std::unique_ptr<MemoryMapModel> _memory_map_model;

    // Maps (entityKind, moduleIndex, secondaryIndex) → tree nodeKey.
    // Populated during buildTree(), consumed by buildMemoryMap().
    using EntityKey = std::tuple<int, int, int>;
    std::map<EntityKey, quint64> _tree_node_keys;
};
