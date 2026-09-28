#include "sessions/adaptersessionbase.h"

#include "core/diagnostics.h"

AdapterSessionBase::AdapterSessionBase(FormatId formatId,
                                       QString formatName,
                                       QString displayName,
                                       QString sourcePath,
                                       QList<DiagnosticMessage> diagnostics)
    : _format_id(formatId),
      _format_name(std::move(formatName)),
      _display_name(std::move(displayName)),
      _source_path(std::move(sourcePath)),
      _diagnostics(std::move(diagnostics)) {
}

FormatId AdapterSessionBase::formatId() const {
    return _format_id;
}

QString AdapterSessionBase::formatName() const {
    return _format_name;
}

QString AdapterSessionBase::displayName() const {
    return _display_name;
}

QString AdapterSessionBase::sourcePath() const {
    return _source_path;
}

TreeModel* AdapterSessionBase::treeModel() {
    return &_tree_model;
}

DetailModel* AdapterSessionBase::detailModel() {
    return &_detail_model;
}

QList<DiagnosticMessage> AdapterSessionBase::diagnostics() const {
    return _diagnostics;
}

bool AdapterSessionBase::hasDiagnostics() const {
    return !_diagnostics.isEmpty();
}

void AdapterSessionBase::moveModelsToThread(QThread* thread) {
    _tree_model.moveToThread(thread);
    _detail_model.moveToThread(thread);
}

void AdapterSessionBase::setRootItem(std::unique_ptr<TreeItem> root) {
    _tree_model.setRoot(std::move(root));
}

void AdapterSessionBase::addDiagnostic(DiagnosticMessage diagnostic) {
    _diagnostics.push_back(std::move(diagnostic));
}

TreeItem* AdapterSessionBase::appendNode(TreeItem* parent,
                                         const QString& title,
                                         const QString& subtitle,
                                         const QString& iconKey,
                                         SemanticKind semanticKind,
                                         bool selectable) {
    auto child = std::make_unique<TreeItem>();
    child->title = title;
    child->subtitle = subtitle;
    child->iconKey = iconKey;
    child->semanticKind = semanticKind;
    child->nodeKey = ++_last_key;
    child->selectable = selectable;
    child->parent = parent;

    TreeItem* rawChild = child.get();
    parent->children.push_back(std::move(child));
    return rawChild;
}
