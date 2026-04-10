#include "core/noderegistry.h"

NodeRef NodeRegistry::registerBinding(FormatId format, NodeBinding binding) {
    const quint64 key = nextKey_++;
    bindings_.insert(key, std::move(binding));
    return NodeRef{format, key};
}

const NodeBinding* NodeRegistry::resolve(NodeRef ref) const {
    if (!ref.isValid()) {
        return nullptr;
    }

    const auto it = bindings_.constFind(ref.key);
    if (it == bindings_.cend()) {
        return nullptr;
    }

    return &it.value();
}

void NodeRegistry::clear() {
    bindings_.clear();
    nextKey_ = 1;
}
