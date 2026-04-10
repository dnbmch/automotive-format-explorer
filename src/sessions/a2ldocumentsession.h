#pragma once

#include "sessions/adaptersessionbase.h"

#pragma push_macro("signals")
#undef signals
#include "a2l/a2l.pb.h"
#pragma pop_macro("signals")

class A2lDocumentSession final : public AdapterSessionBase {
public:
    A2lDocumentSession(QString displayName,
                       QString sourcePath,
                       a2l::A2lFile document,
                       QList<DiagnosticMessage> diagnostics = {});

private:
    void buildTree();

    a2l::A2lFile document_;
};
