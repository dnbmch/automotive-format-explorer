#pragma once

#include <QString>

#include <memory>
#include <vector>

enum class SemanticKind {
    Root,
    Section,
    Entity,
    Attribute,
    Diagnostic
};

struct TreeItem {
    QString title;
    QString subtitle;
    QString iconKey;
    SemanticKind semanticKind = SemanticKind::Root;
    // Session-local key, nonzero for every row; only the invisible root keeps 0.
    quint64 nodeKey = 0;
    bool selectable = false;
    // Position under the parent, set when the tree is installed in a TreeModel.
    int row = 0;
    TreeItem* parent = nullptr;
    std::vector<std::unique_ptr<TreeItem>> children;
};
