#pragma once

#include "sessions/presentertext.h"

#pragma push_macro("signals")
#undef signals
#include "a2l/a2l.pb.h"
#include "a2l/ccp.pb.h"
#include "a2l/record_layout.pb.h"
#include "a2l/xcp.pb.h"
#pragma pop_macro("signals")

#include <QString>
#include <QStringList>

#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <string>
#include <tuple>
#include <type_traits>

#include <google/protobuf/repeated_field.h>

enum class A2lEntityKind {
    Module,
    Measurement,
    Characteristic,
    AxisPts,
    Blob,
    CompuMethod,
    RecordLayout,
    Unit,
    Function,
    Group,
    Frame,
    Transformer,
    UserRights,
    XcpSummary,
    CcpSummary,
    TypedefItem,
    Instance,
    VariantCoding
};

// An A2L entity a tree row shows: its kind, the module, the entity's index
// in that module's list and, for typedefs, the typedef category.
struct A2lPath {
    A2lEntityKind kind = A2lEntityKind::Module;
    int primaryIndex = -1;
    int secondaryIndex = -1;
    int tertiaryIndex = -1;
};

// The tree row of each named entity, by module, kind and name: what a link in
// the details selects. The session fills it while it builds the tree.
class A2lLinks {
public:
    void add(int module, A2lEntityKind kind, const std::string& name, quint64 key) {
        _keys.emplace(Key{module, kind, name}, key);
    }

    // The row of the first of `kinds` that has an entity of this name, or 0.
    // A reference may name any kind of one namespace: a TRANSFORMER's object
    // is a CHARACTERISTIC, an AXIS_PTS, a BLOB or an INSTANCE.
    quint64 find(int module, const std::string& name,
                 std::initializer_list<A2lEntityKind> kinds) const {
        for (A2lEntityKind kind : kinds) {
            const auto it = _keys.find(Key{module, kind, name});
            if (it != _keys.end()) {
                return it->second;
            }
        }
        return 0;
    }

private:
    using Key = std::tuple<int, A2lEntityKind, std::string>;
    std::map<Key, quint64> _keys;
};

// Format-specific helpers live in a2ldetail so they don't collide with other
// backends' same-named global helpers (e.g. LDF's differently-defined
// numberText) when all backends link statically into one executable.
namespace a2ldetail {

constexpr int kTypedefCharacteristicCategory = 0;
constexpr int kTypedefStructureCategory = 1;
constexpr int kTypedefAxisCategory = 2;
constexpr int kTypedefMeasurementCategory = 3;
constexpr int kTypedefBlobCategory = 4;

template<typename T>
QString numberText(T value) {
    if constexpr (std::is_floating_point_v<T>) {
        return QString::number(static_cast<double>(value));
    } else if constexpr (std::is_unsigned_v<T>) {
        return QString::number(static_cast<qulonglong>(value));
    } else {
        return QString::number(static_cast<qlonglong>(value));
    }
}

template<typename T>
void addNumberField(QList<DetailField>& fields, const QString& key, T value) {
    fields.push_back(DetailField{key, numberText(value)});
}

template<typename T>
void addOptionalNumber(QList<DetailField>& fields, const QString& key, bool present, T value) {
    if (present) {
        addNumberField(fields, key, value);
    }
}

inline void addOptionalString(QList<DetailField>& fields,
                              const QString& key,
                              bool present,
                              const std::string& value) {
    if (present) {
        addField(fields, key, text(value));
    }
}

inline void addOptionalBool(QList<DetailField>& fields, const QString& key, bool present, bool value) {
    if (present) {
        addField(fields, key, boolText(value));
    }
}

inline QString joinNumbers(const google::protobuf::RepeatedField<google::protobuf::uint32>& values) {
    QStringList items;
    items.reserve(values.size());
    for (google::protobuf::uint32 value : values) {
        items.push_back(QString::number(value));
    }
    return items.join(QStringLiteral(" x "));
}

inline QString joinIntegers(const google::protobuf::RepeatedField<int>& values) {
    QStringList items;
    items.reserve(values.size());
    for (int value : values) {
        items.push_back(QString::number(value));
    }
    return items.join(QStringLiteral(", "));
}

inline void addIfDataCounts(QList<DetailField>& fields,
                            const google::protobuf::RepeatedPtrField<a2l::IfData>& ifDatas) {
    int xcpCount = 0;
    int ccpCount = 0;
    int rawCount = 0;
    for (const auto& ifData : ifDatas) {
        if (ifData.has_xcp()) {
            ++xcpCount;
        } else if (ifData.has_ccp()) {
            ++ccpCount;
        } else if (ifData.has_raw_payload()) {
            ++rawCount;
        }
    }

    if (!ifDatas.empty()) {
        addNumberField(fields, QStringLiteral("IF_DATA Blocks"), ifDatas.size());
        if (xcpCount > 0) {
            addNumberField(fields, QStringLiteral("XCP Blocks"), xcpCount);
        }
        if (ccpCount > 0) {
            addNumberField(fields, QStringLiteral("CCP Blocks"), ccpCount);
        }
        if (rawCount > 0) {
            addNumberField(fields, QStringLiteral("Raw Blocks"), rawCount);
        }
    }
}

inline QString a2lEntityKindName(A2lEntityKind kind) {
    switch (kind) {
    case A2lEntityKind::Module:         return QStringLiteral("Module");
    case A2lEntityKind::Measurement:    return QStringLiteral("Measurement");
    case A2lEntityKind::Characteristic: return QStringLiteral("Characteristic");
    case A2lEntityKind::AxisPts:        return QStringLiteral("Axis Points");
    case A2lEntityKind::Blob:           return QStringLiteral("Blob");
    case A2lEntityKind::CompuMethod:    return QStringLiteral("Compu Method");
    case A2lEntityKind::RecordLayout:   return QStringLiteral("Record Layout");
    case A2lEntityKind::Unit:           return QStringLiteral("Unit");
    case A2lEntityKind::Function:       return QStringLiteral("Function");
    case A2lEntityKind::Group:          return QStringLiteral("Group");
    case A2lEntityKind::Frame:          return QStringLiteral("Frame");
    case A2lEntityKind::Transformer:    return QStringLiteral("Transformer");
    case A2lEntityKind::UserRights:     return QStringLiteral("User Rights");
    case A2lEntityKind::XcpSummary:     return QStringLiteral("XCP Summary");
    case A2lEntityKind::CcpSummary:     return QStringLiteral("CCP Summary");
    case A2lEntityKind::TypedefItem:    return QStringLiteral("Typedef");
    case A2lEntityKind::Instance:       return QStringLiteral("Instance");
    case A2lEntityKind::VariantCoding:  return QStringLiteral("Variant Coding");
    }
    return QStringLiteral("Unknown");
}

inline QString a2lSectionName(A2lEntityKind kind) {
    switch (kind) {
    case A2lEntityKind::Module:         return QStringLiteral("Modules");
    case A2lEntityKind::Measurement:    return QStringLiteral("Measurements");
    case A2lEntityKind::Characteristic: return QStringLiteral("Characteristics");
    case A2lEntityKind::AxisPts:        return QStringLiteral("Axis Points");
    case A2lEntityKind::Blob:           return QStringLiteral("Blobs");
    case A2lEntityKind::CompuMethod:    return QStringLiteral("Compu Methods");
    case A2lEntityKind::RecordLayout:   return QStringLiteral("Record Layouts");
    case A2lEntityKind::Unit:           return QStringLiteral("Units");
    case A2lEntityKind::Function:       return QStringLiteral("Functions");
    case A2lEntityKind::Group:          return QStringLiteral("Groups");
    case A2lEntityKind::Frame:          return QStringLiteral("Frames");
    case A2lEntityKind::Transformer:    return QStringLiteral("Transformers");
    case A2lEntityKind::UserRights:     return QStringLiteral("User Rights");
    case A2lEntityKind::XcpSummary:     return QStringLiteral("Protocols");
    case A2lEntityKind::CcpSummary:     return QStringLiteral("Protocols");
    case A2lEntityKind::TypedefItem:    return QStringLiteral("Types");
    case A2lEntityKind::Instance:       return QStringLiteral("Types");
    case A2lEntityKind::VariantCoding:  return QStringLiteral("Variant Coding");
    }
    return {};
}

// DataType -> byte size lookup.
inline uint64_t dataTypeSize(a2l::DataType dt) {
    switch (dt) {
    case a2l::DATA_TYPE_UBYTE:
    case a2l::DATA_TYPE_SBYTE:        return 1;
    case a2l::DATA_TYPE_UWORD:
    case a2l::DATA_TYPE_SWORD:
    case a2l::DATA_TYPE_FLOAT16_IEEE: return 2;
    case a2l::DATA_TYPE_ULONG:
    case a2l::DATA_TYPE_SLONG:
    case a2l::DATA_TYPE_FLOAT32_IEEE: return 4;
    case a2l::DATA_TYPE_A_UINT64:
    case a2l::DATA_TYPE_A_INT64:
    case a2l::DATA_TYPE_FLOAT64_IEEE: return 8;
    default:                          return 0;
    }
}

// "k * x + d" without its zero terms.
inline QString affineText(double k, double d, const QString& x) {
    if (k == 0.0) {
        return numberText(d);
    }
    QString term = k == 1.0 ? x : QStringLiteral("%1 * %2").arg(numberText(k), x);
    if (d != 0.0) {
        term += QStringLiteral(" %1 %2").arg(d < 0.0 ? QStringLiteral("-") : QStringLiteral("+"),
                                             numberText(std::abs(d)));
    }
    return term;
}

// The conversion as engineers read it, the physical value of the internal
// one. LINEAR is that already. RAT_FUNC's COEFFS run the other way,
// INT = (a*PHYS^2 + b*PHYS + c) / (d*PHYS^2 + e*PHYS + f) (ASAP2 1.7.1 section
// 4.7.38): with a = d = 0 it inverts to PHYS = (f*INT - c) / (b - e*INT), and
// otherwise it is shown as given. Empty for a table, whose rows say it.
inline QString conversionFormula(const a2l::CompuMethod& method) {
    const QString in = QStringLiteral("int");
    switch (method.conversion_type()) {
    case a2l::CONVERSION_TYPE_IDENTICAL:
        return QStringLiteral("phys = int");
    case a2l::CONVERSION_TYPE_LINEAR:
        if (!method.has_coeffs_linear()) {
            return {};
        }
        return QStringLiteral("phys = ")
            + affineText(method.coeffs_linear().a(), method.coeffs_linear().b(), in);
    case a2l::CONVERSION_TYPE_RAT_FUNC: {
        if (!method.has_coeffs()) {
            return {};
        }
        const auto& c = method.coeffs();
        if (c.a() == 0.0 && c.d() == 0.0 && c.b() != 0.0 && c.e() == 0.0) {
            return QStringLiteral("phys = ") + affineText(c.f() / c.b(), -c.c() / c.b(), in);
        }
        if (c.a() == 0.0 && c.d() == 0.0 && c.e() != 0.0) {
            return QStringLiteral("phys = (%1) / (%2)")
                .arg(affineText(c.f(), -c.c(), in), affineText(-c.e(), c.b(), in));
        }
        return QStringLiteral("int = (%1 * phys^2 + %2 * phys + %3) / (%4 * phys^2 + %5 * phys + %6)")
            .arg(numberText(c.a()), numberText(c.b()), numberText(c.c()),
                 numberText(c.d()), numberText(c.e()), numberText(c.f()));
    }
    case a2l::CONVERSION_TYPE_FORM:
        return method.has_formula_raw() ? text(method.formula_raw()) : QString();
    default:
        return {};
    }
}

inline QString recordLayoutComponentSummary(const a2l::RecordLayoutComponent& component) {
    if (component.has_alignment()) {
        const auto& alignment = component.alignment();
        return QStringLiteral("Alignment %1 border=%2")
            .arg(enumKeyword(alignment.type()))
            .arg(alignment.alignment_border());
    }
    if (component.has_function_values()) {
        const auto& values = component.function_values();
        return QStringLiteral("Function Values pos=%1 type=%2")
            .arg(values.position())
            .arg(enumKeyword(values.datatype()));
    }
    if (component.has_axis_points()) {
        const auto& axis = component.axis_points();
        return QStringLiteral("Axis Points %1 pos=%2")
            .arg(enumKeyword(axis.axis()))
            .arg(axis.position());
    }
    if (component.has_axis_rescale()) {
        const auto& axis = component.axis_rescale();
        return QStringLiteral("Axis Rescale %1 pos=%2")
            .arg(enumKeyword(axis.axis()))
            .arg(axis.position());
    }
    if (component.has_axis_count()) {
        const auto& axis = component.axis_count();
        return QStringLiteral("Axis Count %1 pos=%2")
            .arg(enumKeyword(axis.axis()))
            .arg(axis.position());
    }
    if (component.has_rescale_count()) {
        const auto& axis = component.rescale_count();
        return QStringLiteral("Rescale Count %1 pos=%2")
            .arg(enumKeyword(axis.axis()))
            .arg(axis.position());
    }
    if (component.has_fixed_axis_count()) {
        const auto& axis = component.fixed_axis_count();
        return QStringLiteral("Fixed Axis Count %1 n=%2")
            .arg(enumKeyword(axis.axis()))
            .arg(axis.number_of_axis_points());
    }
    if (component.has_identification()) {
        const auto& identification = component.identification();
        return QStringLiteral("Identification pos=%1 type=%2")
            .arg(identification.position())
            .arg(enumKeyword(identification.datatype()));
    }
    if (component.has_axis_operand()) {
        const auto& operand = component.axis_operand();
        return QStringLiteral("Axis Operand %1 %2 pos=%3")
            .arg(enumKeyword(operand.kind()))
            .arg(enumKeyword(operand.axis()))
            .arg(operand.position());
    }
    if (component.has_address_reference()) {
        const auto& reference = component.address_reference();
        return QStringLiteral("Address Ref %1 %2 pos=%3")
            .arg(enumKeyword(reference.kind()))
            .arg(enumKeyword(reference.axis()))
            .arg(reference.position());
    }
    if (component.has_static_flag()) {
        return QStringLiteral("Static Flag %1")
            .arg(enumKeyword(component.static_flag().flag()));
    }
    if (component.has_raw_component()) {
        return text(component.raw_component());
    }
    return QStringLiteral("Component");
}

} // namespace a2ldetail

class A2lDetailPresenter final {
public:
    A2lDetailPresenter(const a2l::A2lFile& document, const A2lLinks& links)
        : _document(document), _links(links) {
    }

    QList<DetailSection> buildDetails(const A2lPath& path) const;
    QString buildRawJson(const A2lPath& path) const;

private:
    const a2l::Module* moduleAt(int index) const;
    QString entityName(const A2lPath& path) const;
    QString buildBreadcrumb(const A2lPath& path) const;
    const a2l::CompuMethod* findCompuMethod(const a2l::Module& module, const std::string& name) const;
    void insertResolvedConversion(QList<DetailSection>& sections, const A2lPath& path) const;
    void appendCrossReferences(QList<DetailSection>& sections, const A2lPath& path) const;
    void appendConversionTable(QList<DetailSection>& sections, const a2l::Module& module,
                               const std::string& name) const;

    // A field naming another entity of the module: a link to its row when the
    // file defines one of `kinds` by that name. Nothing for an empty name.
    void addLink(QList<DetailField>& fields, const QString& key, int module,
                 const std::string& name, std::initializer_list<A2lEntityKind> kinds) const;
    // A section of names, each a link; a long list ends in the count it leaves out.
    void appendNameList(QList<DetailSection>& sections, const QString& title, int module,
                        const google::protobuf::RepeatedPtrField<std::string>& names,
                        std::initializer_list<A2lEntityKind> kinds) const;

    QList<DetailSection> moduleDetails(const A2lPath& path) const;
    QList<DetailSection> measurementDetails(const A2lPath& path) const;
    QList<DetailSection> characteristicDetails(const A2lPath& path) const;
    QList<DetailSection> axisPtsDetails(const A2lPath& path) const;
    QList<DetailSection> blobDetails(const A2lPath& path) const;
    QList<DetailSection> compuMethodDetails(const A2lPath& path) const;
    QList<DetailSection> recordLayoutDetails(const A2lPath& path) const;
    QList<DetailSection> unitDetails(const A2lPath& path) const;
    QList<DetailSection> functionDetails(const A2lPath& path) const;
    QList<DetailSection> groupDetails(const A2lPath& path) const;
    QList<DetailSection> frameDetails(const A2lPath& path) const;
    QList<DetailSection> transformerDetails(const A2lPath& path) const;
    QList<DetailSection> userRightsDetails(const A2lPath& path) const;
    QList<DetailSection> xcpSummaryDetails(const A2lPath& path) const;
    QList<DetailSection> ccpSummaryDetails(const A2lPath& path) const;
    QList<DetailSection> typedefDetails(const A2lPath& path) const;
    QList<DetailSection> instanceDetails(const A2lPath& path) const;
    QList<DetailSection> variantCodingDetails(const A2lPath& path) const;

    const a2l::A2lFile& _document;
    const A2lLinks& _links;
};
