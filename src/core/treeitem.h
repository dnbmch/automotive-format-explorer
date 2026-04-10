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
    quint64 nodeKey = 0;
    bool selectable = false;
    TreeItem* parent = nullptr;
    std::vector<std::unique_ptr<TreeItem>> children;
};
