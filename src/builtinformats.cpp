#include "builtinformats.h"

#include "adapters/a2ladapter.h"
#include "adapters/dbcadapter.h"
#include "adapters/ldfadapter.h"
#include "adapters/mdf4adapter.h"

FormatList builtInFormats() {
    FormatList formats;
    formats.push_back({FormatId::A2L, {QStringLiteral("a2l")}, std::make_unique<A2lAdapter>()});
    formats.push_back({FormatId::DBC, {QStringLiteral("dbc")}, std::make_unique<DbcAdapter>()});
    formats.push_back({FormatId::LDF, {QStringLiteral("ldf")}, std::make_unique<LdfAdapter>()});
    formats.push_back({FormatId::MDF4, {QStringLiteral("mf4")}, std::make_unique<Mdf4Adapter>()});
    return formats;
}
