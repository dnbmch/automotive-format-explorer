#include "sessions/a2ldocumentsession.h"
#include "models/memorymapmodel.h"

#pragma push_macro("signals")
#undef signals
#include "a2l/ccp.pb.h"
#include "a2l/record_layout.pb.h"
#include "a2l/xcp.pb.h"
#pragma pop_macro("signals")

#include <QStringList>
#include <type_traits>
#include <unordered_map>
#include <variant>

#include <google/protobuf/json/json.h>

namespace {

constexpr int kTypedefCharacteristicCategory = 0;
constexpr int kTypedefStructureCategory = 1;
constexpr int kTypedefAxisCategory = 2;

QString text(const std::string& value) {
    auto utf8 = QString::fromUtf8(value.data(), static_cast<int>(value.size()));
    if (utf8.contains(QChar::ReplacementCharacter)) {
        return QString::fromLatin1(value.data(), static_cast<int>(value.size()));
    }
    return utf8;
}

QString boolText(bool value) {
    return value ? QStringLiteral("Yes") : QStringLiteral("No");
}

QString hex64(quint64 value) {
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

void addField(QList<DetailField>& fields, const QString& key, const QString& value) {
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

void addOptionalString(QList<DetailField>& fields,
                       const QString& key,
                       bool present,
                       const std::string& value) {
    if (present) {
        addField(fields, key, text(value));
    }
}

void addOptionalBool(QList<DetailField>& fields, const QString& key, bool present, bool value) {
    if (present) {
        addField(fields, key, boolText(value));
    }
}

void pushSection(QList<DetailSection>& sections, const QString& title, QList<DetailField> fields) {
    if (!fields.isEmpty()) {
        sections.push_back(DetailSection{title, std::move(fields)});
    }
}

QString joinNumbers(const google::protobuf::RepeatedField<google::protobuf::uint32>& values) {
    QStringList items;
    items.reserve(values.size());
    for (google::protobuf::uint32 value : values) {
        items.push_back(QString::number(value));
    }
    return items.join(QStringLiteral(" x "));
}

QString joinIntegers(const google::protobuf::RepeatedField<int>& values) {
    QStringList items;
    items.reserve(values.size());
    for (int value : values) {
        items.push_back(QString::number(value));
    }
    return items.join(QStringLiteral(", "));
}

void addIfDataCounts(QList<DetailField>& fields,
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

QString a2lEntityKindName(A2lEntityKind kind) {
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

QString a2lSectionName(A2lEntityKind kind) {
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

int dataTypeSizeBytes(a2l::DataType dt) {
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

QString formatCompuMethodSummary(const a2l::CompuMethod& method) {
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
        return QStringLiteral("%1: (%2x\u00B2+%3x+%4)/(%5x\u00B2+%6x+%7)")
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

QString recordLayoutComponentSummary(const a2l::RecordLayoutComponent& component) {
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
        : document_(document) {
    }

    QList<DetailSection> buildDetails(const NodeBinding& binding) const override {
        if (!std::holds_alternative<A2lPath>(binding.payload)) {
            return {};
        }

        const A2lPath path = std::get<A2lPath>(binding.payload);
        QList<DetailSection> sections;

        switch (path.kind) {
        case A2lEntityKind::Module:
            sections = moduleDetails(path); break;
        case A2lEntityKind::Measurement:
            sections = measurementDetails(path); break;
        case A2lEntityKind::Characteristic:
            sections = characteristicDetails(path); break;
        case A2lEntityKind::AxisPts:
            sections = axisPtsDetails(path); break;
        case A2lEntityKind::CompuMethod:
            sections = compuMethodDetails(path); break;
        case A2lEntityKind::RecordLayout:
            sections = recordLayoutDetails(path); break;
        case A2lEntityKind::Unit:
            sections = unitDetails(path); break;
        case A2lEntityKind::Function:
            sections = functionDetails(path); break;
        case A2lEntityKind::Group:
            sections = groupDetails(path); break;
        case A2lEntityKind::XcpSummary:
            sections = xcpSummaryDetails(path); break;
        case A2lEntityKind::CcpSummary:
            sections = ccpSummaryDetails(path); break;
        case A2lEntityKind::TypedefItem:
            sections = typedefDetails(path); break;
        case A2lEntityKind::Instance:
            sections = instanceDetails(path); break;
        case A2lEntityKind::VariantCoding:
            sections = variantCodingDetails(path); break;
        }

        if (sections.isEmpty()) {
            return sections;
        }

        // Prepend breadcrumb context section
        QList<DetailField> context;
        addField(context, QStringLiteral("Entity Type"), a2lEntityKindName(path.kind));
        QString breadcrumb = buildBreadcrumb(path);
        addField(context, QStringLiteral("Path"), breadcrumb);
        sections.prepend(DetailSection{QStringLiteral("Context"), std::move(context)});

        // Append inline compu method + data size for applicable entities
        appendConversionSummary(sections, path);

        // Append cross-references (which functions/groups reference this entity)
        appendCrossReferences(sections, path);

        return sections;
    }

    QString buildRawJson(const NodeBinding& binding) const override {
        if (!std::holds_alternative<A2lPath>(binding.payload)) {
            return {};
        }

        const A2lPath path = std::get<A2lPath>(binding.payload);
        const auto* module = moduleAt(path.primaryIndex);
        if (!module) {
            return {};
        }

        const google::protobuf::Message* msg = nullptr;
        int i = path.secondaryIndex;

        switch (path.kind) {
        case A2lEntityKind::Module:         msg = module; break;
        case A2lEntityKind::Measurement:    if (i >= 0 && i < module->measurements_size()) msg = &module->measurements(i); break;
        case A2lEntityKind::Characteristic: if (i >= 0 && i < module->characteristics_size()) msg = &module->characteristics(i); break;
        case A2lEntityKind::AxisPts:        if (i >= 0 && i < module->axis_points_size()) msg = &module->axis_points(i); break;
        case A2lEntityKind::CompuMethod:    if (i >= 0 && i < module->compu_methods_size()) msg = &module->compu_methods(i); break;
        case A2lEntityKind::RecordLayout:   if (i >= 0 && i < module->record_layouts_size()) msg = &module->record_layouts(i); break;
        case A2lEntityKind::Unit:           if (i >= 0 && i < module->units_size()) msg = &module->units(i); break;
        case A2lEntityKind::Function:       if (i >= 0 && i < module->functions_size()) msg = &module->functions(i); break;
        case A2lEntityKind::Group:          if (i >= 0 && i < module->groups_size()) msg = &module->groups(i); break;
        case A2lEntityKind::Instance:       if (i >= 0 && i < module->instances_size()) msg = &module->instances(i); break;
        case A2lEntityKind::TypedefItem:
            if (path.tertiaryIndex == kTypedefCharacteristicCategory && i >= 0 && i < module->typedef_characteristics_size())
                msg = &module->typedef_characteristics(i);
            else if (path.tertiaryIndex == kTypedefStructureCategory && i >= 0 && i < module->typedef_structures_size())
                msg = &module->typedef_structures(i);
            else if (path.tertiaryIndex == kTypedefAxisCategory && i >= 0 && i < module->typedef_axes_size())
                msg = &module->typedef_axes(i);
            break;
        case A2lEntityKind::VariantCoding:
            if (module->has_variant_coding()) msg = &module->variant_coding();
            break;
        case A2lEntityKind::XcpSummary:
        case A2lEntityKind::CcpSummary:
            break;
        }

        if (!msg) {
            return {};
        }

        google::protobuf::json::PrintOptions opts;
        opts.add_whitespace = true;
        opts.always_print_fields_with_no_presence = false;
        std::string json;
        auto status = google::protobuf::json::MessageToJsonString(*msg, &json, opts);
        if (!status.ok()) {
            return {};
        }
        return text(json);
    }

private:
    const a2l::Module* moduleAt(int index) const {
        if (index < 0 || index >= document_.modules_size()) {
            return nullptr;
        }
        return &document_.modules(index);
    }

    QString entityName(const A2lPath& path) const {
        const auto* module = moduleAt(path.primaryIndex);
        if (!module) {
            return {};
        }
        int i = path.secondaryIndex;
        switch (path.kind) {
        case A2lEntityKind::Module:         return text(module->name());
        case A2lEntityKind::Measurement:    return (i >= 0 && i < module->measurements_size()) ? text(module->measurements(i).name()) : QString();
        case A2lEntityKind::Characteristic: return (i >= 0 && i < module->characteristics_size()) ? text(module->characteristics(i).name()) : QString();
        case A2lEntityKind::AxisPts:        return (i >= 0 && i < module->axis_points_size()) ? text(module->axis_points(i).name()) : QString();
        case A2lEntityKind::CompuMethod:    return (i >= 0 && i < module->compu_methods_size()) ? text(module->compu_methods(i).name()) : QString();
        case A2lEntityKind::RecordLayout:   return (i >= 0 && i < module->record_layouts_size()) ? text(module->record_layouts(i).name()) : QString();
        case A2lEntityKind::Unit:           return (i >= 0 && i < module->units_size()) ? text(module->units(i).name()) : QString();
        case A2lEntityKind::Function:       return (i >= 0 && i < module->functions_size()) ? text(module->functions(i).name()) : QString();
        case A2lEntityKind::Group:          return (i >= 0 && i < module->groups_size()) ? text(module->groups(i).name()) : QString();
        case A2lEntityKind::Instance:       return (i >= 0 && i < module->instances_size()) ? text(module->instances(i).name()) : QString();
        case A2lEntityKind::XcpSummary:     return QStringLiteral("XCP");
        case A2lEntityKind::CcpSummary:     return QStringLiteral("CCP");
        case A2lEntityKind::TypedefItem: {
            if (path.tertiaryIndex == kTypedefCharacteristicCategory && i >= 0 && i < module->typedef_characteristics_size())
                return text(module->typedef_characteristics(i).name());
            if (path.tertiaryIndex == kTypedefStructureCategory && i >= 0 && i < module->typedef_structures_size())
                return text(module->typedef_structures(i).name());
            if (path.tertiaryIndex == kTypedefAxisCategory && i >= 0 && i < module->typedef_axes_size())
                return text(module->typedef_axes(i).name());
            return {};
        }
        case A2lEntityKind::VariantCoding:  return QStringLiteral("Variant Coding");
        }
        return {};
    }

    QString buildBreadcrumb(const A2lPath& path) const {
        const auto* module = moduleAt(path.primaryIndex);
        if (!module) {
            return {};
        }
        QString moduleName = text(module->name());
        if (path.kind == A2lEntityKind::Module) {
            return moduleName;
        }
        QString section = a2lSectionName(path.kind);
        QString name = entityName(path);
        if (name.isEmpty()) {
            return QStringLiteral("%1 \u203A %2").arg(moduleName, section);
        }
        return QStringLiteral("%1 \u203A %2 \u203A %3").arg(moduleName, section, name);
    }

    const a2l::CompuMethod* findCompuMethod(const a2l::Module& module, const std::string& name) const {
        if (name.empty()) {
            return nullptr;
        }
        for (int i = 0; i < module.compu_methods_size(); ++i) {
            if (module.compu_methods(i).name() == name) {
                return &module.compu_methods(i);
            }
        }
        return nullptr;
    }

    void appendConversionSummary(QList<DetailSection>& sections, const A2lPath& path) const {
        const auto* module = moduleAt(path.primaryIndex);
        if (!module) {
            return;
        }

        std::string convRef;
        int sizeBytes = 0;

        if (path.kind == A2lEntityKind::Measurement) {
            int i = path.secondaryIndex;
            if (i >= 0 && i < module->measurements_size()) {
                const auto& m = module->measurements(i);
                convRef = m.conversion();
                sizeBytes = dataTypeSizeBytes(m.datatype());
            }
        } else if (path.kind == A2lEntityKind::Characteristic) {
            int i = path.secondaryIndex;
            if (i >= 0 && i < module->characteristics_size()) {
                convRef = module->characteristics(i).conversion();
            }
        } else if (path.kind == A2lEntityKind::AxisPts) {
            int i = path.secondaryIndex;
            if (i >= 0 && i < module->axis_points_size()) {
                convRef = module->axis_points(i).conversion_ref();
            }
        } else {
            return;
        }

        QList<DetailField> fields;
        const auto* cm = findCompuMethod(*module, convRef);
        if (cm) {
            addField(fields, QStringLiteral("Conversion Formula"), formatCompuMethodSummary(*cm));
            if (!cm->unit().empty()) {
                addField(fields, QStringLiteral("Physical Unit"), text(cm->unit()));
            }
        }
        if (sizeBytes > 0) {
            addNumberField(fields, QStringLiteral("Data Size (bytes)"), sizeBytes);
        }
        pushSection(sections, QStringLiteral("Resolved Conversion"), std::move(fields));
    }

    void appendCrossReferences(QList<DetailSection>& sections, const A2lPath& path) const {
        if (path.kind != A2lEntityKind::Measurement &&
            path.kind != A2lEntityKind::Characteristic) {
            return;
        }

        const auto* module = moduleAt(path.primaryIndex);
        if (!module) {
            return;
        }

        QString name = entityName(path);
        if (name.isEmpty()) {
            return;
        }
        std::string nameStd = name.toStdString();

        QStringList functionRefs;
        for (int i = 0; i < module->functions_size(); ++i) {
            const auto& fn = module->functions(i);
            bool found = false;
            if (path.kind == A2lEntityKind::Measurement) {
                for (const auto& ref : fn.in_measurements()) {
                    if (ref == nameStd) { found = true; break; }
                }
                if (!found) for (const auto& ref : fn.out_measurements()) {
                    if (ref == nameStd) { found = true; break; }
                }
                if (!found) for (const auto& ref : fn.loc_measurements()) {
                    if (ref == nameStd) { found = true; break; }
                }
            } else {
                for (const auto& ref : fn.def_characteristics()) {
                    if (ref == nameStd) { found = true; break; }
                }
                if (!found) for (const auto& ref : fn.ref_characteristics()) {
                    if (ref == nameStd) { found = true; break; }
                }
            }
            if (found) {
                functionRefs.push_back(text(fn.name()));
            }
        }

        QStringList groupRefs;
        for (int i = 0; i < module->groups_size(); ++i) {
            const auto& grp = module->groups(i);
            bool found = false;
            if (path.kind == A2lEntityKind::Measurement) {
                for (const auto& ref : grp.ref_measurements()) {
                    if (ref == nameStd) { found = true; break; }
                }
            } else {
                for (const auto& ref : grp.ref_characteristics()) {
                    if (ref == nameStd) { found = true; break; }
                }
            }
            if (found) {
                groupRefs.push_back(text(grp.name()));
            }
        }

        QList<DetailField> fields;
        if (!functionRefs.isEmpty()) {
            addField(fields,
                     QStringLiteral("Functions (%1)").arg(functionRefs.size()),
                     functionRefs.join(QStringLiteral(", ")));
        }
        if (!groupRefs.isEmpty()) {
            addField(fields,
                     QStringLiteral("Groups (%1)").arg(groupRefs.size()),
                     groupRefs.join(QStringLiteral(", ")));
        }
        pushSection(sections, QStringLiteral("Referenced By"), std::move(fields));
    }

    QList<DetailSection> moduleDetails(const A2lPath& path) const {
        QList<DetailSection> sections;
        const auto* module = moduleAt(path.primaryIndex);
        if (!module) {
            return sections;
        }

        QList<DetailField> identity;
        addField(identity, QStringLiteral("Name"), text(module->name()));
        addField(identity, QStringLiteral("Long Identifier"), text(module->long_identifier()));
        addNumberField(identity, QStringLiteral("Measurements"), module->measurements_size());
        addNumberField(identity, QStringLiteral("Characteristics"), module->characteristics_size());
        addNumberField(identity, QStringLiteral("Axis Points"), module->axis_points_size());
        addNumberField(identity, QStringLiteral("Compu Methods"), module->compu_methods_size());
        addNumberField(identity, QStringLiteral("Record Layouts"), module->record_layouts_size());
        addNumberField(identity, QStringLiteral("Units"), module->units_size());
        addNumberField(identity, QStringLiteral("Functions"), module->functions_size());
        addNumberField(identity, QStringLiteral("Groups"), module->groups_size());
        addNumberField(identity, QStringLiteral("Typedef Characteristics"), module->typedef_characteristics_size());
        addNumberField(identity, QStringLiteral("Typedef Structures"), module->typedef_structures_size());
        addNumberField(identity, QStringLiteral("Typedef Axes"), module->typedef_axes_size());
        addNumberField(identity, QStringLiteral("Instances"), module->instances_size());
        pushSection(sections, QStringLiteral("Identity"), std::move(identity));

        if (module->has_mod_common()) {
            const auto& modCommon = module->mod_common();
            QList<DetailField> defaults;
            addField(defaults,
                     QStringLiteral("Byte Order"),
                     text(a2l::ByteOrder_Name(modCommon.byte_order())));
            addOptionalNumber(defaults,
                              QStringLiteral("Data Size Bits"),
                              modCommon.has_data_size_bits(),
                              modCommon.data_size_bits());
            addField(defaults,
                     QStringLiteral("Deposit Mode"),
                     text(a2l::DepositMode_Name(modCommon.deposit_mode())));
            if (!modCommon.alignments().empty()) {
                addNumberField(defaults, QStringLiteral("Alignments"), modCommon.alignments().size());
            }
            pushSection(sections, QStringLiteral("Mod Common"), std::move(defaults));
        }

        if (module->has_mod_par()) {
            const auto& modPar = module->mod_par();
            QList<DetailField> parameters;
            addField(parameters, QStringLiteral("Comment"), text(modPar.comment()));
            addField(parameters, QStringLiteral("ECU"), text(modPar.ecu()));
            addField(parameters, QStringLiteral("Version"), text(modPar.version()));
            addField(parameters, QStringLiteral("Supplier"), text(modPar.supplier()));
            addField(parameters, QStringLiteral("CPU Type"), text(modPar.cpu_type()));
            addOptionalNumber(parameters,
                              QStringLiteral("ECU Calibration Offset"),
                              modPar.has_ecu_calibration_offset(),
                              modPar.ecu_calibration_offset());
            addNumberField(parameters, QStringLiteral("System Constants"), modPar.system_constants_size());
            addNumberField(parameters, QStringLiteral("Memory Segments"), modPar.memory_segments_size());
            pushSection(sections, QStringLiteral("Mod Par"), std::move(parameters));
        }

        QList<DetailField> protocolFields;
        addIfDataCounts(protocolFields, module->if_datas());
        if (module->has_variant_coding()) {
            addField(protocolFields, QStringLiteral("Variant Coding"), QStringLiteral("Present"));
        }
        pushSection(sections, QStringLiteral("Protocol / Variant"), std::move(protocolFields));
        return sections;
    }

    QList<DetailSection> measurementDetails(const A2lPath& path) const {
        QList<DetailSection> sections;
        const auto* module = moduleAt(path.primaryIndex);
        if (!module || path.secondaryIndex < 0 || path.secondaryIndex >= module->measurements_size()) {
            return sections;
        }

        const auto& measurement = module->measurements(path.secondaryIndex);
        QList<DetailField> identity;
        addField(identity, QStringLiteral("Name"), text(measurement.name()));
        addField(identity, QStringLiteral("Long Identifier"), text(measurement.long_identifier()));
        addField(identity,
                 QStringLiteral("Data Type"),
                 text(a2l::DataType_Name(measurement.datatype())));
        addField(identity, QStringLiteral("Conversion"), text(measurement.conversion()));
        addNumberField(identity, QStringLiteral("Resolution"), measurement.resolution());
        addNumberField(identity, QStringLiteral("Accuracy"), measurement.accuracy());
        pushSection(sections, QStringLiteral("Identity"), std::move(identity));

        QList<DetailField> range;
        addNumberField(range, QStringLiteral("Lower Limit"), measurement.lower_limit());
        addNumberField(range, QStringLiteral("Upper Limit"), measurement.upper_limit());
        addOptionalNumber(range,
                          QStringLiteral("ECU Address"),
                          measurement.has_ecu_address(),
                          measurement.ecu_address());
        addOptionalNumber(range,
                          QStringLiteral("ECU Address Extension"),
                          measurement.has_ecu_address_extension(),
                          measurement.ecu_address_extension());
        addField(range,
                 QStringLiteral("Byte Order"),
                 measurement.has_byte_order()
                     ? text(a2l::ByteOrder_Name(measurement.byte_order()))
                     : QString());
        pushSection(sections, QStringLiteral("Range / Address"), std::move(range));

        QList<DetailField> layout;
        addOptionalNumber(layout, QStringLiteral("Bit Mask"), measurement.has_bit_mask(), measurement.bit_mask());
        if (measurement.has_bit_operation()) {
            const auto& bitOp = measurement.bit_operation();
            QStringList ops;
            if (bitOp.has_left_shift())  ops << QStringLiteral("left_shift=%1").arg(bitOp.left_shift());
            if (bitOp.has_right_shift()) ops << QStringLiteral("right_shift=%1").arg(bitOp.right_shift());
            if (bitOp.has_sign_extend()) ops << QStringLiteral("sign_extend");
            addField(layout, QStringLiteral("Bit Operation"), ops.join(QStringLiteral(", ")));
        }
        addOptionalNumber(layout, QStringLiteral("Error Mask"), measurement.has_error_mask(), measurement.error_mask());
        addOptionalNumber(layout, QStringLiteral("Array Size"), measurement.has_array_size(), measurement.array_size());
        addField(layout, QStringLiteral("Matrix Dim"), joinNumbers(measurement.matrix_dim()));
        addField(layout,
                 QStringLiteral("Matrix Layout"),
                 measurement.has_layout()
                     ? text(a2l::MatrixLayout_Name(measurement.layout()))
                     : QString());
        addOptionalBool(layout, QStringLiteral("Discrete"), measurement.has_discrete(), measurement.discrete());
        addOptionalString(layout, QStringLiteral("Format"), measurement.has_format(), measurement.format());
        addOptionalString(layout, QStringLiteral("Physical Unit"), measurement.has_phys_unit(), measurement.phys_unit());
        addIfDataCounts(layout, measurement.if_datas());
        pushSection(sections, QStringLiteral("Layout / Display"), std::move(layout));
        return sections;
    }

    QList<DetailSection> characteristicDetails(const A2lPath& path) const {
        QList<DetailSection> sections;
        const auto* module = moduleAt(path.primaryIndex);
        if (!module || path.secondaryIndex < 0 || path.secondaryIndex >= module->characteristics_size()) {
            return sections;
        }

        const auto& characteristic = module->characteristics(path.secondaryIndex);
        QList<DetailField> identity;
        addField(identity, QStringLiteral("Name"), text(characteristic.name()));
        addField(identity, QStringLiteral("Long Identifier"), text(characteristic.long_identifier()));
        addField(identity,
                 QStringLiteral("Type"),
                 text(a2l::CharacteristicType_Name(characteristic.type())));
        addField(identity, QStringLiteral("Address"), hex64(characteristic.address()));
        addField(identity, QStringLiteral("Record Layout"), text(characteristic.record_layout_ref()));
        addField(identity, QStringLiteral("Conversion"), text(characteristic.conversion()));
        pushSection(sections, QStringLiteral("Identity"), std::move(identity));

        QList<DetailField> calibration;
        addNumberField(calibration, QStringLiteral("Max Diff"), characteristic.max_diff());
        addNumberField(calibration, QStringLiteral("Lower Limit"), characteristic.lower_limit());
        addNumberField(calibration, QStringLiteral("Upper Limit"), characteristic.upper_limit());
        addField(calibration,
                 QStringLiteral("Byte Order"),
                 characteristic.has_byte_order()
                     ? text(a2l::ByteOrder_Name(characteristic.byte_order()))
                     : QString());
        addOptionalNumber(calibration,
                          QStringLiteral("Bit Mask"),
                          characteristic.has_bit_mask(),
                          characteristic.bit_mask());
        addField(calibration, QStringLiteral("Matrix Dim"), joinNumbers(characteristic.matrix_dim()));
        addOptionalNumber(calibration,
                          QStringLiteral("Number"),
                          characteristic.has_number(),
                          characteristic.number());
        addOptionalString(calibration, QStringLiteral("Format"), characteristic.has_format(), characteristic.format());
        addOptionalString(calibration,
                          QStringLiteral("Physical Unit"),
                          characteristic.has_phys_unit(),
                          characteristic.phys_unit());
        addOptionalNumber(calibration,
                          QStringLiteral("ECU Address Extension"),
                          characteristic.has_ecu_address_extension(),
                          characteristic.ecu_address_extension());
        addNumberField(calibration, QStringLiteral("Axis Descriptions"), characteristic.axis_descrs_size());
        addIfDataCounts(calibration, characteristic.if_datas());
        pushSection(sections, QStringLiteral("Calibration"), std::move(calibration));
        return sections;
    }

    QList<DetailSection> axisPtsDetails(const A2lPath& path) const {
        QList<DetailSection> sections;
        const auto* module = moduleAt(path.primaryIndex);
        if (!module || path.secondaryIndex < 0 || path.secondaryIndex >= module->axis_points_size()) {
            return sections;
        }

        const auto& axis = module->axis_points(path.secondaryIndex);
        QList<DetailField> identity;
        addField(identity, QStringLiteral("Name"), text(axis.name()));
        addField(identity, QStringLiteral("Long Identifier"), text(axis.long_identifier()));
        addField(identity, QStringLiteral("Address"), hex64(axis.address()));
        addField(identity, QStringLiteral("Input Quantity"), text(axis.input_quantity_ref()));
        addField(identity, QStringLiteral("Record Layout"), text(axis.record_layout_ref()));
        addField(identity, QStringLiteral("Conversion"), text(axis.conversion_ref()));
        pushSection(sections, QStringLiteral("Identity"), std::move(identity));

        QList<DetailField> axisFields;
        addNumberField(axisFields, QStringLiteral("Max Diff"), axis.max_diff());
        addNumberField(axisFields, QStringLiteral("Max Axis Points"), axis.max_axis_points());
        addNumberField(axisFields, QStringLiteral("Lower Limit"), axis.lower_limit());
        addNumberField(axisFields, QStringLiteral("Upper Limit"), axis.upper_limit());
        addField(axisFields,
                 QStringLiteral("Byte Order"),
                 axis.has_byte_order()
                     ? text(a2l::ByteOrder_Name(axis.byte_order()))
                     : QString());
        addField(axisFields,
                 QStringLiteral("Calibration Access"),
                 axis.has_calibration_access()
                     ? text(a2l::CalibrationAccess_Name(axis.calibration_access()))
                     : QString());
        addField(axisFields,
                 QStringLiteral("Deposit Mode"),
                 axis.has_deposit_mode()
                     ? text(a2l::DepositMode_Name(axis.deposit_mode()))
                     : QString());
        addOptionalString(axisFields, QStringLiteral("Format"), axis.has_format(), axis.format());
        addOptionalString(axisFields, QStringLiteral("Physical Unit"), axis.has_phys_unit(), axis.phys_unit());
        addOptionalBool(axisFields, QStringLiteral("Guard Rails"), axis.has_guard_rails(), axis.guard_rails());
        addOptionalBool(axisFields, QStringLiteral("Read Only"), axis.has_read_only(), axis.read_only());
        addOptionalNumber(axisFields, QStringLiteral("Step Size"), axis.has_step_size(), axis.step_size());
        addField(axisFields,
                 QStringLiteral("Monotony"),
                 axis.has_monotony()
                     ? text(a2l::Monotony_Name(axis.monotony()))
                     : QString());
        addOptionalNumber(axisFields, QStringLiteral("ECU Address Extension"),
                          axis.has_ecu_address_extension(), axis.ecu_address_extension());
        if (axis.has_extended_limits()) {
            addField(axisFields,
                     QStringLiteral("Extended Limits"),
                     QStringLiteral("%1 .. %2")
                         .arg(numberText(axis.extended_limits().lower_limit()))
                         .arg(numberText(axis.extended_limits().upper_limit())));
        }
        addOptionalString(axisFields, QStringLiteral("Ref Memory Segment"),
                          axis.has_ref_memory_segment(), axis.ref_memory_segment());
        addOptionalString(axisFields, QStringLiteral("Display Identifier"),
                          axis.has_display_identifier(), axis.display_identifier());
        if (axis.has_symbol_link()) {
            addField(axisFields,
                     QStringLiteral("Symbol Link"),
                     QStringLiteral("%1 (offset %2)")
                         .arg(text(axis.symbol_link().symbol_name()))
                         .arg(axis.symbol_link().bit_offset()));
        }
        addOptionalString(axisFields, QStringLiteral("Model Link"),
                          axis.has_model_link(), axis.model_link());
        if (axis.has_max_refresh()) {
            addField(axisFields,
                     QStringLiteral("Max Refresh"),
                     QStringLiteral("unit=%1 rate=%2")
                         .arg(axis.max_refresh().scaling_unit())
                         .arg(axis.max_refresh().rate()));
        }
        addIfDataCounts(axisFields, axis.if_datas());
        pushSection(sections, QStringLiteral("Axis"), std::move(axisFields));
        return sections;
    }

    QList<DetailSection> compuMethodDetails(const A2lPath& path) const {
        QList<DetailSection> sections;
        const auto* module = moduleAt(path.primaryIndex);
        if (!module || path.secondaryIndex < 0 || path.secondaryIndex >= module->compu_methods_size()) {
            return sections;
        }

        const auto& method = module->compu_methods(path.secondaryIndex);
        QList<DetailField> identity;
        addField(identity, QStringLiteral("Name"), text(method.name()));
        addField(identity, QStringLiteral("Long Identifier"), text(method.long_identifier()));
        addField(identity,
                 QStringLiteral("Conversion Type"),
                 text(a2l::ConversionType_Name(method.conversion_type())));
        addField(identity, QStringLiteral("Format"), text(method.format()));
        addField(identity, QStringLiteral("Unit"), text(method.unit()));
        pushSection(sections, QStringLiteral("Identity"), std::move(identity));

        QList<DetailField> conversion;
        if (method.has_coeffs()) {
            const auto& coeffs = method.coeffs();
            addField(conversion,
                     QStringLiteral("Coeffs"),
                     QStringLiteral("a=%1 b=%2 c=%3 d=%4 e=%5 f=%6")
                         .arg(numberText(coeffs.a()))
                         .arg(numberText(coeffs.b()))
                         .arg(numberText(coeffs.c()))
                         .arg(numberText(coeffs.d()))
                         .arg(numberText(coeffs.e()))
                         .arg(numberText(coeffs.f())));
        }
        if (method.has_coeffs_linear()) {
            const auto& coeffs = method.coeffs_linear();
            addField(conversion,
                     QStringLiteral("Linear"),
                     QStringLiteral("a=%1 b=%2")
                         .arg(numberText(coeffs.a()))
                         .arg(numberText(coeffs.b())));
        }
        addOptionalString(conversion, QStringLiteral("Compu Tab Ref"), method.has_compu_tab_ref(), method.compu_tab_ref());
        addOptionalString(conversion, QStringLiteral("Ref Unit"), method.has_ref_unit(), method.ref_unit());
        addOptionalString(conversion,
                          QStringLiteral("Status String Ref"),
                          method.has_status_string_ref(),
                          method.status_string_ref());
        addOptionalString(conversion, QStringLiteral("Formula"), method.has_formula_raw(), method.formula_raw());
        pushSection(sections, QStringLiteral("Conversion"), std::move(conversion));
        return sections;
    }

    QList<DetailSection> recordLayoutDetails(const A2lPath& path) const {
        QList<DetailSection> sections;
        const auto* module = moduleAt(path.primaryIndex);
        if (!module || path.secondaryIndex < 0 || path.secondaryIndex >= module->record_layouts_size()) {
            return sections;
        }

        const auto& layout = module->record_layouts(path.secondaryIndex);
        pushSection(sections,
                    QStringLiteral("Identity"),
                    {
                        DetailField{QStringLiteral("Name"), text(layout.name())},
                        DetailField{QStringLiteral("Components"), QString::number(layout.components_size())},
                    });

        QList<DetailField> components;
        for (int i = 0; i < layout.components_size(); ++i) {
            components.push_back(DetailField{
                QStringLiteral("Component %1").arg(i + 1),
                recordLayoutComponentSummary(layout.components(i)),
            });
        }
        pushSection(sections, QStringLiteral("Components"), std::move(components));
        return sections;
    }

    QList<DetailSection> unitDetails(const A2lPath& path) const {
        QList<DetailSection> sections;
        const auto* module = moduleAt(path.primaryIndex);
        if (!module || path.secondaryIndex < 0 || path.secondaryIndex >= module->units_size()) {
            return sections;
        }

        const auto& unit = module->units(path.secondaryIndex);
        QList<DetailField> identity;
        addField(identity, QStringLiteral("Name"), text(unit.name()));
        addField(identity, QStringLiteral("Long Identifier"), text(unit.long_identifier()));
        addField(identity, QStringLiteral("Display"), text(unit.display()));
        addField(identity, QStringLiteral("Type"), text(a2l::UnitType_Name(unit.type())));
        addOptionalString(identity, QStringLiteral("Ref Unit"), unit.has_ref_unit(), unit.ref_unit());
        pushSection(sections, QStringLiteral("Identity"), std::move(identity));

        QList<DetailField> conversion;
        addField(conversion, QStringLiteral("SI Exponents"), joinIntegers(unit.si_exponents()));
        addOptionalNumber(conversion,
                          QStringLiteral("Gradient"),
                          unit.has_unit_conversion_gradient(),
                          unit.unit_conversion_gradient());
        addOptionalNumber(conversion,
                          QStringLiteral("Offset"),
                          unit.has_unit_conversion_offset(),
                          unit.unit_conversion_offset());
        pushSection(sections, QStringLiteral("Conversion"), std::move(conversion));
        return sections;
    }

    QList<DetailSection> functionDetails(const A2lPath& path) const {
        QList<DetailSection> sections;
        const auto* module = moduleAt(path.primaryIndex);
        if (!module || path.secondaryIndex < 0 || path.secondaryIndex >= module->functions_size()) {
            return sections;
        }

        const auto& function = module->functions(path.secondaryIndex);
        QList<DetailField> identity;
        addField(identity, QStringLiteral("Name"), text(function.name()));
        addField(identity, QStringLiteral("Long Identifier"), text(function.long_identifier()));
        addOptionalString(identity,
                          QStringLiteral("Version"),
                          function.has_function_version(),
                          function.function_version());
        pushSection(sections, QStringLiteral("Identity"), std::move(identity));

        QList<DetailField> references;
        addNumberField(references, QStringLiteral("Defined Characteristics"), function.def_characteristics_size());
        addNumberField(references, QStringLiteral("Referenced Characteristics"), function.ref_characteristics_size());
        addNumberField(references, QStringLiteral("Input Measurements"), function.in_measurements_size());
        addNumberField(references, QStringLiteral("Output Measurements"), function.out_measurements_size());
        addNumberField(references, QStringLiteral("Local Measurements"), function.loc_measurements_size());
        addNumberField(references, QStringLiteral("Sub Functions"), function.sub_functions_size());
        pushSection(sections, QStringLiteral("References"), std::move(references));
        return sections;
    }

    QList<DetailSection> groupDetails(const A2lPath& path) const {
        QList<DetailSection> sections;
        const auto* module = moduleAt(path.primaryIndex);
        if (!module || path.secondaryIndex < 0 || path.secondaryIndex >= module->groups_size()) {
            return sections;
        }

        const auto& group = module->groups(path.secondaryIndex);
        QList<DetailField> identity;
        addField(identity, QStringLiteral("Name"), text(group.name()));
        addField(identity, QStringLiteral("Long Identifier"), text(group.long_identifier()));
        addOptionalBool(identity, QStringLiteral("Root"), group.has_root(), group.root());
        pushSection(sections, QStringLiteral("Identity"), std::move(identity));

        QList<DetailField> references;
        addNumberField(references, QStringLiteral("Referenced Characteristics"), group.ref_characteristics_size());
        addNumberField(references, QStringLiteral("Referenced Measurements"), group.ref_measurements_size());
        addNumberField(references, QStringLiteral("Sub Groups"), group.sub_groups_size());
        addNumberField(references, QStringLiteral("Function List"), group.function_list_size());
        pushSection(sections, QStringLiteral("References"), std::move(references));
        return sections;
    }

    QList<DetailSection> xcpSummaryDetails(const A2lPath& path) const {
        QList<DetailSection> sections;
        const auto* module = moduleAt(path.primaryIndex);
        if (!module) {
            return sections;
        }

        for (const auto& ifData : module->if_datas()) {
            if (!ifData.has_xcp()) {
                continue;
            }

            const auto& xcp = ifData.xcp();

            // Protocol Layer
            if (xcp.has_protocol_layer()) {
                const auto& pl = xcp.protocol_layer();
                QList<DetailField> fields;
                addField(fields, QStringLiteral("Protocol Version"), hex64(pl.protocol_version()));
                addNumberField(fields, QStringLiteral("T1 (ms)"), pl.t1_ms());
                addNumberField(fields, QStringLiteral("T2 (ms)"), pl.t2_ms());
                addNumberField(fields, QStringLiteral("T3 (ms)"), pl.t3_ms());
                addNumberField(fields, QStringLiteral("T4 (ms)"), pl.t4_ms());
                addNumberField(fields, QStringLiteral("T5 (ms)"), pl.t5_ms());
                addNumberField(fields, QStringLiteral("T6 (ms)"), pl.t6_ms());
                addNumberField(fields, QStringLiteral("T7 (ms)"), pl.t7_ms());
                addNumberField(fields, QStringLiteral("MAX_CTO"), pl.max_cto());
                addNumberField(fields, QStringLiteral("MAX_DTO"), pl.max_dto());
                addField(fields, QStringLiteral("Byte Order"), text(a2l::ByteOrder_Name(pl.byte_order())));
                addField(fields, QStringLiteral("Address Granularity"), text(a2l::AddressGranularity_Name(pl.address_granularity())));
                if (pl.optional_cmds_size() > 0) {
                    addField(fields, QStringLiteral("Optional Commands"), joinNumbers(pl.optional_cmds()));
                }
                if (pl.optional_level1_cmds_size() > 0) {
                    addField(fields, QStringLiteral("Optional L1 Commands"), joinNumbers(pl.optional_level1_cmds()));
                }
                addOptionalNumber(fields, QStringLiteral("Comm Mode Supported"), pl.has_communication_mode_supported(), pl.communication_mode_supported());
                addOptionalString(fields, QStringLiteral("Seed & Key Function"), pl.has_seed_and_key_external_function(), pl.seed_and_key_external_function());
                addOptionalNumber(fields, QStringLiteral("MAX_DTO_STIM"), pl.has_max_dto_stim(), pl.max_dto_stim());
                pushSection(sections, QStringLiteral("Protocol Layer"), std::move(fields));
            }

            // DAQ
            if (xcp.has_daq()) {
                const auto& daq = xcp.daq();
                QList<DetailField> fields;
                addField(fields, QStringLiteral("Config Type"), text(a2l::DaqConfigType_Name(daq.daq_config_type())));
                addNumberField(fields, QStringLiteral("MAX_DAQ"), daq.max_daq());
                addNumberField(fields, QStringLiteral("MAX_EVENT_CHANNEL"), daq.max_event_channel());
                addNumberField(fields, QStringLiteral("MIN_DAQ"), daq.min_daq());
                addField(fields, QStringLiteral("Optimisation Type"), text(a2l::DaqOptimisationType_Name(daq.optimisation_type())));
                addField(fields, QStringLiteral("Address Extension"), text(a2l::DaqAddressExtensionMode_Name(daq.address_extension_mode())));
                addField(fields, QStringLiteral("ID Field Type"), text(a2l::IdentificationFieldType_Name(daq.identification_field_type())));
                addNumberField(fields, QStringLiteral("Granularity ODT Entry Size DAQ"), daq.granularity_odt_entry_size_daq());
                addNumberField(fields, QStringLiteral("MAX_ODT_ENTRY_SIZE_DAQ"), daq.max_odt_entry_size_daq());
                addField(fields, QStringLiteral("Overload Indication"), text(a2l::OverloadIndication_Name(daq.overload_indication())));
                addOptionalBool(fields, QStringLiteral("Prescaler Supported"), daq.has_prescaler_supported(), daq.prescaler_supported());
                addOptionalBool(fields, QStringLiteral("Resume Supported"), daq.has_resume_supported(), daq.resume_supported());
                addOptionalBool(fields, QStringLiteral("Store DAQ Supported"), daq.has_store_daq_supported(), daq.store_daq_supported());

                if (daq.has_stim()) {
                    addNumberField(fields, QStringLiteral("STIM Granularity"), daq.stim().granularity_odt_entry_size_stim());
                    addNumberField(fields, QStringLiteral("STIM MAX_ODT_ENTRY_SIZE"), daq.stim().max_odt_entry_size_stim());
                    addOptionalNumber(fields, QStringLiteral("STIM MIN_ST"), daq.stim().has_min_st_stim(), daq.stim().min_st_stim());
                }

                if (daq.has_timestamp_supported()) {
                    const auto& ts = daq.timestamp_supported();
                    addNumberField(fields, QStringLiteral("Timestamp Ticks"), ts.timestamp_ticks());
                    addField(fields, QStringLiteral("Timestamp Size"), text(a2l::TimestampSize_Name(ts.timestamp_size())));
                    addField(fields, QStringLiteral("Timestamp Unit"), text(a2l::TimestampUnit_Name(ts.timestamp_unit())));
                    addOptionalBool(fields, QStringLiteral("Timestamp Fixed"), ts.has_timestamp_fixed(), ts.timestamp_fixed());
                }

                if (daq.has_daq_memory_consumption()) {
                    const auto& mem = daq.daq_memory_consumption();
                    addOptionalNumber(fields, QStringLiteral("ODT Size"), mem.has_odt_size(), mem.odt_size());
                    addOptionalNumber(fields, QStringLiteral("ODT Entry Size"), mem.has_odt_entry_size(), mem.odt_entry_size());
                    addOptionalNumber(fields, QStringLiteral("DAQ Size"), mem.has_daq_size(), mem.daq_size());
                    addOptionalNumber(fields, QStringLiteral("DAQ Memory Limit"), mem.has_daq_memory_limit(), mem.daq_memory_limit());
                }

                pushSection(sections, QStringLiteral("DAQ"), std::move(fields));

                // DAQ Lists
                for (int i = 0; i < daq.daq_lists_size(); ++i) {
                    const auto& list = daq.daq_lists(i);
                    QList<DetailField> listFields;
                    addNumberField(listFields, QStringLiteral("DAQ List Number"), list.daq_list_number());
                    addNumberField(listFields, QStringLiteral("DAQ List Type"), list.daq_list_type());
                    addNumberField(listFields, QStringLiteral("MAX_ODT"), list.max_odt());
                    addNumberField(listFields, QStringLiteral("MAX_ODT_ENTRIES"), list.max_odt_entries());
                    addOptionalNumber(listFields, QStringLiteral("First PID"), list.has_first_pid(), list.first_pid());
                    addOptionalNumber(listFields, QStringLiteral("Event Fixed"), list.has_event_fixed(), list.event_fixed());
                    pushSection(sections, QStringLiteral("DAQ List #%1").arg(list.daq_list_number()), std::move(listFields));
                }

                // Event Channels
                for (int i = 0; i < daq.events_size(); ++i) {
                    const auto& ev = daq.events(i);
                    QList<DetailField> evFields;
                    addField(evFields, QStringLiteral("Name"), text(ev.name()));
                    addField(evFields, QStringLiteral("Short Name"), text(ev.short_name()));
                    addNumberField(evFields, QStringLiteral("Channel Number"), ev.event_channel_number());
                    addNumberField(evFields, QStringLiteral("DAQ List Type"), ev.daq_list_type());
                    addNumberField(evFields, QStringLiteral("MAX_DAQ_LIST"), ev.max_daq_list());
                    addNumberField(evFields, QStringLiteral("Time Cycle"), ev.time_cycle());
                    addNumberField(evFields, QStringLiteral("Time Unit"), ev.time_unit());
                    addNumberField(evFields, QStringLiteral("Priority"), ev.priority());
                    addOptionalNumber(evFields, QStringLiteral("Consistency"), ev.has_consistency(), ev.consistency());
                    pushSection(sections, QStringLiteral("Event \"%1\" (#%2)").arg(text(ev.short_name())).arg(ev.event_channel_number()), std::move(evFields));
                }
            }

            // CAN Transports
            for (int i = 0; i < xcp.xcp_on_cans_size(); ++i) {
                const auto& can = xcp.xcp_on_cans(i);
                QList<DetailField> fields;
                addField(fields, QStringLiteral("Transport Version"), hex64(can.transport_version()));
                addOptionalNumber(fields, QStringLiteral("CAN ID Broadcast"), can.has_can_id_broadcast(), can.can_id_broadcast());
                addOptionalNumber(fields, QStringLiteral("CAN ID Master"), can.has_can_id_master(), can.can_id_master());
                addOptionalBool(fields, QStringLiteral("Master Incremental"), can.has_can_id_master_incremental(), can.can_id_master_incremental());
                addOptionalNumber(fields, QStringLiteral("CAN ID Slave"), can.has_can_id_slave(), can.can_id_slave());
                addOptionalNumber(fields, QStringLiteral("CAN ID DAQ Clock Multicast"), can.has_can_id_get_daq_clock_multicast(), can.can_id_get_daq_clock_multicast());
                addOptionalNumber(fields, QStringLiteral("Baudrate"), can.has_baudrate(), can.baudrate());
                addOptionalNumber(fields, QStringLiteral("Sample Point"), can.has_sample_point(), can.sample_point());
                if (can.has_sample_rate()) {
                    addField(fields, QStringLiteral("Sample Rate"), text(a2l::SampleRate_Name(can.sample_rate())));
                }
                addOptionalNumber(fields, QStringLiteral("BTL Cycles"), can.has_btl_cycles(), can.btl_cycles());
                addOptionalNumber(fields, QStringLiteral("SJW"), can.has_sjw(), can.sjw());
                addOptionalNumber(fields, QStringLiteral("MAX_DLC_REQUIRED"), can.has_max_dlc_required(), can.max_dlc_required());
                addOptionalNumber(fields, QStringLiteral("MAX_BUS_LOAD"), can.has_max_bus_load(), can.max_bus_load());
                addOptionalBool(fields, QStringLiteral("Measurement Split Allowed"), can.has_measurement_split_allowed(), can.measurement_split_allowed());
                addOptionalString(fields, QStringLiteral("Transport Layer Instance"), can.has_transport_layer_instance(), can.transport_layer_instance());

                if (can.has_can_fd()) {
                    const auto& fd = can.can_fd();
                    addOptionalNumber(fields, QStringLiteral("CAN-FD MAX_DLC"), fd.has_max_dlc(), fd.max_dlc());
                    addOptionalNumber(fields, QStringLiteral("CAN-FD Data Baudrate"), fd.has_can_fd_data_transfer_baudrate(), fd.can_fd_data_transfer_baudrate());
                    addOptionalNumber(fields, QStringLiteral("CAN-FD Sample Point"), fd.has_sample_point(), fd.sample_point());
                    addOptionalNumber(fields, QStringLiteral("CAN-FD BTL Cycles"), fd.has_btl_cycles(), fd.btl_cycles());
                    addOptionalNumber(fields, QStringLiteral("CAN-FD SJW"), fd.has_sjw(), fd.sjw());
                }

                if (can.daq_list_can_ids_size() > 0) {
                    for (const auto& dlc : can.daq_list_can_ids()) {
                        if (dlc.has_fixed_can_id()) {
                            addField(fields, QStringLiteral("DAQ List #%1 CAN ID").arg(dlc.daq_list_number()), hex64(dlc.fixed_can_id()));
                        } else if (dlc.has_variable_can_id()) {
                            addField(fields, QStringLiteral("DAQ List #%1 CAN ID").arg(dlc.daq_list_number()), QStringLiteral("variable"));
                        }
                    }
                }

                pushSection(sections, QStringLiteral("XCP on CAN"), std::move(fields));
            }

            // TCP Transports
            for (int i = 0; i < xcp.xcp_on_tcp_ips_size(); ++i) {
                const auto& ip = xcp.xcp_on_tcp_ips(i);
                QList<DetailField> fields;
                addField(fields, QStringLiteral("Transport Version"), hex64(ip.transport_version()));
                addNumberField(fields, QStringLiteral("Port"), ip.port());
                if (ip.has_host_name()) addField(fields, QStringLiteral("Host"), text(ip.host_name()));
                if (ip.has_address()) addField(fields, QStringLiteral("Address"), text(ip.address()));
                if (ip.has_ipv6()) addField(fields, QStringLiteral("IPv6"), text(ip.ipv6()));
                addOptionalNumber(fields, QStringLiteral("MAX_BUS_LOAD"), ip.has_max_bus_load(), ip.max_bus_load());
                addOptionalNumber(fields, QStringLiteral("MAX_BIT_RATE"), ip.has_max_bit_rate(), ip.max_bit_rate());
                addOptionalNumber(fields, QStringLiteral("Packet Alignment"), ip.has_packet_alignment(), ip.packet_alignment());
                addOptionalString(fields, QStringLiteral("Transport Layer Instance"), ip.has_transport_layer_instance(), ip.transport_layer_instance());
                pushSection(sections, QStringLiteral("XCP on TCP/IP"), std::move(fields));
            }

            // UDP Transports
            for (int i = 0; i < xcp.xcp_on_udp_ips_size(); ++i) {
                const auto& ip = xcp.xcp_on_udp_ips(i);
                QList<DetailField> fields;
                addField(fields, QStringLiteral("Transport Version"), hex64(ip.transport_version()));
                addNumberField(fields, QStringLiteral("Port"), ip.port());
                if (ip.has_host_name()) addField(fields, QStringLiteral("Host"), text(ip.host_name()));
                if (ip.has_address()) addField(fields, QStringLiteral("Address"), text(ip.address()));
                if (ip.has_ipv6()) addField(fields, QStringLiteral("IPv6"), text(ip.ipv6()));
                addOptionalNumber(fields, QStringLiteral("MAX_BUS_LOAD"), ip.has_max_bus_load(), ip.max_bus_load());
                addOptionalNumber(fields, QStringLiteral("MAX_BIT_RATE"), ip.has_max_bit_rate(), ip.max_bit_rate());
                addOptionalNumber(fields, QStringLiteral("Packet Alignment"), ip.has_packet_alignment(), ip.packet_alignment());
                addOptionalString(fields, QStringLiteral("Transport Layer Instance"), ip.has_transport_layer_instance(), ip.transport_layer_instance());
                pushSection(sections, QStringLiteral("XCP on UDP/IP"), std::move(fields));
            }

            // Segments
            for (int i = 0; i < xcp.segments_size(); ++i) {
                const auto& seg = xcp.segments(i);
                QList<DetailField> segFields;
                addNumberField(segFields, QStringLiteral("Segment Number"), seg.segment_number());
                addNumberField(segFields, QStringLiteral("Page Count"), seg.page_count());
                addNumberField(segFields, QStringLiteral("Address Extension"), seg.address_extension());
                addNumberField(segFields, QStringLiteral("Compression Method"), seg.compression_method());
                addNumberField(segFields, QStringLiteral("Encryption Method"), seg.encryption_method());
                addOptionalNumber(segFields, QStringLiteral("Default Page Number"), seg.has_default_page_number(), seg.default_page_number());

                if (seg.has_checksum()) {
                    const auto& cs = seg.checksum();
                    addField(segFields, QStringLiteral("Checksum Type"), text(a2l::ChecksumType_Name(cs.type())));
                    addOptionalNumber(segFields, QStringLiteral("Checksum MAX_BLOCK_SIZE"), cs.has_max_block_size(), cs.max_block_size());
                    addOptionalString(segFields, QStringLiteral("Checksum External Function"), cs.has_external_function(), cs.external_function());
                }

                for (int p = 0; p < seg.pages_size(); ++p) {
                    const auto& page = seg.pages(p);
                    addNumberField(segFields, QStringLiteral("Page %1 Number").arg(p), page.page_number());
                    addField(segFields, QStringLiteral("Page %1 ECU Access").arg(p), text(page.ecu_access()));
                    addField(segFields, QStringLiteral("Page %1 XCP Access").arg(p), text(page.xcp_access()));
                    addOptionalBool(segFields, QStringLiteral("Page %1 Init Segment").arg(p), page.has_init_segment(), page.init_segment());
                }

                for (int m = 0; m < seg.address_mappings_size(); ++m) {
                    const auto& mapping = seg.address_mappings(m);
                    addField(segFields, QStringLiteral("Mapping %1").arg(m),
                             QStringLiteral("%1 -> %2 (len %3)")
                                 .arg(hex64(mapping.mapping_address()))
                                 .arg(hex64(mapping.original_address()))
                                 .arg(mapping.length()));
                }

                pushSection(sections, QStringLiteral("Segment #%1").arg(seg.segment_number()), std::move(segFields));
            }

            // DAQ Event binding (measurement-level)
            if (xcp.has_daq_event()) {
                const auto& daqEv = xcp.daq_event();
                QList<DetailField> fields;
                if (daqEv.has_fixed_event_list()) {
                    addField(fields, QStringLiteral("Type"), QStringLiteral("Fixed Event List"));
                    addField(fields, QStringLiteral("Event Numbers"), joinNumbers(daqEv.fixed_event_list().event_numbers()));
                }
                if (daqEv.has_variable_event_list()) {
                    addField(fields, QStringLiteral("Type"), QStringLiteral("Variable Event List"));
                    if (daqEv.variable_event_list().available_event_list_size() > 0) {
                        addField(fields, QStringLiteral("Available Events"), joinNumbers(daqEv.variable_event_list().available_event_list()));
                    }
                    if (daqEv.variable_event_list().default_event_list_size() > 0) {
                        addField(fields, QStringLiteral("Default Events"), joinNumbers(daqEv.variable_event_list().default_event_list()));
                    }
                }
                pushSection(sections, QStringLiteral("DAQ Event Binding"), std::move(fields));
            }

            // Raw blocks
            if (xcp.raw_blocks_size() > 0) {
                QList<DetailField> fields;
                for (int i = 0; i < xcp.raw_blocks_size(); ++i) {
                    addField(fields, QStringLiteral("Raw Block %1").arg(i), text(xcp.raw_blocks(i)));
                }
                pushSection(sections, QStringLiteral("Raw Blocks"), std::move(fields));
            }
        }

        return sections;
    }

    QList<DetailSection> ccpSummaryDetails(const A2lPath& path) const {
        QList<DetailSection> sections;
        const auto* module = moduleAt(path.primaryIndex);
        if (!module) {
            return sections;
        }

        for (const auto& ifData : module->if_datas()) {
            if (!ifData.has_ccp()) {
                continue;
            }

            const auto& ccp = ifData.ccp();

            // TP_BLOB
            if (ccp.has_tp_blob()) {
                const auto& tp = ccp.tp_blob();
                QList<DetailField> fields;
                addField(fields, QStringLiteral("CCP Version"), hex64(tp.ccp_version()));
                addField(fields, QStringLiteral("BLOB Version"), hex64(tp.blob_version()));
                addField(fields, QStringLiteral("CRM CAN ID"), hex64(tp.crm_can_id()));
                addField(fields, QStringLiteral("DTO CAN ID"), hex64(tp.dto_can_id()));
                addNumberField(fields, QStringLiteral("Station Address"), tp.station_address());
                addField(fields, QStringLiteral("Byte Order"), text(a2l::ByteOrder_Name(tp.byte_order())));
                addOptionalNumber(fields, QStringLiteral("Baudrate"), tp.has_baudrate(), tp.baudrate());
                addOptionalNumber(fields, QStringLiteral("Sample Point"), tp.has_sample_point(), tp.sample_point());
                addOptionalNumber(fields, QStringLiteral("Sample Rate"), tp.has_sample_rate(), tp.sample_rate());
                addOptionalNumber(fields, QStringLiteral("BTL Cycles"), tp.has_btl_cycles(), tp.btl_cycles());
                addOptionalNumber(fields, QStringLiteral("SJW"), tp.has_sjw(), tp.sjw());
                if (tp.has_sync_edge()) {
                    addField(fields, QStringLiteral("Sync Edge"), text(a2l::CcpSyncEdge_Name(tp.sync_edge())));
                }
                if (tp.has_daq_mode()) {
                    addField(fields, QStringLiteral("DAQ Mode"), text(a2l::CcpDaqMode_Name(tp.daq_mode())));
                }
                if (tp.has_consistency()) {
                    addField(fields, QStringLiteral("Consistency"), text(a2l::CcpConsistency_Name(tp.consistency())));
                }
                if (tp.has_address_extension()) {
                    addField(fields, QStringLiteral("Address Extension"), text(a2l::CcpAddressExtensionMode_Name(tp.address_extension())));
                }
                if (tp.optional_cmds_size() > 0) {
                    addField(fields, QStringLiteral("Optional Commands"), joinNumbers(tp.optional_cmds()));
                }

                // Defined Pages
                for (int p = 0; p < tp.defined_pages_size(); ++p) {
                    const auto& page = tp.defined_pages(p);
                    addNumberField(fields, QStringLiteral("Page %1 Number").arg(p), page.page_number());
                    addField(fields, QStringLiteral("Page %1 Name").arg(p), text(page.name()));
                    addNumberField(fields, QStringLiteral("Page %1 Address Extension").arg(p), page.address_extension());
                    addField(fields, QStringLiteral("Page %1 Address").arg(p), hex64(page.address()));
                    addNumberField(fields, QStringLiteral("Page %1 Size").arg(p), page.size());
                    addField(fields, QStringLiteral("Page %1 Memory Type").arg(p), text(a2l::CcpPageMemoryType_Name(page.memory_type())));
                    addOptionalBool(fields, QStringLiteral("Page %1 RAM Init by ECU").arg(p), page.has_ram_init_by_ecu(), page.ram_init_by_ecu());
                    addOptionalBool(fields, QStringLiteral("Page %1 RAM Init by Tool").arg(p), page.has_ram_init_by_tool(), page.ram_init_by_tool());
                    addOptionalBool(fields, QStringLiteral("Page %1 Default").arg(p), page.has_default_page(), page.default_page());
                }

                if (tp.has_page_switch_method()) {
                    const auto& psm = tp.page_switch_method();
                    addNumberField(fields, QStringLiteral("Page Switch Version"), psm.version());
                    if (psm.has_autostart_behavior()) {
                        addField(fields, QStringLiteral("Autostart"), text(a2l::CcpAutostartBehavior_Name(psm.autostart_behavior())));
                    }
                    if (psm.has_mailbox()) {
                        addNumberField(fields, QStringLiteral("Mailbox Version"), psm.mailbox().mailbox_version());
                        addNumberField(fields, QStringLiteral("Mailbox Setup Time (ms)"), psm.mailbox().page_setup_time_ms());
                        addField(fields, QStringLiteral("Mailbox Start Address"), hex64(psm.mailbox().start_address()));
                    }
                }

                pushSection(sections, QStringLiteral("TP_BLOB"), std::move(fields));
            }

            // Sources
            for (int i = 0; i < ccp.sources_size(); ++i) {
                const auto& src = ccp.sources(i);
                QList<DetailField> fields;
                addField(fields, QStringLiteral("Name"), text(src.name()));
                addNumberField(fields, QStringLiteral("Scaling Unit"), src.scaling_unit());
                addNumberField(fields, QStringLiteral("Rate"), src.rate());

                if (src.has_qp_blob()) {
                    const auto& qp = src.qp_blob();
                    addNumberField(fields, QStringLiteral("QP DAQ List Number"), qp.daq_list_number());
                    addNumberField(fields, QStringLiteral("QP Length"), qp.length());
                    addField(fields, QStringLiteral("QP CAN ID Mode"), text(a2l::CcpCanIdMode_Name(qp.can_id_mode())));
                    addOptionalNumber(fields, QStringLiteral("QP Fixed CAN ID"), qp.has_fixed_can_id(), qp.fixed_can_id());
                    addOptionalNumber(fields, QStringLiteral("QP First PID"), qp.has_first_pid(), qp.first_pid());
                    addOptionalBool(fields, QStringLiteral("QP Reduction Allowed"), qp.has_reduction_allowed(), qp.reduction_allowed());
                    if (qp.raster_ids_size() > 0) {
                        addField(fields, QStringLiteral("QP Raster IDs"), joinNumbers(qp.raster_ids()));
                    }
                }

                pushSection(sections, QStringLiteral("Source \"%1\"").arg(text(src.name())), std::move(fields));
            }

            // Rasters
            for (int i = 0; i < ccp.rasters_size(); ++i) {
                const auto& raster = ccp.rasters(i);
                QList<DetailField> fields;
                addField(fields, QStringLiteral("Name"), text(raster.name()));
                addField(fields, QStringLiteral("Short Name"), text(raster.short_name()));
                addNumberField(fields, QStringLiteral("Raster ID"), raster.raster_id());
                addNumberField(fields, QStringLiteral("Scaling Unit"), raster.scaling_unit());
                addNumberField(fields, QStringLiteral("Rate"), raster.rate());
                pushSection(sections, QStringLiteral("Raster \"%1\" (#%2)").arg(text(raster.short_name())).arg(raster.raster_id()), std::move(fields));
            }

            // Event Groups
            for (int i = 0; i < ccp.event_groups_size(); ++i) {
                const auto& eg = ccp.event_groups(i);
                QList<DetailField> fields;
                addField(fields, QStringLiteral("Name"), text(eg.name()));
                addField(fields, QStringLiteral("Short Name"), text(eg.short_name()));
                if (eg.raster_ids_size() > 0) {
                    addField(fields, QStringLiteral("Raster IDs"), joinNumbers(eg.raster_ids()));
                }
                pushSection(sections, QStringLiteral("Event Group \"%1\"").arg(text(eg.short_name())), std::move(fields));
            }

            // KP_BLOB
            if (ccp.has_kp_blob()) {
                const auto& kp = ccp.kp_blob();
                QList<DetailField> fields;
                addNumberField(fields, QStringLiteral("Address Extension"), kp.address_extension());
                addField(fields, QStringLiteral("Address"), hex64(kp.address()));
                addNumberField(fields, QStringLiteral("Size"), kp.size());
                if (kp.raster_ids_size() > 0) {
                    addField(fields, QStringLiteral("Raster IDs"), joinNumbers(kp.raster_ids()));
                }
                pushSection(sections, QStringLiteral("KP_BLOB"), std::move(fields));
            }

            // Raw fallback blocks
            QList<DetailField> rawFields;
            addOptionalString(rawFields, QStringLiteral("Seed & Key"), ccp.has_seed_key_raw(), ccp.seed_key_raw());
            addOptionalString(rawFields, QStringLiteral("Checksum"), ccp.has_checksum_raw(), ccp.checksum_raw());
            addOptionalString(rawFields, QStringLiteral("DP_BLOB"), ccp.has_dp_blob_raw(), ccp.dp_blob_raw());
            addOptionalString(rawFields, QStringLiteral("Address Mapping"), ccp.has_addr_mapping_raw(), ccp.addr_mapping_raw());
            for (int i = 0; i < ccp.raw_blocks_size(); ++i) {
                addField(rawFields, QStringLiteral("Raw Block %1").arg(i), text(ccp.raw_blocks(i)));
            }
            pushSection(sections, QStringLiteral("Raw / Deferred"), std::move(rawFields));
        }

        return sections;
    }

    QList<DetailSection> typedefDetails(const A2lPath& path) const {
        QList<DetailSection> sections;
        const auto* module = moduleAt(path.primaryIndex);
        if (!module || path.secondaryIndex < 0) {
            return sections;
        }

        if (path.tertiaryIndex == kTypedefCharacteristicCategory) {
            if (path.secondaryIndex >= module->typedef_characteristics_size()) {
                return sections;
            }

            const auto& item = module->typedef_characteristics(path.secondaryIndex);
            QList<DetailField> fields;
            addField(fields, QStringLiteral("Name"), text(item.name()));
            addField(fields, QStringLiteral("Long Identifier"), text(item.long_identifier()));
            addField(fields,
                     QStringLiteral("Type"),
                     text(a2l::CharacteristicType_Name(item.type())));
            addField(fields, QStringLiteral("Record Layout"), text(item.record_layout_ref()));
            addField(fields, QStringLiteral("Conversion"), text(item.conversion()));
            addNumberField(fields, QStringLiteral("Axis Descriptions"), item.axis_descrs_size());
            addOptionalString(fields, QStringLiteral("Format"), item.has_format(), item.format());
            addOptionalString(fields, QStringLiteral("Physical Unit"), item.has_phys_unit(), item.phys_unit());
            pushSection(sections, QStringLiteral("Typedef Characteristic"), std::move(fields));
            return sections;
        }

        if (path.tertiaryIndex == kTypedefStructureCategory) {
            if (path.secondaryIndex >= module->typedef_structures_size()) {
                return sections;
            }

            const auto& item = module->typedef_structures(path.secondaryIndex);
            QList<DetailField> fields;
            addField(fields, QStringLiteral("Name"), text(item.name()));
            addField(fields, QStringLiteral("Long Identifier"), text(item.long_identifier()));
            addNumberField(fields, QStringLiteral("Total Size"), item.total_size());
            addNumberField(fields, QStringLiteral("Components"), item.components_size());
            pushSection(sections, QStringLiteral("Typedef Structure"), std::move(fields));
            return sections;
        }

        if (path.tertiaryIndex == kTypedefAxisCategory) {
            if (path.secondaryIndex >= module->typedef_axes_size()) {
                return sections;
            }

            const auto& item = module->typedef_axes(path.secondaryIndex);
            QList<DetailField> fields;
            addField(fields, QStringLiteral("Name"), text(item.name()));
            addField(fields, QStringLiteral("Long Identifier"), text(item.long_identifier()));
            addField(fields, QStringLiteral("Input Quantity"), text(item.input_quantity()));
            addField(fields, QStringLiteral("Record Layout"), text(item.record_layout_ref()));
            addField(fields, QStringLiteral("Conversion"), text(item.conversion()));
            addNumberField(fields, QStringLiteral("Max Axis Points"), item.max_axis_points());
            addOptionalString(fields, QStringLiteral("Format"), item.has_format(), item.format());
            addOptionalString(fields, QStringLiteral("Physical Unit"), item.has_phys_unit(), item.phys_unit());
            pushSection(sections, QStringLiteral("Typedef Axis"), std::move(fields));
        }

        return sections;
    }

    QList<DetailSection> instanceDetails(const A2lPath& path) const {
        QList<DetailSection> sections;
        const auto* module = moduleAt(path.primaryIndex);
        if (!module || path.secondaryIndex < 0 || path.secondaryIndex >= module->instances_size()) {
            return sections;
        }

        const auto& instance = module->instances(path.secondaryIndex);
        QList<DetailField> identity;
        addField(identity, QStringLiteral("Name"), text(instance.name()));
        addField(identity, QStringLiteral("Long Identifier"), text(instance.long_identifier()));
        addField(identity, QStringLiteral("Type Ref"), text(instance.type_ref()));
        addField(identity, QStringLiteral("Address"), hex64(instance.address()));
        addOptionalString(identity,
                          QStringLiteral("Display Identifier"),
                          instance.has_display_identifier(),
                          instance.display_identifier());
        pushSection(sections, QStringLiteral("Identity"), std::move(identity));

        QList<DetailField> properties;
        addField(properties, QStringLiteral("Matrix Dim"), joinNumbers(instance.matrix_dim()));
        addOptionalNumber(properties,
                          QStringLiteral("ECU Address Extension"),
                          instance.has_ecu_address_extension(),
                          instance.ecu_address_extension());
        addOptionalBool(properties, QStringLiteral("Read Only"), instance.has_read_only(), instance.read_only());
        addIfDataCounts(properties, instance.if_datas());
        pushSection(sections, QStringLiteral("Properties"), std::move(properties));
        return sections;
    }

    QList<DetailSection> variantCodingDetails(const A2lPath& path) const {
        QList<DetailSection> sections;
        const auto* module = moduleAt(path.primaryIndex);
        if (!module || !module->has_variant_coding()) {
            return sections;
        }

        const auto& variant = module->variant_coding();
        QList<DetailField> fields;
        addOptionalString(fields, QStringLiteral("Separator"), variant.has_var_separator(), variant.var_separator());
        addField(fields,
                 QStringLiteral("Naming"),
                 variant.has_var_naming() ? text(a2l::VarNaming_Name(variant.var_naming())) : QString());
        addNumberField(fields, QStringLiteral("Characteristics"), variant.var_characteristics_size());
        addNumberField(fields, QStringLiteral("Criteria"), variant.var_criteria_size());
        addNumberField(fields, QStringLiteral("Forbidden Combinations"), variant.var_forbidden_combs_size());
        pushSection(sections, QStringLiteral("Variant Coding"), std::move(fields));
        return sections;
    }

    const a2l::A2lFile& document_;
};
} // namespace

A2lDocumentSession::A2lDocumentSession(QString displayName,
                                       QString sourcePath,
                                       a2l::A2lFile document,
                                       QList<DiagnosticMessage> diagnostics)
    : AdapterSessionBase(FormatId::A2L,
                         QStringLiteral("A2L"),
                         std::move(displayName),
                         std::move(sourcePath),
                         std::move(diagnostics)),
      document_(std::move(document)) {
    setDetailPresenter(std::make_unique<A2lDetailPresenter>(document_));
    buildTree();
    buildMemoryMap();
}

A2lDocumentSession::~A2lDocumentSession() = default;

QUrl A2lDocumentSession::centerPanelSource() const {
    if (memoryMapModel_ && memoryMapModel_->totalObjectCount() > 0) {
        return QUrl(QStringLiteral("qrc:/qt/qml/ExplorerApp/qml/components/MemoryView.qml"));
    }
    return {};
}

QAbstractListModel* A2lDocumentSession::centerPanelModel() {
    return memoryMapModel_.get();
}

void A2lDocumentSession::moveModelsToThread(QThread* thread) {
    AdapterSessionBase::moveModelsToThread(thread);
    if (memoryMapModel_)
        memoryMapModel_->moveToThread(thread);
}

namespace {

// DataType -> byte size lookup.
uint64_t dataTypeSize(a2l::DataType dt) {
    switch (dt) {
    case a2l::DATA_TYPE_UBYTE:
    case a2l::DATA_TYPE_SBYTE:
        return 1;
    case a2l::DATA_TYPE_UWORD:
    case a2l::DATA_TYPE_SWORD:
    case a2l::DATA_TYPE_FLOAT16_IEEE:
        return 2;
    case a2l::DATA_TYPE_ULONG:
    case a2l::DATA_TYPE_SLONG:
    case a2l::DATA_TYPE_FLOAT32_IEEE:
        return 4;
    case a2l::DATA_TYPE_A_UINT64:
    case a2l::DATA_TYPE_A_INT64:
    case a2l::DATA_TYPE_FLOAT64_IEEE:
        return 8;
    default:
        return 0;
    }
}

// Color index mapping for object types.
// 0=VALUE, 1=CURVE, 2=MAP, 3=CUBOID+, 4=ASCII, 5=VAL_BLK, 6=MEASUREMENT, 7=AXIS_PTS
int characteristicColorIndex(a2l::CharacteristicType type) {
    switch (type) {
    case a2l::CHARACTERISTIC_TYPE_VALUE:   return 0;
    case a2l::CHARACTERISTIC_TYPE_CURVE:   return 1;
    case a2l::CHARACTERISTIC_TYPE_MAP:     return 2;
    case a2l::CHARACTERISTIC_TYPE_CUBOID:
    case a2l::CHARACTERISTIC_TYPE_CUBE_4:
    case a2l::CHARACTERISTIC_TYPE_CUBE_5:  return 3;
    case a2l::CHARACTERISTIC_TYPE_ASCII:   return 4;
    case a2l::CHARACTERISTIC_TYPE_VAL_BLK: return 5;
    default:                               return 0;
    }
}

struct RecordLayoutInfo {
    a2l::DataType functionValuesType = a2l::DATA_TYPE_UNSPECIFIED;
    a2l::DataType axisXType = a2l::DATA_TYPE_UNSPECIFIED;
    a2l::DataType axisYType = a2l::DATA_TYPE_UNSPECIFIED;
    a2l::DataType identType = a2l::DATA_TYPE_UNSPECIFIED;
    a2l::DataType axisCountXType = a2l::DATA_TYPE_UNSPECIFIED;
    a2l::DataType axisCountYType = a2l::DATA_TYPE_UNSPECIFIED;
    bool hasAlternateMode = false;
};

RecordLayoutInfo analyzeRecordLayout(const a2l::RecordLayout& rl) {
    RecordLayoutInfo info;
    for (const auto& comp : rl.components()) {
        if (comp.has_function_values()) {
            info.functionValuesType = comp.function_values().datatype();
            auto mode = comp.function_values().index_mode();
            info.hasAlternateMode =
                (mode == a2l::RECORD_LAYOUT_INDEX_MODE_ALTERNATE_CURVES ||
                 mode == a2l::RECORD_LAYOUT_INDEX_MODE_ALTERNATE_WITH_X ||
                 mode == a2l::RECORD_LAYOUT_INDEX_MODE_ALTERNATE_WITH_Y);
        } else if (comp.has_axis_points()) {
            if (comp.axis_points().axis() == a2l::RECORD_LAYOUT_AXIS_X) {
                info.axisXType = comp.axis_points().datatype();
            } else if (comp.axis_points().axis() == a2l::RECORD_LAYOUT_AXIS_Y) {
                info.axisYType = comp.axis_points().datatype();
            }
        } else if (comp.has_identification()) {
            info.identType = comp.identification().datatype();
        } else if (comp.has_axis_count()) {
            if (comp.axis_count().axis() == a2l::RECORD_LAYOUT_AXIS_X) {
                info.axisCountXType = comp.axis_count().datatype();
            } else if (comp.axis_count().axis() == a2l::RECORD_LAYOUT_AXIS_Y) {
                info.axisCountYType = comp.axis_count().datatype();
            }
        }
    }
    return info;
}

struct SizeResult {
    uint64_t size = 0;
    bool approximate = false;
};

SizeResult computeCharacteristicSize(const a2l::Characteristic& ch,
                                     const RecordLayoutInfo& rlInfo) {
    uint64_t fvSize = dataTypeSize(rlInfo.functionValuesType);
    if (fvSize == 0) {
        return {0, true};
    }

    switch (ch.type()) {
    case a2l::CHARACTERISTIC_TYPE_VALUE:
        return {fvSize, false};

    case a2l::CHARACTERISTIC_TYPE_ASCII: {
        // matrix_dim gives string length, or number field.
        uint64_t len = 0;
        if (ch.matrix_dim_size() > 0) {
            len = ch.matrix_dim(0);
        } else if (ch.has_number()) {
            len = ch.number();
        }
        return {len > 0 ? len : fvSize, len == 0};
    }

    case a2l::CHARACTERISTIC_TYPE_VAL_BLK: {
        uint64_t count = 1;
        for (int i = 0; i < ch.matrix_dim_size(); ++i) {
            count *= ch.matrix_dim(i);
        }
        if (ch.matrix_dim_size() == 0 && ch.has_number()) {
            count = ch.number();
        }
        return {count * fvSize, false};
    }

    case a2l::CHARACTERISTIC_TYPE_CURVE: {
        if (rlInfo.hasAlternateMode) {
            // Tier 3: deferred.
            uint32_t axisCount = 0;
            if (ch.axis_descrs_size() > 0) {
                axisCount = ch.axis_descrs(0).max_axis_points();
            }
            uint64_t axSize = dataTypeSize(rlInfo.axisXType);
            uint64_t est = axisCount * (fvSize + axSize);
            return {est > 0 ? est : fvSize, true};
        }
        uint32_t axisCount = 0;
        if (ch.axis_descrs_size() > 0) {
            axisCount = ch.axis_descrs(0).max_axis_points();
        }
        uint64_t axSize = dataTypeSize(rlInfo.axisXType);
        // NO_AXIS_PTS_X header field.
        uint64_t header = dataTypeSize(rlInfo.axisCountXType);
        // Identification header.
        header += dataTypeSize(rlInfo.identType);
        uint64_t total = header + axisCount * axSize + axisCount * fvSize;
        return {total > 0 ? total : fvSize, false};
    }

    case a2l::CHARACTERISTIC_TYPE_MAP: {
        if (rlInfo.hasAlternateMode) {
            uint32_t xCount = 0, yCount = 0;
            if (ch.axis_descrs_size() > 0) xCount = ch.axis_descrs(0).max_axis_points();
            if (ch.axis_descrs_size() > 1) yCount = ch.axis_descrs(1).max_axis_points();
            uint64_t est = xCount * yCount * fvSize;
            return {est > 0 ? est : fvSize, true};
        }
        uint32_t xCount = 0, yCount = 0;
        if (ch.axis_descrs_size() > 0) xCount = ch.axis_descrs(0).max_axis_points();
        if (ch.axis_descrs_size() > 1) yCount = ch.axis_descrs(1).max_axis_points();
        uint64_t axXSize = dataTypeSize(rlInfo.axisXType);
        uint64_t axYSize = dataTypeSize(rlInfo.axisYType);
        uint64_t header = dataTypeSize(rlInfo.axisCountXType)
                        + dataTypeSize(rlInfo.axisCountYType)
                        + dataTypeSize(rlInfo.identType);
        uint64_t total = header + xCount * axXSize + yCount * axYSize
                       + static_cast<uint64_t>(xCount) * yCount * fvSize;
        return {total > 0 ? total : fvSize, false};
    }

    case a2l::CHARACTERISTIC_TYPE_CUBOID:
    case a2l::CHARACTERISTIC_TYPE_CUBE_4:
    case a2l::CHARACTERISTIC_TYPE_CUBE_5: {
        // Tier 3: approximate.
        uint64_t count = 1;
        for (int i = 0; i < ch.axis_descrs_size(); ++i) {
            count *= ch.axis_descrs(i).max_axis_points();
        }
        return {count * fvSize, true};
    }

    default:
        return {fvSize, true};
    }
}

SizeResult computeAxisPtsSize(const a2l::AxisPts& ap,
                              const RecordLayoutInfo& rlInfo) {
    uint64_t axSize = dataTypeSize(rlInfo.axisXType);
    if (axSize == 0) {
        axSize = dataTypeSize(rlInfo.functionValuesType);
    }
    if (axSize == 0) {
        return {0, true};
    }
    uint64_t header = dataTypeSize(rlInfo.axisCountXType)
                    + dataTypeSize(rlInfo.identType);
    uint64_t total = header + ap.max_axis_points() * axSize;
    return {total, false};
}

} // namespace (size calculation helpers)

void A2lDocumentSession::buildMemoryMap() {
    memoryMapModel_ = std::make_unique<MemoryMapModel>();

    // Build record layout lookup across all modules.
    std::unordered_map<std::string, const a2l::RecordLayout*> rlMap;
    for (int m = 0; m < document_.modules_size(); ++m) {
        const auto& module = document_.modules(m);
        for (int i = 0; i < module.record_layouts_size(); ++i) {
            rlMap[module.record_layouts(i).name()] = &module.record_layouts(i);
        }
    }

    int excludedMeasurements = 0;

    for (int m = 0; m < document_.modules_size(); ++m) {
        const auto& module = document_.modules(m);

        // Add real memory segments.
        if (module.has_mod_par()) {
            for (const auto& seg : module.mod_par().memory_segments()) {
                MemorySegmentInfo info;
                info.name = text(seg.name());
                info.address = seg.address();
                info.size = seg.size();
                info.memoryType = text(
                    a2l::MemoryType_Name(seg.memory_type()));
                info.prgType = text(
                    a2l::MemoryPrgType_Name(seg.prg_type()));
                memoryMapModel_->addSegment(std::move(info));
            }
        }

        // Characteristics.
        for (int i = 0; i < module.characteristics_size(); ++i) {
            const auto& ch = module.characteristics(i);
            RecordLayoutInfo rlInfo;
            auto rlIt = rlMap.find(ch.record_layout_ref());
            if (rlIt != rlMap.end()) {
                rlInfo = analyzeRecordLayout(*rlIt->second);
            }

            SizeResult sr = computeCharacteristicSize(ch, rlInfo);

            MemoryObject obj;
            obj.name = text(ch.name());
            obj.longIdentifier = text(ch.long_identifier());
            obj.typeName = text(
                a2l::CharacteristicType_Name(ch.type()));
            obj.address = ch.address();
            obj.size = sr.size;
            obj.sizeApproximate = sr.approximate;
            obj.colorIndex = characteristicColorIndex(ch.type());
            obj.recordLayoutRef = text(ch.record_layout_ref());
            obj.conversion = text(ch.conversion());
            auto keyIt = treeNodeKeys_.find({static_cast<int>(A2lEntityKind::Characteristic), m, i});
            if (keyIt != treeNodeKeys_.end()) obj.nodeKey = keyIt->second;
            memoryMapModel_->addObject(std::move(obj));
        }

        // AxisPts.
        for (int i = 0; i < module.axis_points_size(); ++i) {
            const auto& ap = module.axis_points(i);
            RecordLayoutInfo rlInfo;
            auto rlIt = rlMap.find(ap.record_layout_ref());
            if (rlIt != rlMap.end()) {
                rlInfo = analyzeRecordLayout(*rlIt->second);
            }

            SizeResult sr = computeAxisPtsSize(ap, rlInfo);

            MemoryObject obj;
            obj.name = text(ap.name());
            obj.longIdentifier = text(ap.long_identifier());
            obj.typeName = QStringLiteral("AXIS_PTS");
            obj.address = ap.address();
            obj.size = sr.size;
            obj.sizeApproximate = sr.approximate;
            obj.colorIndex = 7; // AXIS_PTS color
            obj.recordLayoutRef = text(ap.record_layout_ref());
            obj.conversion = text(ap.conversion_ref());
            auto keyIt = treeNodeKeys_.find({static_cast<int>(A2lEntityKind::AxisPts), m, i});
            if (keyIt != treeNodeKeys_.end()) obj.nodeKey = keyIt->second;
            memoryMapModel_->addObject(std::move(obj));
        }

        // Measurements (only those with ecu_address).
        for (int i = 0; i < module.measurements_size(); ++i) {
            const auto& meas = module.measurements(i);
            if (!meas.has_ecu_address()) {
                ++excludedMeasurements;
                continue;
            }

            uint64_t size = dataTypeSize(meas.datatype());
            if (meas.has_array_size() && meas.array_size() > 1) {
                size *= meas.array_size();
            } else if (meas.matrix_dim_size() > 0) {
                uint64_t count = 1;
                for (int d = 0; d < meas.matrix_dim_size(); ++d) {
                    count *= meas.matrix_dim(d);
                }
                size *= count;
            }

            MemoryObject obj;
            obj.name = text(meas.name());
            obj.longIdentifier = text(meas.long_identifier());
            obj.typeName = QStringLiteral("MEASUREMENT");
            obj.address = meas.ecu_address();
            obj.size = size > 0 ? size : 1;
            obj.sizeApproximate = (size == 0);
            obj.colorIndex = 6; // MEASUREMENT color
            auto keyIt = treeNodeKeys_.find({static_cast<int>(A2lEntityKind::Measurement), m, i});
            if (keyIt != treeNodeKeys_.end()) obj.nodeKey = keyIt->second;
            memoryMapModel_->addObject(std::move(obj));
        }
    }

    memoryMapModel_->setExcludedMeasurementCount(excludedMeasurements);
    memoryMapModel_->finalize();
}

void A2lDocumentSession::buildTree() {
    auto root = std::make_unique<TreeItem>();
    root->title = displayName();
    root->semanticKind = SemanticKind::Root;
    if (document_.has_asap2_version_major() || document_.has_asap2_version_minor()) {
        root->subtitle = QStringLiteral("ASAP2 %1.%2")
            .arg(document_.has_asap2_version_major() ? document_.asap2_version_major() : 0)
            .arg(document_.has_asap2_version_minor() ? document_.asap2_version_minor() : 0);
    }

    TreeItem* modulesSection = appendNode(root.get(),
                                          QStringLiteral("Modules"),
                                          QString::number(document_.modules_size()),
                                          QStringLiteral("modules"),
                                          SemanticKind::Section);

    for (int moduleIndex = 0; moduleIndex < document_.modules_size(); ++moduleIndex) {
        const auto& module = document_.modules(moduleIndex);
        TreeItem* moduleItem = appendNode(modulesSection,
                                          text(module.name()),
                                          text(module.long_identifier()),
                                          QStringLiteral("module"),
                                          SemanticKind::Entity,
                                          NodeBinding{SemanticKind::Entity,
                                                      A2lPath{A2lEntityKind::Module, moduleIndex, -1, -1},
                                                      true});

        if (module.measurements_size() > 0) {
            TreeItem* section = appendNode(moduleItem,
                                           QStringLiteral("Measurements"),
                                           QString::number(module.measurements_size()),
                                           QStringLiteral("measurements"),
                                           SemanticKind::Section);
            for (int i = 0; i < module.measurements_size(); ++i) {
                const auto& item = module.measurements(i);
                TreeItem* node = appendNode(section,
                           text(item.name()),
                           text(a2l::DataType_Name(item.datatype())),
                           QStringLiteral("measurement"),
                           SemanticKind::Entity,
                           NodeBinding{SemanticKind::Entity,
                                       A2lPath{A2lEntityKind::Measurement, moduleIndex, i, -1},
                                       true});
                if (node->nodeKey) {
                    treeNodeKeys_[{static_cast<int>(A2lEntityKind::Measurement), moduleIndex, i}] = node->nodeKey;
                }
            }
        }

        if (module.characteristics_size() > 0) {
            TreeItem* section = appendNode(moduleItem,
                                           QStringLiteral("Characteristics"),
                                           QString::number(module.characteristics_size()),
                                           QStringLiteral("characteristics"),
                                           SemanticKind::Section);
            for (int i = 0; i < module.characteristics_size(); ++i) {
                const auto& item = module.characteristics(i);
                TreeItem* node = appendNode(section,
                           text(item.name()),
                           text(a2l::CharacteristicType_Name(item.type())),
                           QStringLiteral("characteristic"),
                           SemanticKind::Entity,
                           NodeBinding{SemanticKind::Entity,
                                       A2lPath{A2lEntityKind::Characteristic, moduleIndex, i, -1},
                                       true});
                if (node->nodeKey) {
                    treeNodeKeys_[{static_cast<int>(A2lEntityKind::Characteristic), moduleIndex, i}] = node->nodeKey;
                }
            }
        }

        if (module.axis_points_size() > 0) {
            TreeItem* section = appendNode(moduleItem,
                                           QStringLiteral("Axis Points"),
                                           QString::number(module.axis_points_size()),
                                           QStringLiteral("axispts"),
                                           SemanticKind::Section);
            for (int i = 0; i < module.axis_points_size(); ++i) {
                const auto& item = module.axis_points(i);
                TreeItem* node = appendNode(section,
                           text(item.name()),
                           text(item.record_layout_ref()),
                           QStringLiteral("axispt"),
                           SemanticKind::Entity,
                           NodeBinding{SemanticKind::Entity,
                                       A2lPath{A2lEntityKind::AxisPts, moduleIndex, i, -1},
                                       true});
                if (node->nodeKey) {
                    treeNodeKeys_[{static_cast<int>(A2lEntityKind::AxisPts), moduleIndex, i}] = node->nodeKey;
                }
            }
        }

        if (module.compu_methods_size() > 0) {
            TreeItem* section = appendNode(moduleItem,
                                           QStringLiteral("Compu Methods"),
                                           QString::number(module.compu_methods_size()),
                                           QStringLiteral("compumethods"),
                                           SemanticKind::Section);
            for (int i = 0; i < module.compu_methods_size(); ++i) {
                const auto& item = module.compu_methods(i);
                appendNode(section,
                           text(item.name()),
                           text(a2l::ConversionType_Name(item.conversion_type())),
                           QStringLiteral("compumethod"),
                           SemanticKind::Entity,
                           NodeBinding{SemanticKind::Entity,
                                       A2lPath{A2lEntityKind::CompuMethod, moduleIndex, i, -1},
                                       true});
            }
        }

        if (module.record_layouts_size() > 0) {
            TreeItem* section = appendNode(moduleItem,
                                           QStringLiteral("Record Layouts"),
                                           QString::number(module.record_layouts_size()),
                                           QStringLiteral("recordlayouts"),
                                           SemanticKind::Section);
            for (int i = 0; i < module.record_layouts_size(); ++i) {
                const auto& item = module.record_layouts(i);
                appendNode(section,
                           text(item.name()),
                           QStringLiteral("%1 components").arg(item.components_size()),
                           QStringLiteral("recordlayout"),
                           SemanticKind::Entity,
                           NodeBinding{SemanticKind::Entity,
                                       A2lPath{A2lEntityKind::RecordLayout, moduleIndex, i, -1},
                                       true});
            }
        }

        if (module.units_size() > 0) {
            TreeItem* section = appendNode(moduleItem,
                                           QStringLiteral("Units"),
                                           QString::number(module.units_size()),
                                           QStringLiteral("units"),
                                           SemanticKind::Section);
            for (int i = 0; i < module.units_size(); ++i) {
                const auto& item = module.units(i);
                appendNode(section,
                           text(item.name()),
                           text(item.display()),
                           QStringLiteral("unit"),
                           SemanticKind::Entity,
                           NodeBinding{SemanticKind::Entity,
                                       A2lPath{A2lEntityKind::Unit, moduleIndex, i, -1},
                                       true});
            }
        }

        if (module.functions_size() > 0) {
            TreeItem* section = appendNode(moduleItem,
                                           QStringLiteral("Functions"),
                                           QString::number(module.functions_size()),
                                           QStringLiteral("functions"),
                                           SemanticKind::Section);
            for (int i = 0; i < module.functions_size(); ++i) {
                const auto& item = module.functions(i);
                appendNode(section,
                           text(item.name()),
                           QStringLiteral("%1 refs").arg(item.in_measurements_size() +
                                                         item.out_measurements_size() +
                                                         item.def_characteristics_size() +
                                                         item.ref_characteristics_size()),
                           QStringLiteral("function"),
                           SemanticKind::Entity,
                           NodeBinding{SemanticKind::Entity,
                                       A2lPath{A2lEntityKind::Function, moduleIndex, i, -1},
                                       true});
            }
        }

        if (module.groups_size() > 0) {
            TreeItem* section = appendNode(moduleItem,
                                           QStringLiteral("Groups"),
                                           QString::number(module.groups_size()),
                                           QStringLiteral("groups"),
                                           SemanticKind::Section);
            for (int i = 0; i < module.groups_size(); ++i) {
                const auto& item = module.groups(i);
                appendNode(section,
                           text(item.name()),
                           item.has_root() && item.root() ? QStringLiteral("root") : QString(),
                           QStringLiteral("group"),
                           SemanticKind::Entity,
                           NodeBinding{SemanticKind::Entity,
                                       A2lPath{A2lEntityKind::Group, moduleIndex, i, -1},
                                       true});
            }
        }

        bool hasXcp = false;
        bool hasCcp = false;
        for (const auto& ifData : module.if_datas()) {
            hasXcp = hasXcp || ifData.has_xcp();
            hasCcp = hasCcp || ifData.has_ccp();
        }

        if (hasXcp || hasCcp) {
            TreeItem* protocolSection = appendNode(moduleItem,
                                                   QStringLiteral("Protocols"),
                                                   QString(),
                                                   QStringLiteral("protocols"),
                                                   SemanticKind::Section);
            if (hasXcp) {
                appendNode(protocolSection,
                           QStringLiteral("XCP"),
                           QStringLiteral("module IF_DATA"),
                           QStringLiteral("xcp"),
                           SemanticKind::Entity,
                           NodeBinding{SemanticKind::Entity,
                                       A2lPath{A2lEntityKind::XcpSummary, moduleIndex, -1, -1},
                                       true});
            }
            if (hasCcp) {
                appendNode(protocolSection,
                           QStringLiteral("CCP"),
                           QStringLiteral("module IF_DATA"),
                           QStringLiteral("ccp"),
                           SemanticKind::Entity,
                           NodeBinding{SemanticKind::Entity,
                                       A2lPath{A2lEntityKind::CcpSummary, moduleIndex, -1, -1},
                                       true});
            }
        }

        if (module.typedef_characteristics_size() > 0 ||
            module.typedef_structures_size() > 0 ||
            module.typedef_axes_size() > 0 ||
            module.instances_size() > 0) {
            TreeItem* typesSection = appendNode(moduleItem,
                                                QStringLiteral("Types"),
                                                QString(),
                                                QStringLiteral("types"),
                                                SemanticKind::Section);

            if (module.typedef_characteristics_size() > 0) {
                TreeItem* section = appendNode(typesSection,
                                               QStringLiteral("Typedef Characteristics"),
                                               QString::number(module.typedef_characteristics_size()),
                                               QStringLiteral("typedefcharacteristics"),
                                               SemanticKind::Section);
                for (int i = 0; i < module.typedef_characteristics_size(); ++i) {
                    const auto& item = module.typedef_characteristics(i);
                    appendNode(section,
                               text(item.name()),
                               text(a2l::CharacteristicType_Name(item.type())),
                               QStringLiteral("typedefcharacteristic"),
                               SemanticKind::Entity,
                               NodeBinding{SemanticKind::Entity,
                                           A2lPath{A2lEntityKind::TypedefItem,
                                                   moduleIndex,
                                                   i,
                                                   kTypedefCharacteristicCategory},
                                           true});
                }
            }

            if (module.typedef_structures_size() > 0) {
                TreeItem* section = appendNode(typesSection,
                                               QStringLiteral("Typedef Structures"),
                                               QString::number(module.typedef_structures_size()),
                                               QStringLiteral("typedefstructures"),
                                               SemanticKind::Section);
                for (int i = 0; i < module.typedef_structures_size(); ++i) {
                    const auto& item = module.typedef_structures(i);
                    appendNode(section,
                               text(item.name()),
                               QStringLiteral("%1 components").arg(item.components_size()),
                               QStringLiteral("typedefstructure"),
                               SemanticKind::Entity,
                               NodeBinding{SemanticKind::Entity,
                                           A2lPath{A2lEntityKind::TypedefItem,
                                                   moduleIndex,
                                                   i,
                                                   kTypedefStructureCategory},
                                           true});
                }
            }

            if (module.typedef_axes_size() > 0) {
                TreeItem* section = appendNode(typesSection,
                                               QStringLiteral("Typedef Axes"),
                                               QString::number(module.typedef_axes_size()),
                                               QStringLiteral("typedefaxes"),
                                               SemanticKind::Section);
                for (int i = 0; i < module.typedef_axes_size(); ++i) {
                    const auto& item = module.typedef_axes(i);
                    appendNode(section,
                               text(item.name()),
                               text(item.input_quantity()),
                               QStringLiteral("typedefaxis"),
                               SemanticKind::Entity,
                               NodeBinding{SemanticKind::Entity,
                                           A2lPath{A2lEntityKind::TypedefItem,
                                                   moduleIndex,
                                                   i,
                                                   kTypedefAxisCategory},
                                           true});
                }
            }

            if (module.instances_size() > 0) {
                TreeItem* section = appendNode(typesSection,
                                               QStringLiteral("Instances"),
                                               QString::number(module.instances_size()),
                                               QStringLiteral("instances"),
                                               SemanticKind::Section);
                for (int i = 0; i < module.instances_size(); ++i) {
                    const auto& item = module.instances(i);
                    appendNode(section,
                               text(item.name()),
                               text(item.type_ref()),
                               QStringLiteral("instance"),
                               SemanticKind::Entity,
                               NodeBinding{SemanticKind::Entity,
                                           A2lPath{A2lEntityKind::Instance, moduleIndex, i, -1},
                                           true});
                }
            }
        }

        if (module.has_variant_coding()) {
            appendNode(moduleItem,
                       QStringLiteral("Variant Coding"),
                       QStringLiteral("%1 criteria").arg(module.variant_coding().var_criteria_size()),
                       QStringLiteral("variantcoding"),
                       SemanticKind::Entity,
                       NodeBinding{SemanticKind::Entity,
                                   A2lPath{A2lEntityKind::VariantCoding, moduleIndex, -1, -1},
                                   true});
        }
    }

    setRootItem(std::move(root));
}
