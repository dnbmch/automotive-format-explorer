#pragma once

#include "sessions/adaptersessionbase.h"

#pragma push_macro("signals")
#undef signals
#include "ldf/ldf.pb.h"
#pragma pop_macro("signals")

#include <memory>
#include <map>
#include <tuple>

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
    void moveModelsToThread(QThread* thread) override;

private:
    void buildTree();
    void buildSignalMap();

    ldf::LdfFile _document;
    std::unique_ptr<SignalMapModel> _signal_map_model;

    // Maps (entityKind, frameIndex, signalIndex) -> tree nodeKey.
    using EntityKey = std::tuple<int, int, int>;
    std::map<EntityKey, quint64> _tree_node_keys;
};
