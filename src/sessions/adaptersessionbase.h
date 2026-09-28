#pragma once

#include "core/treeitem.h"
#include "models/detailmodel.h"
#include "models/treemodel.h"
#include "sessions/documentsession.h"

#include <memory>

// Identity, tree and detail model shared by the format sessions. Each format
// session binds its rows to its own typed entities and answers selectNode().
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
    bool hasDiagnostics() const override;
    void moveModelsToThread(QThread* thread) override;

protected:
    void setRootItem(std::unique_ptr<TreeItem> root);
    // A diagnostic the session raises while building its own views.
    void addDiagnostic(DiagnosticMessage diagnostic);
    // Appends a row with the session's next key.
    TreeItem* appendNode(TreeItem* parent,
                         const QString& title,
                         const QString& subtitle,
                         const QString& iconKey,
                         SemanticKind semanticKind,
                         bool selectable = false);

    TreeModel _tree_model;
    DetailModel _detail_model;

private:
    FormatId _format_id = FormatId::Unknown;
    QString _format_name;
    QString _display_name;
    QString _source_path;
    QList<DiagnosticMessage> _diagnostics;
    quint64 _last_key = 0;
};
