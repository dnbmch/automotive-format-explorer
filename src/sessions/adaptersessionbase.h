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

    NodeRegistry registry_;
    TreeModel treeModel_;
    DetailModel detailModel_;

private:
    FormatId formatId_ = FormatId::Unknown;
    QString formatName_;
    QString displayName_;
    QString sourcePath_;
    QList<DiagnosticMessage> diagnostics_;
    std::unique_ptr<DetailPresenter> detailPresenter_;
};
