#include "sessions/a2ldetailpresenter.h"

#include <QStringList>
#include <variant>

#include <google/protobuf/util/json_util.h>

QList<DetailSection> A2lDetailPresenter::buildDetails(const NodeBinding& binding) const {
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

QString A2lDetailPresenter::buildRawJson(const NodeBinding& binding) const {
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

    google::protobuf::util::JsonPrintOptions opts;
    opts.add_whitespace = true;
    std::string json;
    auto status = google::protobuf::util::MessageToJsonString(*msg, &json, opts);
    if (!status.ok()) {
        return {};
    }
    return text(json);
}

const a2l::Module* A2lDetailPresenter::moduleAt(int index) const {
    if (index < 0 || index >= _document.modules_size()) {
        return nullptr;
    }
    return &_document.modules(index);
}

QString A2lDetailPresenter::entityName(const A2lPath& path) const {
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

QString A2lDetailPresenter::buildBreadcrumb(const A2lPath& path) const {
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
        return QStringLiteral("%1 › %2").arg(moduleName, section);
    }
    return QStringLiteral("%1 › %2 › %3").arg(moduleName, section, name);
}

const a2l::CompuMethod* A2lDetailPresenter::findCompuMethod(const a2l::Module& module, const std::string& name) const {
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

void A2lDetailPresenter::appendConversionSummary(QList<DetailSection>& sections, const A2lPath& path) const {
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

void A2lDetailPresenter::appendCrossReferences(QList<DetailSection>& sections, const A2lPath& path) const {
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

QList<DetailSection> A2lDetailPresenter::moduleDetails(const A2lPath& path) const {
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

QList<DetailSection> A2lDetailPresenter::measurementDetails(const A2lPath& path) const {
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

QList<DetailSection> A2lDetailPresenter::characteristicDetails(const A2lPath& path) const {
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
    addIfDataCounts(calibration, characteristic.if_datas());
    pushSection(sections, QStringLiteral("Calibration"), std::move(calibration));

    for (int a = 0; a < characteristic.axis_descrs_size(); ++a) {
        const auto& axis = characteristic.axis_descrs(a);
        QString axisLabel = (a < 3) ? QString(QChar('X' + a)) : QStringLiteral("Axis %1").arg(a + 1);
        QList<DetailField> axisFields;
        addField(axisFields,
                 QStringLiteral("Attribute"),
                 text(a2l::AxisAttribute_Name(axis.attribute())));
        addField(axisFields, QStringLiteral("Input Quantity"), text(axis.input_quantity()));
        addField(axisFields, QStringLiteral("Conversion"), text(axis.conversion()));
        addNumberField(axisFields, QStringLiteral("Max Axis Points"), axis.max_axis_points());
        addNumberField(axisFields, QStringLiteral("Lower Limit"), axis.lower_limit());
        addNumberField(axisFields, QStringLiteral("Upper Limit"), axis.upper_limit());
        addOptionalString(axisFields, QStringLiteral("Axis Pts Ref"), axis.has_axis_pts_ref(), axis.axis_pts_ref());
        addOptionalString(axisFields, QStringLiteral("Curve Axis Ref"), axis.has_curve_axis_ref(), axis.curve_axis_ref());
        addField(axisFields,
                 QStringLiteral("Byte Order"),
                 axis.has_byte_order()
                     ? text(a2l::ByteOrder_Name(axis.byte_order()))
                     : QString());
        addField(axisFields,
                 QStringLiteral("Monotony"),
                 axis.has_monotony()
                     ? text(a2l::Monotony_Name(axis.monotony()))
                     : QString());
        addOptionalString(axisFields, QStringLiteral("Format"), axis.has_format(), axis.format());
        addOptionalString(axisFields, QStringLiteral("Physical Unit"), axis.has_phys_unit(), axis.phys_unit());
        addOptionalBool(axisFields, QStringLiteral("Read Only"), axis.has_read_only(), axis.read_only());
        addOptionalNumber(axisFields, QStringLiteral("Step Size"), axis.has_step_size(), axis.step_size());
        addOptionalNumber(axisFields, QStringLiteral("Max Grad"), axis.has_max_grad(), axis.max_grad());
        if (axis.has_fix_axis_par()) {
            addField(axisFields, QStringLiteral("Fix Axis Par"),
                     QStringLiteral("offset=%1  shift=%2  n=%3")
                         .arg(axis.fix_axis_par().offset())
                         .arg(axis.fix_axis_par().shift())
                         .arg(axis.fix_axis_par().numberapo()));
        }
        if (axis.has_fix_axis_par_dist()) {
            addField(axisFields, QStringLiteral("Fix Axis Par Dist"),
                     QStringLiteral("offset=%1  distance=%2  n=%3")
                         .arg(axis.fix_axis_par_dist().offset())
                         .arg(axis.fix_axis_par_dist().distance())
                         .arg(axis.fix_axis_par_dist().numberapo()));
        }
        if (axis.fix_axis_par_list_size() > 0) {
            QStringList vals;
            for (double v : axis.fix_axis_par_list()) {
                vals.push_back(QString::number(v));
            }
            addField(axisFields, QStringLiteral("Fix Axis Par List"), vals.join(QStringLiteral(", ")));
        }
        pushSection(sections, QStringLiteral("Axis %1").arg(axisLabel), std::move(axisFields));
    }
    return sections;
}

QList<DetailSection> A2lDetailPresenter::axisPtsDetails(const A2lPath& path) const {
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

QList<DetailSection> A2lDetailPresenter::compuMethodDetails(const A2lPath& path) const {
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

QList<DetailSection> A2lDetailPresenter::recordLayoutDetails(const A2lPath& path) const {
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

    for (int i = 0; i < layout.components_size(); ++i) {
        const auto& comp = layout.components(i);
        QList<DetailField> cf;
        addField(cf, QStringLiteral("Summary"), recordLayoutComponentSummary(comp));

        if (comp.has_function_values()) {
            const auto& fv = comp.function_values();
            addField(cf, QStringLiteral("Data Type"), text(a2l::DataType_Name(fv.datatype())));
            addField(cf, QStringLiteral("Index Mode"), text(a2l::RecordLayoutIndexMode_Name(fv.index_mode())));
            addField(cf, QStringLiteral("Address Type"), text(a2l::RecordLayoutAddressType_Name(fv.address_type())));
        } else if (comp.has_axis_points()) {
            const auto& ap = comp.axis_points();
            addField(cf, QStringLiteral("Data Type"), text(a2l::DataType_Name(ap.datatype())));
            addField(cf, QStringLiteral("Index Order"), text(a2l::IndexOrder_Name(ap.index_order())));
            addField(cf, QStringLiteral("Address Type"), text(a2l::RecordLayoutAddressType_Name(ap.address_type())));
        } else if (comp.has_axis_rescale()) {
            const auto& ar = comp.axis_rescale();
            addField(cf, QStringLiteral("Data Type"), text(a2l::DataType_Name(ar.datatype())));
            addField(cf, QStringLiteral("Index Order"), text(a2l::IndexOrder_Name(ar.index_order())));
            addField(cf, QStringLiteral("Address Type"), text(a2l::RecordLayoutAddressType_Name(ar.address_type())));
            addNumberField(cf, QStringLiteral("Max Rescale Pairs"), ar.max_number_of_rescale_pairs());
        } else if (comp.has_axis_count()) {
            addField(cf, QStringLiteral("Data Type"), text(a2l::DataType_Name(comp.axis_count().datatype())));
        } else if (comp.has_rescale_count()) {
            addField(cf, QStringLiteral("Data Type"), text(a2l::DataType_Name(comp.rescale_count().datatype())));
        } else if (comp.has_axis_operand()) {
            addField(cf, QStringLiteral("Data Type"), text(a2l::DataType_Name(comp.axis_operand().datatype())));
        } else if (comp.has_address_reference()) {
            addField(cf, QStringLiteral("Data Type"), text(a2l::DataType_Name(comp.address_reference().datatype())));
        }

        pushSection(sections, QStringLiteral("Component %1").arg(i + 1), std::move(cf));
    }
    return sections;
}

QList<DetailSection> A2lDetailPresenter::unitDetails(const A2lPath& path) const {
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

QList<DetailSection> A2lDetailPresenter::functionDetails(const A2lPath& path) const {
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

QList<DetailSection> A2lDetailPresenter::groupDetails(const A2lPath& path) const {
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

QList<DetailSection> A2lDetailPresenter::typedefDetails(const A2lPath& path) const {
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

QList<DetailSection> A2lDetailPresenter::instanceDetails(const A2lPath& path) const {
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

QList<DetailSection> A2lDetailPresenter::variantCodingDetails(const A2lPath& path) const {
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
