#pragma once

#include "core/detailpresenter.h"
#include "core/noderegistry.h"
#include "core/treeitem.h"
#include "models/detailmodel.h"
#include "models/treemodel.h"
#include "sessions/documentsession.h"

#include <memory>
#include <optional>

class AdapterSessionBase : public DocumentSession {
public:
    AdapterSessionBase(FormatId formatId,
                       QString formatName,
                       QString displayName,
                       QString sourcePath,
                       QList<DiagnosticMessage> diagnostics = {});

    FormatId formatId() const override;
    QString formatName() const override;
    QString displayName() const override;
    QString sourcePath() const override;
    TreeModel* treeModel() override;
    DetailModel* detailModel() override;
    QList<DiagnosticMessage> diagnostics() const override;
    bool hasWarnings() const override;
    void selectNode(quint64 key) override;
    void moveModelsToThread(QThread* thread) override;

protected:
    void setRootItem(std::unique_ptr<TreeItem> root);
    void setDetailPresenter(std::unique_ptr<DetailPresenter> presenter);
    NodeRef bindNode(NodeBinding binding);
    TreeItem* appendNode(TreeItem* parent,
                         const QString& title,
                         const QString& subtitle,
                         const QString& iconKey,
                         SemanticKind semanticKind,
                         std::optional<NodeBinding> binding = std::nullopt);

    NodeRegistry _registry;
    TreeModel _tree_model;
    DetailModel _detail_model;

private:
    FormatId _format_id = FormatId::Unknown;
    QString _format_name;
    QString _display_name;
    QString _source_path;
    QList<DiagnosticMessage> _diagnostics;
    std::unique_ptr<DetailPresenter> _detail_presenter;
};
