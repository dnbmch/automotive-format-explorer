#pragma once

#include "sessions/adaptersessionbase.h"

#pragma push_macro("signals")
#undef signals
#include "dbc/dbc.pb.h"
#pragma pop_macro("signals")

class DbcDocumentSession final : public AdapterSessionBase {
public:
    DbcDocumentSession(QString displayName,
                       QString sourcePath,
                       dbc::DbcFile document,
                       QList<DiagnosticMessage> diagnostics = {});

private:
    void buildTree();

    dbc::DbcFile document_;
};
