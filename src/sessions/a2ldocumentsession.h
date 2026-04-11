#pragma once

#include "sessions/adaptersessionbase.h"

#pragma push_macro("signals")
#undef signals
#include "a2l/a2l.pb.h"
#pragma pop_macro("signals")

#include <tuple>
#include <map>

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
    void moveModelsToThread(QThread* thread) override;

private:
    void buildTree();
    void buildMemoryMap();

    a2l::A2lFile document_;
    std::unique_ptr<MemoryMapModel> memoryMapModel_;

    // Maps (entityKind, moduleIndex, secondaryIndex) → tree nodeKey.
    // Populated during buildTree(), consumed by buildMemoryMap().
    using EntityKey = std::tuple<int, int, int>;
    std::map<EntityKey, quint64> treeNodeKeys_;
};
