#pragma once

#include "sessions/adaptersessionbase.h"

#pragma push_macro("signals")
#undef signals
#include "ldf/ldf.pb.h"
#pragma pop_macro("signals")

class LdfDocumentSession final : public AdapterSessionBase {
public:
    LdfDocumentSession(QString displayName,
                       QString sourcePath,
                       ldf::LdfFile document,
                       QList<DiagnosticMessage> diagnostics = {});

private:
    void buildTree();

    ldf::LdfFile document_;
};
