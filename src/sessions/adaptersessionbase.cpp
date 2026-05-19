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

bool AdapterSessionBase::hasWarnings() const {
    return ::hasWarnings(_diagnostics);
}

void AdapterSessionBase::selectNode(quint64 key) {
    if (!_detail_presenter) {
        _detail_model.setSections({});
        _detail_model.setRawJsonText({});
        return;
    }

    const NodeBinding* binding = _registry.resolve(NodeRef{_format_id, key});
    if (!binding || !binding->selectable) {
        _detail_model.setSections({});
        _detail_model.setRawJsonText({});
        return;
    }

    _detail_model.setSections(_detail_presenter->buildDetails(*binding));
    _detail_model.setRawJsonText(_detail_presenter->buildRawJson(*binding));
}

void AdapterSessionBase::moveModelsToThread(QThread* thread) {
    _tree_model.moveToThread(thread);
    _detail_model.moveToThread(thread);
}

void AdapterSessionBase::setRootItem(std::unique_ptr<TreeItem> root) {
    _tree_model.setRoot(std::move(root));
}

void AdapterSessionBase::setDetailPresenter(std::unique_ptr<DetailPresenter> presenter) {
    _detail_presenter = std::move(presenter);
}

NodeRef AdapterSessionBase::bindNode(NodeBinding binding) {
    return _registry.registerBinding(_format_id, std::move(binding));
}

TreeItem* AdapterSessionBase::appendNode(TreeItem* parent,
                                         const QString& title,
                                         const QString& subtitle,
                                         const QString& iconKey,
                                         SemanticKind semanticKind,
                                         std::optional<NodeBinding> binding) {
    auto child = std::make_unique<TreeItem>();
    child->title = title;
    child->subtitle = subtitle;
    child->iconKey = iconKey;
    child->semanticKind = semanticKind;
    child->parent = parent;

    if (binding.has_value()) {
        child->selectable = binding->selectable;
        if (child->selectable) {
            child->nodeKey = bindNode(std::move(*binding)).key;
        }
    }

    TreeItem* rawChild = child.get();
    parent->children.push_back(std::move(child));
    return rawChild;
}
