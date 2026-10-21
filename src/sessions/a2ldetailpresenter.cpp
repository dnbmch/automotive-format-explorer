#include "sessions/a2ldetailpresenter.h"

#include <QStringList>

#include <algorithm>

using namespace a2ldetail;

QList<DetailSection> A2lDetailPresenter::buildDetails(const A2lPath& path) const {
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
    case A2lEntityKind::Blob:
        sections = blobDetails(path); break;
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
    case A2lEntityKind::Frame:
        sections = frameDetails(path); break;
    case A2lEntityKind::Transformer:
        sections = transformerDetails(path); break;
    case A2lEntityKind::UserRights:
        sections = userRightsDetails(path); break;
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

    insertResolvedConversion(sections, path);
    appendCrossReferences(sections, path);

    return sections;
}

void A2lDetailPresenter::addLink(QList<DetailField>& fields, const QString& key, int module,
                                 const std::string& name,
                                 std::initializer_list<A2lEntityKind> kinds) const {
    if (!name.empty()) {
        fields.push_back(DetailField{key, text(name), _links.find(module, name, kinds)});
    }
}

void A2lDetailPresenter::appendNameList(QList<DetailSection>& sections, const QString& title,
                                        int module,
                                        const google::protobuf::RepeatedPtrField<std::string>& names,
                                        std::initializer_list<A2lEntityKind> kinds) const {
    // A card builds every row at once: a few hundred stay quick to show.
    constexpr int kShown = 500;
    QList<DetailField> fields;
    for (int i = 0; i < names.size() && i < kShown; ++i) {
        addLink(fields, QString(), module, names[i], kinds);
    }
    if (names.size() > kShown) {
        fields.push_back(DetailField{QString(), QStringLiteral("… %1 more").arg(names.size() - kShown)});
    }
    pushSection(sections, QStringLiteral("%1 (%2)").arg(title).arg(names.size()), std::move(fields));
}

QString A2lDetailPresenter::buildRawJson(const A2lPath& path) const {
    const auto* module = moduleAt(path.primaryIndex);
    if (!module) {
        return {};
    }

    // The XCP/CCP summary nodes are synthesised over the module's repeated
    // if_datas, so their raw JSON is the matching source blocks. One match
    // renders as a single object; several render as a JSON array so the result
    // stays parseable rather than back-to-back objects.
    if (path.kind == A2lEntityKind::XcpSummary || path.kind == A2lEntityKind::CcpSummary) {
        const bool wantXcp = path.kind == A2lEntityKind::XcpSummary;
        QStringList blocks;
        for (const auto& ifData : module->if_datas()) {
            if (wantXcp ? ifData.has_xcp() : ifData.has_ccp()) {
                blocks.push_back(messageToJsonText(ifData));
            }
        }
        if (blocks.isEmpty()) {
            return {};
        }
        if (blocks.size() == 1) {
            return blocks.front();
        }
        return QStringLiteral("[\n%1\n]").arg(blocks.join(QStringLiteral(",\n")));
    }

    const google::protobuf::Message* msg = nullptr;
    int i = path.secondaryIndex;

    switch (path.kind) {
    case A2lEntityKind::Module:         msg = module; break;
    case A2lEntityKind::Measurement:    if (i >= 0 && i < module->measurements_size()) msg = &module->measurements(i); break;
    case A2lEntityKind::Characteristic: if (i >= 0 && i < module->characteristics_size()) msg = &module->characteristics(i); break;
    case A2lEntityKind::AxisPts:        if (i >= 0 && i < module->axis_points_size()) msg = &module->axis_points(i); break;
    case A2lEntityKind::Blob:           if (i >= 0 && i < module->blobs_size()) msg = &module->blobs(i); break;
    case A2lEntityKind::CompuMethod:    if (i >= 0 && i < module->compu_methods_size()) msg = &module->compu_methods(i); break;
    case A2lEntityKind::RecordLayout:   if (i >= 0 && i < module->record_layouts_size()) msg = &module->record_layouts(i); break;
    case A2lEntityKind::Unit:           if (i >= 0 && i < module->units_size()) msg = &module->units(i); break;
    case A2lEntityKind::Function:       if (i >= 0 && i < module->functions_size()) msg = &module->functions(i); break;
    case A2lEntityKind::Group:          if (i >= 0 && i < module->groups_size()) msg = &module->groups(i); break;
    case A2lEntityKind::Frame:          if (i >= 0 && i < module->frames_size()) msg = &module->frames(i); break;
    case A2lEntityKind::Transformer:    if (i >= 0 && i < module->transformers_size()) msg = &module->transformers(i); break;
    case A2lEntityKind::UserRights:     if (i >= 0 && i < module->user_rights_size()) msg = &module->user_rights(i); break;
    case A2lEntityKind::Instance:       if (i >= 0 && i < module->instances_size()) msg = &module->instances(i); break;
    case A2lEntityKind::TypedefItem:
        if (path.tertiaryIndex == kTypedefCharacteristicCategory && i >= 0 && i < module->typedef_characteristics_size())
            msg = &module->typedef_characteristics(i);
        else if (path.tertiaryIndex == kTypedefStructureCategory && i >= 0 && i < module->typedef_structures_size())
            msg = &module->typedef_structures(i);
        else if (path.tertiaryIndex == kTypedefAxisCategory && i >= 0 && i < module->typedef_axes_size())
            msg = &module->typedef_axes(i);
        else if (path.tertiaryIndex == kTypedefMeasurementCategory && i >= 0 && i < module->typedef_measurements_size())
            msg = &module->typedef_measurements(i);
        else if (path.tertiaryIndex == kTypedefBlobCategory && i >= 0 && i < module->typedef_blobs_size())
            msg = &module->typedef_blobs(i);
        break;
    case A2lEntityKind::VariantCoding:
        if (module->has_variant_coding()) msg = &module->variant_coding();
        break;
    case A2lEntityKind::XcpSummary:
    case A2lEntityKind::CcpSummary:
        // Synthesised summary nodes are rendered by the early return above;
        // listed here only to keep the switch exhaustive over A2lEntityKind.
        break;
    }

    if (!msg) {
        return {};
    }

    return messageToJsonText(*msg);
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
    case A2lEntityKind::Blob:           return (i >= 0 && i < module->blobs_size()) ? text(module->blobs(i).name()) : QString();
    case A2lEntityKind::CompuMethod:    return (i >= 0 && i < module->compu_methods_size()) ? text(module->compu_methods(i).name()) : QString();
    case A2lEntityKind::RecordLayout:   return (i >= 0 && i < module->record_layouts_size()) ? text(module->record_layouts(i).name()) : QString();
    case A2lEntityKind::Unit:           return (i >= 0 && i < module->units_size()) ? text(module->units(i).name()) : QString();
    case A2lEntityKind::Function:       return (i >= 0 && i < module->functions_size()) ? text(module->functions(i).name()) : QString();
    case A2lEntityKind::Group:          return (i >= 0 && i < module->groups_size()) ? text(module->groups(i).name()) : QString();
    case A2lEntityKind::Frame:          return (i >= 0 && i < module->frames_size()) ? text(module->frames(i).name()) : QString();
    case A2lEntityKind::Transformer:    return (i >= 0 && i < module->transformers_size()) ? text(module->transformers(i).name()) : QString();
    case A2lEntityKind::UserRights:     return (i >= 0 && i < module->user_rights_size()) ? text(module->user_rights(i).user_level_id()) : QString();
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
        if (path.tertiaryIndex == kTypedefMeasurementCategory && i >= 0 && i < module->typedef_measurements_size())
            return text(module->typedef_measurements(i).name());
        if (path.tertiaryIndex == kTypedefBlobCategory && i >= 0 && i < module->typedef_blobs_size())
            return text(module->typedef_blobs(i).name());
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

// The conversion the entity names, resolved, directly under its Identity card:
// the formula, the unit and, for a table, its rows.
void A2lDetailPresenter::insertResolvedConversion(QList<DetailSection>& sections,
                                                  const A2lPath& path) const {
    const auto* module = moduleAt(path.primaryIndex);
    if (!module) {
        return;
    }

    const int i = path.secondaryIndex;
    std::string convRef;
    // PHYS_UNIT overrides the method's unit by presence, even when empty.
    const std::string* unitOverride = nullptr;
    uint64_t sizeBytes = 0;
    if (path.kind == A2lEntityKind::Measurement && i >= 0 && i < module->measurements_size()) {
        convRef = module->measurements(i).conversion();
        sizeBytes = dataTypeSize(module->measurements(i).datatype());
        if (module->measurements(i).has_phys_unit()) unitOverride = &module->measurements(i).phys_unit();
    } else if (path.kind == A2lEntityKind::Characteristic && i >= 0 && i < module->characteristics_size()) {
        convRef = module->characteristics(i).conversion();
        if (module->characteristics(i).has_phys_unit()) unitOverride = &module->characteristics(i).phys_unit();
    } else if (path.kind == A2lEntityKind::AxisPts && i >= 0 && i < module->axis_points_size()) {
        convRef = module->axis_points(i).conversion_ref();
        if (module->axis_points(i).has_phys_unit()) unitOverride = &module->axis_points(i).phys_unit();
    } else if (path.kind == A2lEntityKind::TypedefItem
               && path.tertiaryIndex == kTypedefMeasurementCategory
               && i >= 0 && i < module->typedef_measurements_size()) {
        convRef = module->typedef_measurements(i).conversion();
        sizeBytes = dataTypeSize(module->typedef_measurements(i).datatype());
        if (module->typedef_measurements(i).has_phys_unit()) unitOverride = &module->typedef_measurements(i).phys_unit();
    } else {
        return;
    }

    QList<DetailField> fields;
    QList<DetailSection> table;
    if (const auto* cm = findCompuMethod(*module, convRef)) {
        addField(fields, QStringLiteral("Formula"), conversionFormula(*cm));
        addField(fields, QStringLiteral("Physical Unit"), text(unitOverride ? *unitOverride : cm->unit()));
        addField(fields, QStringLiteral("Format"), text(cm->format()));
        if (cm->has_compu_tab_ref()) {
            appendConversionTable(table, *module, cm->compu_tab_ref());
        }
    }
    if (sizeBytes > 0) {
        addNumberField(fields, QStringLiteral("Data Size (bytes)"), sizeBytes);
    }
    qsizetype at = std::min<qsizetype>(2, sections.size());   // after Context and Identity
    if (!fields.isEmpty()) {
        sections.insert(at++, DetailSection{QStringLiteral("Resolved Conversion"), std::move(fields)});
    }
    for (DetailSection& section : table) {
        sections.insert(at++, std::move(section));
    }
}

// The rows of the COMPU_TAB, COMPU_VTAB or COMPU_VTAB_RANGE a conversion names.
void A2lDetailPresenter::appendConversionTable(QList<DetailSection>& sections,
                                               const a2l::Module& module,
                                               const std::string& name) const {
    constexpr int kShown = 500;
    QList<DetailField> rows;
    QString title;
    int total = 0;
    for (const auto& tab : module.compu_tabs()) {
        if (tab.name() != name) continue;
        title = QStringLiteral("Table %1 (int → phys)").arg(text(name));
        total = tab.pairs_size();
        for (int r = 0; r < total && r < kShown; ++r) {
            rows.push_back({numberText(tab.pairs(r).in_val()), numberText(tab.pairs(r).out_val())});
        }
    }
    for (const auto& tab : module.compu_vtabs()) {
        if (tab.name() != name) continue;
        title = QStringLiteral("Verbal Table %1").arg(text(name));
        total = tab.pairs_size();
        for (int r = 0; r < total && r < kShown; ++r) {
            rows.push_back({numberText(tab.pairs(r).in_val()), text(tab.pairs(r).out_val())});
        }
    }
    for (const auto& tab : module.compu_vtab_ranges()) {
        if (tab.name() != name) continue;
        title = QStringLiteral("Verbal Range Table %1").arg(text(name));
        total = tab.triples_size();
        for (int r = 0; r < total && r < kShown; ++r) {
            const auto& t = tab.triples(r);
            rows.push_back({QStringLiteral("%1 .. %2").arg(numberText(t.in_val_min()), numberText(t.in_val_max())),
                            text(t.out_val())});
        }
    }
    if (total > kShown) {
        rows.push_back({QString(), QStringLiteral("… %1 more").arg(total - kShown)});
    }
    pushSection(sections, title, std::move(rows));
}

// Who names this entity: functions, groups, frames and transformers, each a link.
void A2lDetailPresenter::appendCrossReferences(QList<DetailSection>& sections, const A2lPath& path) const {
    const bool measurement = path.kind == A2lEntityKind::Measurement;
    const bool adjustable = path.kind == A2lEntityKind::Characteristic
        || path.kind == A2lEntityKind::AxisPts || path.kind == A2lEntityKind::Blob;
    if (!measurement && !adjustable) {
        return;
    }
    const auto* module = moduleAt(path.primaryIndex);
    if (!module) {
        return;
    }
    const std::string name = entityName(path).toStdString();
    const int m = path.primaryIndex;
    const auto names = [&name](const google::protobuf::RepeatedPtrField<std::string>& list) {
        return std::find(list.begin(), list.end(), name) != list.end();
    };

    QList<DetailField> fields;
    for (const auto& fn : module->functions()) {
        if (measurement ? names(fn.in_measurements()) || names(fn.out_measurements())
                              || names(fn.loc_measurements())
                        : names(fn.def_characteristics()) || names(fn.ref_characteristics())) {
            addLink(fields, QStringLiteral("Function"), m, fn.name(), {A2lEntityKind::Function});
        }
    }
    for (const auto& group : module->groups()) {
        if (measurement ? names(group.ref_measurements()) : names(group.ref_characteristics())) {
            addLink(fields, QStringLiteral("Group"), m, group.name(), {A2lEntityKind::Group});
        }
    }
    for (const auto& frame : module->frames()) {
        if (measurement && names(frame.frame_measurements())) {
            addLink(fields, QStringLiteral("Frame"), m, frame.name(), {A2lEntityKind::Frame});
        }
    }
    for (const auto& transformer : module->transformers()) {
        if (adjustable && names(transformer.in_objects())) {
            addLink(fields, QStringLiteral("Transformer input"), m, transformer.name(),
                    {A2lEntityKind::Transformer});
        }
        if (adjustable && names(transformer.out_objects())) {
            addLink(fields, QStringLiteral("Transformer output"), m, transformer.name(),
                    {A2lEntityKind::Transformer});
        }
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
    addNumberField(identity, QStringLiteral("Blobs"), module->blobs_size());
    addNumberField(identity, QStringLiteral("Compu Methods"), module->compu_methods_size());
    addNumberField(identity, QStringLiteral("Record Layouts"), module->record_layouts_size());
    addNumberField(identity, QStringLiteral("Units"), module->units_size());
    addNumberField(identity, QStringLiteral("Functions"), module->functions_size());
    addNumberField(identity, QStringLiteral("Groups"), module->groups_size());
    addNumberField(identity, QStringLiteral("Frames"), module->frames_size());
    addNumberField(identity, QStringLiteral("Transformers"), module->transformers_size());
    addNumberField(identity, QStringLiteral("User Rights"), module->user_rights_size());
    addNumberField(identity, QStringLiteral("Typedef Characteristics"), module->typedef_characteristics_size());
    addNumberField(identity, QStringLiteral("Typedef Measurements"), module->typedef_measurements_size());
    addNumberField(identity, QStringLiteral("Typedef Structures"), module->typedef_structures_size());
    addNumberField(identity, QStringLiteral("Typedef Axes"), module->typedef_axes_size());
    addNumberField(identity, QStringLiteral("Typedef Blobs"), module->typedef_blobs_size());
    addNumberField(identity, QStringLiteral("Instances"), module->instances_size());
    pushSection(sections, QStringLiteral("Identity"), std::move(identity));

    if (module->has_mod_common()) {
        const auto& modCommon = module->mod_common();
        QList<DetailField> defaults;
        addField(defaults,
                 QStringLiteral("Byte Order"),
                 enumKeyword(modCommon.byte_order()));
        addOptionalNumber(defaults,
                          QStringLiteral("Data Size Bits"),
                          modCommon.has_data_size_bits(),
                          modCommon.data_size_bits());
        addField(defaults,
                 QStringLiteral("Deposit Mode"),
                 enumKeyword(modCommon.deposit_mode()));
        // The map is unordered; sort by key so the line reads the same each time.
        std::map<std::string, uint32_t> alignments(modCommon.alignments().begin(),
                                                   modCommon.alignments().end());
        QStringList alignmentText;
        for (const auto& [kind, bytes] : alignments) {
            alignmentText.push_back(QStringLiteral("%1 %2").arg(text(kind)).arg(bytes));
        }
        addField(defaults, QStringLiteral("Alignments"), alignmentText.join(QStringLiteral(", ")));
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
             enumKeyword(measurement.datatype()));
    addLink(identity, QStringLiteral("Conversion"), path.primaryIndex, measurement.conversion(),
            {A2lEntityKind::CompuMethod});
    addNumberField(identity, QStringLiteral("Resolution"), measurement.resolution());
    addNumberField(identity, QStringLiteral("Accuracy"), measurement.accuracy());
    pushSection(sections, QStringLiteral("Identity"), std::move(identity));

    QList<DetailField> range;
    addNumberField(range, QStringLiteral("Lower Limit"), measurement.lower_limit());
    addNumberField(range, QStringLiteral("Upper Limit"), measurement.upper_limit());
    if (measurement.has_ecu_address()) {
        addField(range, QStringLiteral("ECU Address"), hexText(measurement.ecu_address()));
    }
    addOptionalNumber(range,
                      QStringLiteral("ECU Address Extension"),
                      measurement.has_ecu_address_extension(),
                      measurement.ecu_address_extension());
    addField(range,
             QStringLiteral("Byte Order"),
             measurement.has_byte_order()
                 ? enumKeyword(measurement.byte_order())
                 : QString());
    pushSection(sections, QStringLiteral("Range / Address"), std::move(range));

    QList<DetailField> layout;
    if (measurement.has_bit_mask()) {
        addField(layout, QStringLiteral("Bit Mask"), hexText(measurement.bit_mask()));
    }
    if (measurement.has_bit_operation()) {
        const auto& bitOp = measurement.bit_operation();
        QStringList ops;
        if (bitOp.has_left_shift())  ops << QStringLiteral("left_shift=%1").arg(bitOp.left_shift());
        if (bitOp.has_right_shift()) ops << QStringLiteral("right_shift=%1").arg(bitOp.right_shift());
        if (bitOp.has_sign_extend()) ops << QStringLiteral("sign_extend");
        addField(layout, QStringLiteral("Bit Operation"), ops.join(QStringLiteral(", ")));
    }
    if (measurement.has_error_mask()) {
        addField(layout, QStringLiteral("Error Mask"), hexText(measurement.error_mask()));
    }
    addOptionalNumber(layout, QStringLiteral("Array Size"), measurement.has_array_size(), measurement.array_size());
    addField(layout, QStringLiteral("Matrix Dim"), joinNumbers(measurement.matrix_dim()));
    addField(layout,
             QStringLiteral("Matrix Layout"),
             measurement.has_layout()
                 ? enumKeyword(measurement.layout())
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
             enumKeyword(characteristic.type()));
    addField(identity, QStringLiteral("Address"), hexText(characteristic.address()));
    const int m = path.primaryIndex;
    addLink(identity, QStringLiteral("Record Layout"), m, characteristic.record_layout_ref(),
            {A2lEntityKind::RecordLayout});
    addLink(identity, QStringLiteral("Conversion"), m, characteristic.conversion(),
            {A2lEntityKind::CompuMethod});
    pushSection(sections, QStringLiteral("Identity"), std::move(identity));

    QList<DetailField> calibration;
    addNumberField(calibration, QStringLiteral("Max Diff"), characteristic.max_diff());
    addNumberField(calibration, QStringLiteral("Lower Limit"), characteristic.lower_limit());
    addNumberField(calibration, QStringLiteral("Upper Limit"), characteristic.upper_limit());
    addField(calibration,
             QStringLiteral("Byte Order"),
             characteristic.has_byte_order()
                 ? enumKeyword(characteristic.byte_order())
                 : QString());
    if (characteristic.has_bit_mask()) {
        addField(calibration, QStringLiteral("Bit Mask"), hexText(characteristic.bit_mask()));
    }
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
                 enumKeyword(axis.attribute()));
        addLink(axisFields, QStringLiteral("Input Quantity"), m, axis.input_quantity(),
                {A2lEntityKind::Measurement, A2lEntityKind::Instance});
        addLink(axisFields, QStringLiteral("Conversion"), m, axis.conversion(),
                {A2lEntityKind::CompuMethod});
        addNumberField(axisFields, QStringLiteral("Max Axis Points"), axis.max_axis_points());
        addNumberField(axisFields, QStringLiteral("Lower Limit"), axis.lower_limit());
        addNumberField(axisFields, QStringLiteral("Upper Limit"), axis.upper_limit());
        addLink(axisFields, QStringLiteral("Axis Pts Ref"), m, axis.axis_pts_ref(),
                {A2lEntityKind::AxisPts});
        addLink(axisFields, QStringLiteral("Curve Axis Ref"), m, axis.curve_axis_ref(),
                {A2lEntityKind::Characteristic});
        addField(axisFields,
                 QStringLiteral("Byte Order"),
                 axis.has_byte_order()
                     ? enumKeyword(axis.byte_order())
                     : QString());
        addField(axisFields,
                 QStringLiteral("Monotony"),
                 axis.has_monotony()
                     ? enumKeyword(axis.monotony())
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
    addField(identity, QStringLiteral("Address"), hexText(axis.address()));
    const int m = path.primaryIndex;
    addLink(identity, QStringLiteral("Input Quantity"), m, axis.input_quantity_ref(),
            {A2lEntityKind::Measurement, A2lEntityKind::Instance});
    addLink(identity, QStringLiteral("Record Layout"), m, axis.record_layout_ref(),
            {A2lEntityKind::RecordLayout});
    addLink(identity, QStringLiteral("Conversion"), m, axis.conversion_ref(),
            {A2lEntityKind::CompuMethod});
    pushSection(sections, QStringLiteral("Identity"), std::move(identity));

    QList<DetailField> axisFields;
    addNumberField(axisFields, QStringLiteral("Max Diff"), axis.max_diff());
    addNumberField(axisFields, QStringLiteral("Max Axis Points"), axis.max_axis_points());
    addNumberField(axisFields, QStringLiteral("Lower Limit"), axis.lower_limit());
    addNumberField(axisFields, QStringLiteral("Upper Limit"), axis.upper_limit());
    addField(axisFields,
             QStringLiteral("Byte Order"),
             axis.has_byte_order()
                 ? enumKeyword(axis.byte_order())
                 : QString());
    addField(axisFields,
             QStringLiteral("Calibration Access"),
             axis.has_calibration_access()
                 ? enumKeyword(axis.calibration_access())
                 : QString());
    addField(axisFields,
             QStringLiteral("Deposit Mode"),
             axis.has_deposit_mode()
                 ? enumKeyword(axis.deposit_mode())
                 : QString());
    addOptionalString(axisFields, QStringLiteral("Format"), axis.has_format(), axis.format());
    addOptionalString(axisFields, QStringLiteral("Physical Unit"), axis.has_phys_unit(), axis.phys_unit());
    addOptionalBool(axisFields, QStringLiteral("Guard Rails"), axis.has_guard_rails(), axis.guard_rails());
    addOptionalBool(axisFields, QStringLiteral("Read Only"), axis.has_read_only(), axis.read_only());
    addOptionalNumber(axisFields, QStringLiteral("Step Size"), axis.has_step_size(), axis.step_size());
    addField(axisFields,
             QStringLiteral("Monotony"),
             axis.has_monotony()
                 ? enumKeyword(axis.monotony())
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

QList<DetailSection> A2lDetailPresenter::blobDetails(const A2lPath& path) const {
    QList<DetailSection> sections;
    const auto* module = moduleAt(path.primaryIndex);
    if (!module || path.secondaryIndex < 0 || path.secondaryIndex >= module->blobs_size()) {
        return sections;
    }

    const auto& blob = module->blobs(path.secondaryIndex);
    QList<DetailField> identity;
    addField(identity, QStringLiteral("Name"), text(blob.name()));
    addField(identity, QStringLiteral("Long Identifier"), text(blob.long_identifier()));
    addField(identity, QStringLiteral("Address"), hexText(blob.address()));
    addNumberField(identity, QStringLiteral("Size (bytes)"), blob.size());
    pushSection(sections, QStringLiteral("Identity"), std::move(identity));

    QList<DetailField> properties;
    addField(properties, QStringLiteral("Address Type"),
             blob.has_address_type() ? enumKeyword(blob.address_type()) : QString());
    addField(properties, QStringLiteral("Calibration Access"),
             blob.has_calibration_access() ? enumKeyword(blob.calibration_access()) : QString());
    addOptionalString(properties, QStringLiteral("Display Identifier"),
                      blob.has_display_identifier(), blob.display_identifier());
    addOptionalNumber(properties, QStringLiteral("ECU Address Extension"),
                      blob.has_ecu_address_extension(), blob.ecu_address_extension());
    if (blob.has_max_refresh()) {
        addField(properties, QStringLiteral("Max Refresh"),
                 QStringLiteral("unit=%1 rate=%2")
                     .arg(blob.max_refresh().scaling_unit())
                     .arg(blob.max_refresh().rate()));
    }
    addOptionalString(properties, QStringLiteral("Model Link"), blob.has_model_link(), blob.model_link());
    if (blob.has_symbol_link()) {
        addField(properties, QStringLiteral("Symbol Link"),
                 QStringLiteral("%1 (offset %2)")
                     .arg(text(blob.symbol_link().symbol_name()))
                     .arg(blob.symbol_link().bit_offset()));
    }
    if (blob.annotations_size() > 0) {
        addNumberField(properties, QStringLiteral("Annotations"), blob.annotations_size());
    }
    addIfDataCounts(properties, blob.if_datas());
    pushSection(sections, QStringLiteral("Properties"), std::move(properties));
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
             enumKeyword(method.conversion_type()));
    addField(identity, QStringLiteral("Format"), text(method.format()));
    addField(identity, QStringLiteral("Unit"), text(method.unit()));
    pushSection(sections, QStringLiteral("Identity"), std::move(identity));

    QList<DetailField> conversion;
    if (method.conversion_type() != a2l::CONVERSION_TYPE_FORM) {
        addField(conversion, QStringLiteral("Formula"), conversionFormula(method));
    }
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
    addLink(conversion, QStringLiteral("Ref Unit"), path.primaryIndex, method.ref_unit(),
            {A2lEntityKind::Unit});
    addOptionalString(conversion,
                      QStringLiteral("Status String Ref"),
                      method.has_status_string_ref(),
                      method.status_string_ref());
    addOptionalString(conversion, QStringLiteral("Formula"), method.has_formula_raw(), method.formula_raw());
    pushSection(sections, QStringLiteral("Conversion"), std::move(conversion));
    if (method.has_compu_tab_ref()) {
        appendConversionTable(sections, *module, method.compu_tab_ref());
    }
    if (method.has_status_string_ref()) {
        appendConversionTable(sections, *module, method.status_string_ref());
    }
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
            addField(cf, QStringLiteral("Data Type"), enumKeyword(fv.datatype()));
            addField(cf, QStringLiteral("Index Mode"), enumKeyword(fv.index_mode()));
            addField(cf, QStringLiteral("Address Type"), enumKeyword(fv.address_type()));
        } else if (comp.has_axis_points()) {
            const auto& ap = comp.axis_points();
            addField(cf, QStringLiteral("Data Type"), enumKeyword(ap.datatype()));
            addField(cf, QStringLiteral("Index Order"), enumKeyword(ap.index_order()));
            addField(cf, QStringLiteral("Address Type"), enumKeyword(ap.address_type()));
        } else if (comp.has_axis_rescale()) {
            const auto& ar = comp.axis_rescale();
            addField(cf, QStringLiteral("Data Type"), enumKeyword(ar.datatype()));
            addField(cf, QStringLiteral("Index Order"), enumKeyword(ar.index_order()));
            addField(cf, QStringLiteral("Address Type"), enumKeyword(ar.address_type()));
            addNumberField(cf, QStringLiteral("Max Rescale Pairs"), ar.max_number_of_rescale_pairs());
        } else if (comp.has_axis_count()) {
            addField(cf, QStringLiteral("Data Type"), enumKeyword(comp.axis_count().datatype()));
        } else if (comp.has_rescale_count()) {
            addField(cf, QStringLiteral("Data Type"), enumKeyword(comp.rescale_count().datatype()));
        } else if (comp.has_axis_operand()) {
            addField(cf, QStringLiteral("Data Type"), enumKeyword(comp.axis_operand().datatype()));
        } else if (comp.has_address_reference()) {
            addField(cf, QStringLiteral("Data Type"), enumKeyword(comp.address_reference().datatype()));
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
    addField(identity, QStringLiteral("Type"), enumKeyword(unit.type()));
    addLink(identity, QStringLiteral("Ref Unit"), path.primaryIndex, unit.ref_unit(), {A2lEntityKind::Unit});
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

    const int m = path.primaryIndex;
    const auto adjustable = {A2lEntityKind::Characteristic, A2lEntityKind::AxisPts,
                             A2lEntityKind::Blob, A2lEntityKind::Instance};
    const auto measured = {A2lEntityKind::Measurement, A2lEntityKind::Instance};
    appendNameList(sections, QStringLiteral("Defined Characteristics"), m, function.def_characteristics(), adjustable);
    appendNameList(sections, QStringLiteral("Referenced Characteristics"), m, function.ref_characteristics(), adjustable);
    appendNameList(sections, QStringLiteral("Input Measurements"), m, function.in_measurements(), measured);
    appendNameList(sections, QStringLiteral("Output Measurements"), m, function.out_measurements(), measured);
    appendNameList(sections, QStringLiteral("Local Measurements"), m, function.loc_measurements(), measured);
    appendNameList(sections, QStringLiteral("Sub Functions"), m, function.sub_functions(), {A2lEntityKind::Function});
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

    const int m = path.primaryIndex;
    appendNameList(sections, QStringLiteral("Characteristics"), m, group.ref_characteristics(),
                   {A2lEntityKind::Characteristic, A2lEntityKind::AxisPts, A2lEntityKind::Blob,
                    A2lEntityKind::Instance});
    appendNameList(sections, QStringLiteral("Measurements"), m, group.ref_measurements(),
                   {A2lEntityKind::Measurement, A2lEntityKind::Instance});
    appendNameList(sections, QStringLiteral("Sub Groups"), m, group.sub_groups(), {A2lEntityKind::Group});
    appendNameList(sections, QStringLiteral("Functions"), m, group.function_list(), {A2lEntityKind::Function});
    return sections;
}

QList<DetailSection> A2lDetailPresenter::frameDetails(const A2lPath& path) const {
    QList<DetailSection> sections;
    const auto* module = moduleAt(path.primaryIndex);
    if (!module || path.secondaryIndex < 0 || path.secondaryIndex >= module->frames_size()) {
        return sections;
    }

    const auto& frame = module->frames(path.secondaryIndex);
    QList<DetailField> identity;
    addField(identity, QStringLiteral("Name"), text(frame.name()));
    addField(identity, QStringLiteral("Long Identifier"), text(frame.long_identifier()));
    addNumberField(identity, QStringLiteral("Scaling Unit"), frame.scaling_unit());
    addNumberField(identity, QStringLiteral("Rate"), frame.rate());
    addIfDataCounts(identity, frame.if_datas());
    pushSection(sections, QStringLiteral("Identity"), std::move(identity));
    appendNameList(sections, QStringLiteral("Measurements"), path.primaryIndex, frame.frame_measurements(),
                   {A2lEntityKind::Measurement, A2lEntityKind::Instance});
    return sections;
}

QList<DetailSection> A2lDetailPresenter::transformerDetails(const A2lPath& path) const {
    QList<DetailSection> sections;
    const auto* module = moduleAt(path.primaryIndex);
    if (!module || path.secondaryIndex < 0 || path.secondaryIndex >= module->transformers_size()) {
        return sections;
    }

    const auto& transformer = module->transformers(path.secondaryIndex);
    const int m = path.primaryIndex;
    QList<DetailField> identity;
    addField(identity, QStringLiteral("Name"), text(transformer.name()));
    addField(identity, QStringLiteral("Version"), text(transformer.version()));
    addField(identity, QStringLiteral("32-bit DLL"), text(transformer.executable32()));
    addField(identity, QStringLiteral("64-bit DLL"), text(transformer.executable64()));
    addField(identity, QStringLiteral("Timeout"),
             transformer.timeout_ms() > 0 ? QStringLiteral("%1 ms").arg(transformer.timeout_ms())
                                          : QStringLiteral("not specified"));
    addField(identity, QStringLiteral("Trigger"), enumKeyword(transformer.trigger()));
    addLink(identity, QStringLiteral("Inverse"), m, transformer.inverse_transformer(),
            {A2lEntityKind::Transformer});
    pushSection(sections, QStringLiteral("Identity"), std::move(identity));

    const auto adjustable = {A2lEntityKind::Characteristic, A2lEntityKind::AxisPts,
                             A2lEntityKind::Blob, A2lEntityKind::Instance};
    appendNameList(sections, QStringLiteral("Input Objects"), m, transformer.in_objects(), adjustable);
    appendNameList(sections, QStringLiteral("Output Objects"), m, transformer.out_objects(), adjustable);
    return sections;
}

QList<DetailSection> A2lDetailPresenter::userRightsDetails(const A2lPath& path) const {
    QList<DetailSection> sections;
    const auto* module = moduleAt(path.primaryIndex);
    if (!module || path.secondaryIndex < 0 || path.secondaryIndex >= module->user_rights_size()) {
        return sections;
    }

    const auto& rights = module->user_rights(path.secondaryIndex);
    QList<DetailField> identity;
    addField(identity, QStringLiteral("User Level"), text(rights.user_level_id()));
    addField(identity, QStringLiteral("Read Only"), boolText(rights.has_read_only() && rights.read_only()));
    pushSection(sections, QStringLiteral("Identity"), std::move(identity));
    appendNameList(sections, QStringLiteral("Groups"), path.primaryIndex, rights.ref_groups(),
                   {A2lEntityKind::Group});
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
                 enumKeyword(item.type()));
        addLink(fields, QStringLiteral("Record Layout"), path.primaryIndex, item.record_layout_ref(),
                {A2lEntityKind::RecordLayout});
        addLink(fields, QStringLiteral("Conversion"), path.primaryIndex, item.conversion(),
                {A2lEntityKind::CompuMethod});
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
        addLink(fields, QStringLiteral("Input Quantity"), path.primaryIndex, item.input_quantity(),
                {A2lEntityKind::Measurement, A2lEntityKind::Instance});
        addLink(fields, QStringLiteral("Record Layout"), path.primaryIndex, item.record_layout_ref(),
                {A2lEntityKind::RecordLayout});
        addLink(fields, QStringLiteral("Conversion"), path.primaryIndex, item.conversion(),
                {A2lEntityKind::CompuMethod});
        addNumberField(fields, QStringLiteral("Max Axis Points"), item.max_axis_points());
        addOptionalString(fields, QStringLiteral("Format"), item.has_format(), item.format());
        addOptionalString(fields, QStringLiteral("Physical Unit"), item.has_phys_unit(), item.phys_unit());
        pushSection(sections, QStringLiteral("Typedef Axis"), std::move(fields));
        return sections;
    }

    if (path.tertiaryIndex == kTypedefMeasurementCategory) {
        if (path.secondaryIndex >= module->typedef_measurements_size()) {
            return sections;
        }

        const auto& item = module->typedef_measurements(path.secondaryIndex);
        QList<DetailField> fields;
        addField(fields, QStringLiteral("Name"), text(item.name()));
        addField(fields, QStringLiteral("Long Identifier"), text(item.long_identifier()));
        addField(fields, QStringLiteral("Data Type"), enumKeyword(item.datatype()));
        addLink(fields, QStringLiteral("Conversion"), path.primaryIndex, item.conversion(),
                {A2lEntityKind::CompuMethod});
        addNumberField(fields, QStringLiteral("Lower Limit"), item.lower_limit());
        addNumberField(fields, QStringLiteral("Upper Limit"), item.upper_limit());
        addField(fields, QStringLiteral("Byte Order"),
                 item.has_byte_order() ? enumKeyword(item.byte_order()) : QString());
        if (item.has_bit_mask()) {
            addField(fields, QStringLiteral("Bit Mask"), hexText(item.bit_mask()));
        }
        addField(fields, QStringLiteral("Matrix Dim"), joinNumbers(item.matrix_dim()));
        addOptionalString(fields, QStringLiteral("Format"), item.has_format(), item.format());
        addOptionalString(fields, QStringLiteral("Physical Unit"), item.has_phys_unit(), item.phys_unit());
        pushSection(sections, QStringLiteral("Typedef Measurement"), std::move(fields));
        return sections;
    }

    if (path.tertiaryIndex == kTypedefBlobCategory) {
        if (path.secondaryIndex >= module->typedef_blobs_size()) {
            return sections;
        }

        const auto& item = module->typedef_blobs(path.secondaryIndex);
        QList<DetailField> fields;
        addField(fields, QStringLiteral("Name"), text(item.name()));
        addField(fields, QStringLiteral("Long Identifier"), text(item.long_identifier()));
        addNumberField(fields, QStringLiteral("Size (bytes)"), item.size());
        addField(fields, QStringLiteral("Address Type"),
                 item.has_address_type() ? enumKeyword(item.address_type()) : QString());
        pushSection(sections, QStringLiteral("Typedef Blob"), std::move(fields));
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
    addLink(identity, QStringLiteral("Type Ref"), path.primaryIndex, instance.type_ref(),
            {A2lEntityKind::TypedefItem});
    addField(identity, QStringLiteral("Address"), hexText(instance.address()));
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
             variant.has_var_naming() ? enumKeyword(variant.var_naming()) : QString());
    addNumberField(fields, QStringLiteral("Characteristics"), variant.var_characteristics_size());
    addNumberField(fields, QStringLiteral("Criteria"), variant.var_criteria_size());
    addNumberField(fields, QStringLiteral("Forbidden Combinations"), variant.var_forbidden_combs_size());
    pushSection(sections, QStringLiteral("Variant Coding"), std::move(fields));
    return sections;
}
