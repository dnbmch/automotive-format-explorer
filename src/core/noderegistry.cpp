#include "core/noderegistry.h"

NodeRef NodeRegistry::registerBinding(FormatId format, NodeBinding binding) {
    const quint64 key = _next_key++;
    _bindings.insert(key, std::move(binding));
    return NodeRef{format, key};
}

const NodeBinding* NodeRegistry::resolve(NodeRef ref) const {
    if (!ref.isValid()) {
        return nullptr;
    }

    const auto it = _bindings.constFind(ref.key);
    if (it == _bindings.cend()) {
        return nullptr;
    }

    return &it.value();
}

void NodeRegistry::clear() {
    _bindings.clear();
    _next_key = 1;
}
