#include "builtinformats.h"

#include "adapters/a2ladapter.h"
#include "adapters/dbcadapter.h"
#include "adapters/ldfadapter.h"
#include "adapters/mdf4adapter.h"

FormatList builtInFormats() {
    FormatList formats;
    formats.push_back({FormatId::A2L, {QStringLiteral("a2l")}, std::make_unique<A2lAdapter>(),
                       QStringLiteral("demo_ecu.a2l")});
    formats.push_back({FormatId::DBC, {QStringLiteral("dbc")}, std::make_unique<DbcAdapter>(),
                       QStringLiteral("tesla_can.dbc")});
    formats.push_back({FormatId::LDF, {QStringLiteral("ldf")}, std::make_unique<LdfAdapter>(),
                       QStringLiteral("demo_seat.ldf")});
    formats.push_back({FormatId::MDF4, {QStringLiteral("mf4")}, std::make_unique<Mdf4Adapter>(),
                       QStringLiteral("demo_recording.mf4")});
    return formats;
}
