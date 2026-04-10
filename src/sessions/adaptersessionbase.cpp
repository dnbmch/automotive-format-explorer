#include "sessions/adaptersessionbase.h"

#include "core/diagnostics.h"

AdapterSessionBase::AdapterSessionBase(FormatId formatId,
                                       QString formatName,
                                       QString displayName,
                                       QString sourcePath,
                                       QList<DiagnosticMessage> diagnostics)
    : formatId_(formatId),
      formatName_(std::move(formatName)),
      displayName_(std::move(displayName)),
      sourcePath_(std::move(sourcePath)),
      diagnostics_(std::move(diagnostics)) {
}

FormatId AdapterSessionBase::formatId() const {
    return formatId_;
}

QString AdapterSessionBase::formatName() const {
    return formatName_;
}

QString AdapterSessionBase::displayName() const {
    return displayName_;
}

QString AdapterSessionBase::sourcePath() const {
    return sourcePath_;
}

TreeModel* AdapterSessionBase::treeModel() {
    return &treeModel_;
}

DetailModel* AdapterSessionBase::detailModel() {
    return &detailModel_;
}

QList<DiagnosticMessage> AdapterSessionBase::diagnostics() const {
    return diagnostics_;
}

bool AdapterSessionBase::hasWarnings() const {
    return ::hasWarnings(diagnostics_);
}

void AdapterSessionBase::selectNode(quint64 key) {
    if (!detailPresenter_) {
        detailModel_.setSections({});
        return;
    }

    const NodeBinding* binding = registry_.resolve(NodeRef{formatId_, key});
    if (!binding || !binding->selectable) {
        detailModel_.setSections({});
        return;
    }

    detailModel_.setSections(detailPresenter_->buildDetails(*binding));
}

void AdapterSessionBase::setRootItem(std::unique_ptr<TreeItem> root) {
    treeModel_.setRoot(std::move(root));
}

void AdapterSessionBase::setDetailPresenter(std::unique_ptr<DetailPresenter> presenter) {
    detailPresenter_ = std::move(presenter);
}

NodeRef AdapterSessionBase::bindNode(NodeBinding binding) {
    return registry_.registerBinding(formatId_, std::move(binding));
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
