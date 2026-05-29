#pragma once

#include "core/detailpresenter.h"

#pragma push_macro("signals")
#undef signals
#include "a2l/a2l.pb.h"
#include "a2l/ccp.pb.h"
#include "a2l/record_layout.pb.h"
#include "a2l/xcp.pb.h"
#pragma pop_macro("signals")

#include <QString>
#include <QStringList>

#include <type_traits>

#include <google/protobuf/repeated_field.h>

constexpr int kTypedefCharacteristicCategory = 0;
constexpr int kTypedefStructureCategory = 1;
constexpr int kTypedefAxisCategory = 2;

inline QString text(const std::string& value) {
    auto utf8 = QString::fromUtf8(value.data(), static_cast<int>(value.size()));
    if (utf8.contains(QChar::ReplacementCharacter)) {
        return QString::fromLatin1(value.data(), static_cast<int>(value.size()));
    }
    return utf8;
}

inline QString boolText(bool value) {
    return value ? QStringLiteral("Yes") : QStringLiteral("No");
}

inline QString hex64(quint64 value) {
    return QStringLiteral("0x%1").arg(value, 0, 16).toUpper();
}

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

inline void addField(QList<DetailField>& fields, const QString& key, const QString& value) {
    if (!value.isEmpty()) {
        fields.push_back(DetailField{key, value});
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

inline void pushSection(QList<DetailSection>& sections, const QString& title, QList<DetailField> fields) {
    if (!fields.isEmpty()) {
        sections.push_back(DetailSection{title, std::move(fields)});
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
    case A2lEntityKind::CompuMethod:    return QStringLiteral("Compu Method");
    case A2lEntityKind::RecordLayout:   return QStringLiteral("Record Layout");
    case A2lEntityKind::Unit:           return QStringLiteral("Unit");
    case A2lEntityKind::Function:       return QStringLiteral("Function");
    case A2lEntityKind::Group:          return QStringLiteral("Group");
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
    case A2lEntityKind::CompuMethod:    return QStringLiteral("Compu Methods");
    case A2lEntityKind::RecordLayout:   return QStringLiteral("Record Layouts");
    case A2lEntityKind::Unit:           return QStringLiteral("Units");
    case A2lEntityKind::Function:       return QStringLiteral("Functions");
    case A2lEntityKind::Group:          return QStringLiteral("Groups");
    case A2lEntityKind::XcpSummary:     return QStringLiteral("Protocols");
    case A2lEntityKind::CcpSummary:     return QStringLiteral("Protocols");
    case A2lEntityKind::TypedefItem:    return QStringLiteral("Types");
    case A2lEntityKind::Instance:       return QStringLiteral("Types");
    case A2lEntityKind::VariantCoding:  return QStringLiteral("Variant Coding");
    }
    return {};
}

inline int dataTypeSizeBytes(a2l::DataType dt) {
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

inline QString formatCompuMethodSummary(const a2l::CompuMethod& method) {
    QString type = text(a2l::ConversionType_Name(method.conversion_type()));
    if (method.has_coeffs_linear()) {
        const auto& c = method.coeffs_linear();
        return QStringLiteral("%1: %2x + %3")
            .arg(type)
            .arg(numberText(c.a()))
            .arg(numberText(c.b()));
    }
    if (method.has_coeffs()) {
        const auto& c = method.coeffs();
        if (c.a() == 0.0 && c.d() == 0.0 && c.e() == 0.0 && c.f() == 1.0) {
            return QStringLiteral("%1: %2x + %3")
                .arg(type)
                .arg(numberText(c.b()))
                .arg(numberText(c.c()));
        }
        return QStringLiteral("%1: (%2x²+%3x+%4)/(%5x²+%6x+%7)")
            .arg(type)
            .arg(numberText(c.a()))
            .arg(numberText(c.b()))
            .arg(numberText(c.c()))
            .arg(numberText(c.d()))
            .arg(numberText(c.e()))
            .arg(numberText(c.f()));
    }
    if (method.has_compu_tab_ref()) {
        return QStringLiteral("%1: %2").arg(type, text(method.compu_tab_ref()));
    }
    if (method.has_formula_raw()) {
        return QStringLiteral("%1: %2").arg(type, text(method.formula_raw()));
    }
    return type;
}

inline QString recordLayoutComponentSummary(const a2l::RecordLayoutComponent& component) {
    if (component.has_alignment()) {
        const auto& alignment = component.alignment();
        return QStringLiteral("Alignment %1 border=%2")
            .arg(text(a2l::RecordLayoutAlignmentType_Name(alignment.type())))
            .arg(alignment.alignment_border());
    }
    if (component.has_function_values()) {
        const auto& values = component.function_values();
        return QStringLiteral("Function Values pos=%1 type=%2")
            .arg(values.position())
            .arg(text(a2l::DataType_Name(values.datatype())));
    }
    if (component.has_axis_points()) {
        const auto& axis = component.axis_points();
        return QStringLiteral("Axis Points %1 pos=%2")
            .arg(text(a2l::RecordLayoutAxis_Name(axis.axis())))
            .arg(axis.position());
    }
    if (component.has_axis_rescale()) {
        const auto& axis = component.axis_rescale();
        return QStringLiteral("Axis Rescale %1 pos=%2")
            .arg(text(a2l::RecordLayoutAxis_Name(axis.axis())))
            .arg(axis.position());
    }
    if (component.has_axis_count()) {
        const auto& axis = component.axis_count();
        return QStringLiteral("Axis Count %1 pos=%2")
            .arg(text(a2l::RecordLayoutAxis_Name(axis.axis())))
            .arg(axis.position());
    }
    if (component.has_rescale_count()) {
        const auto& axis = component.rescale_count();
        return QStringLiteral("Rescale Count %1 pos=%2")
            .arg(text(a2l::RecordLayoutAxis_Name(axis.axis())))
            .arg(axis.position());
    }
    if (component.has_fixed_axis_count()) {
        const auto& axis = component.fixed_axis_count();
        return QStringLiteral("Fixed Axis Count %1 n=%2")
            .arg(text(a2l::RecordLayoutAxis_Name(axis.axis())))
            .arg(axis.number_of_axis_points());
    }
    if (component.has_identification()) {
        const auto& identification = component.identification();
        return QStringLiteral("Identification pos=%1 type=%2")
            .arg(identification.position())
            .arg(text(a2l::DataType_Name(identification.datatype())));
    }
    if (component.has_axis_operand()) {
        const auto& operand = component.axis_operand();
        return QStringLiteral("Axis Operand %1 %2 pos=%3")
            .arg(text(a2l::RecordLayoutAxisOperandKind_Name(operand.kind())))
            .arg(text(a2l::RecordLayoutAxis_Name(operand.axis())))
            .arg(operand.position());
    }
    if (component.has_address_reference()) {
        const auto& reference = component.address_reference();
        return QStringLiteral("Address Ref %1 %2 pos=%3")
            .arg(text(a2l::RecordLayoutAddressReferenceKind_Name(reference.kind())))
            .arg(text(a2l::RecordLayoutAxis_Name(reference.axis())))
            .arg(reference.position());
    }
    if (component.has_static_flag()) {
        return QStringLiteral("Static Flag %1")
            .arg(text(a2l::RecordLayoutStaticFlag_Name(component.static_flag().flag())));
    }
    if (component.has_raw_component()) {
        return text(component.raw_component());
    }
    return QStringLiteral("Component");
}

class A2lDetailPresenter final : public DetailPresenter {
public:
    explicit A2lDetailPresenter(const a2l::A2lFile& document)
        : _document(document) {
    }

    QList<DetailSection> buildDetails(const NodeBinding& binding) const override;
    QString buildRawJson(const NodeBinding& binding) const override;

private:
    const a2l::Module* moduleAt(int index) const;
    QString entityName(const A2lPath& path) const;
    QString buildBreadcrumb(const A2lPath& path) const;
    const a2l::CompuMethod* findCompuMethod(const a2l::Module& module, const std::string& name) const;
    void appendConversionSummary(QList<DetailSection>& sections, const A2lPath& path) const;
    void appendCrossReferences(QList<DetailSection>& sections, const A2lPath& path) const;

    QList<DetailSection> moduleDetails(const A2lPath& path) const;
    QList<DetailSection> measurementDetails(const A2lPath& path) const;
    QList<DetailSection> characteristicDetails(const A2lPath& path) const;
    QList<DetailSection> axisPtsDetails(const A2lPath& path) const;
    QList<DetailSection> compuMethodDetails(const A2lPath& path) const;
    QList<DetailSection> recordLayoutDetails(const A2lPath& path) const;
    QList<DetailSection> unitDetails(const A2lPath& path) const;
    QList<DetailSection> functionDetails(const A2lPath& path) const;
    QList<DetailSection> groupDetails(const A2lPath& path) const;
    QList<DetailSection> xcpSummaryDetails(const A2lPath& path) const;
    QList<DetailSection> ccpSummaryDetails(const A2lPath& path) const;
    QList<DetailSection> typedefDetails(const A2lPath& path) const;
    QList<DetailSection> instanceDetails(const A2lPath& path) const;
    QList<DetailSection> variantCodingDetails(const A2lPath& path) const;

    const a2l::A2lFile& _document;
};
